// The bulk workers of the Trade offers screen: accept all, confirm a sent offer, and the send
// run behind the "Send items" flow. The dialogs and the flow itself are drawn in
// trade_offers_screen.cpp and trade_offers/send_flow.cpp.

#include "ui/screens/trade_offers_detail.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/job_pump.hpp"
#include "core/log.hpp"
#include "core/trade/actions.hpp"
#include "core/trade/inventory.hpp"
#include "core/trade/trade_audit.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/confirmations_screen.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"

namespace sam::ui::screens::trade_offers_detail {

namespace {

namespace trade = core::trade;

// Runs fn, and once more after an automatic sign-in when the session expired and the sign-in
// is not on its cooldown. A failed sign-in replaces the error with its own.
template <typename Fn>
auto with_relogin(app::AppState& state, core::Account& creds, Fn&& fn) {
    auto res = fn(creds);
    if (!res.ok && res.needs_relogin && state.relogin_cooldown_seconds(creds.id) <= 0) {
        std::string e;
        if (state.auto_relogin(creds.id, creds, &e)) {
            apply_refreshed_tokens(state, creds);
            res = fn(creds);
        } else {
            res.error = "auto-relogin failed: " + e;
        }
    }
    return res;
}

std::string count_text(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

void set_row(std::uint64_t run, std::size_t index, SendStatus status, int items = -1, const std::string* offer = nullptr,
             const std::string* detail = nullptr) {
    std::lock_guard lk(g_mtx);
    if (g_send_run.id != run || index >= g_send_run.rows.size()) return;
    SendResult& r = g_send_run.rows[index];
    r.status = status;
    if (items >= 0) r.item_count = items;
    if (offer != nullptr) r.offer_id = *offer;
    if (detail != nullptr) r.detail = *detail;
}

}  // namespace

void bulk_accept_all(app::AppState& state, const std::string& scope) {
    struct Target {
        core::Account acc;
        std::string oid;
        std::uint64_t partner;
    };
    std::vector<Target> targets;
    {
        std::lock_guard lk(g_mtx);
        for (auto& a : state.vault.accounts) {
            if (!scope.empty() && a.id != scope) continue;
            if (!account_can_trade(a)) continue;
            const auto it = g_states.find(a.id);
            if (it == g_states.end()) continue;
            for (const auto& o : it->second.received) {
                if (o.state == trade::TradeOfferState::Active) targets.push_back({a, o.offer_id, o.partner_steam_id_64});
            }
        }
    }
    if (targets.empty()) return;

    const int total = static_cast<int>(targets.size());
    kit::show_toast(state, "Accepting " + count_text(total, "offer", "offers"), {.id = "bulk-accept", .seconds = -1});

    const int stagger = std::max(1500, state.settings.trade.refresh_stagger_ms);
    app::job_pump::submit([&state, targets = std::move(targets), stagger, total]() mutable {
        int ok = 0;
        int failed = 0;
        int processed = 0;
        bool needs_conf_any = false;
        for (auto& t : targets) {
            {
                std::lock_guard lk(g_mtx);
                g_in_flight.insert(t.oid);
            }
            core::Account creds = t.acc;
            auto res = with_relogin(state, creds, [&](core::Account& a) {
                return trade::accept_trade_offer(a, t.oid, t.partner);
            });
            apply_refreshed_tokens(state, creds);
            {
                std::lock_guard lk(g_mtx);
                g_in_flight.erase(t.oid);
            }
            if (res.ok) {
                ++ok;
                needs_conf_any = needs_conf_any || res.needs_confirmation;
                state.post_ui_callback([&state, aid = t.acc.id, oid = t.oid] { erase_offer(state, aid, oid, true); });
            } else {
                ++failed;
                state.post_ui_callback([oid = t.oid, err = res.error] { g_offer_errors[oid] = err; });
            }
            ++processed;
            state.post_ui_callback([&state, processed, total, ok] {
                kit::show_toast(state,
                                "Accepting offers: " + std::to_string(processed) + " of " + std::to_string(total) +
                                    " done, " + std::to_string(ok) + " accepted",
                                {.id = "bulk-accept", .seconds = -1});
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(stagger));
        }
        state.post_ui_callback([&state, ok, failed, total, needs_conf_any] {
            std::string msg = "Accepted " + std::to_string(ok) + " of " + count_text(total, "offer", "offers");
            if (failed) msg += ", " + std::to_string(failed) + " failed";
            const int dwell = std::max(state.settings.notifications.toast_duration_seconds, 12);
            kit::show_toast(state, msg,
                            {.tone = ok == total ? widgets::ToastTone::Success : widgets::ToastTone::Warning,
                             .id = "bulk-accept",
                             .seconds = dwell});
            // An accept that gives items away queues a mobile confirmation.
            if (needs_conf_any) confirmations_trigger_refresh_all(state);
        });
    });
}

void submit_confirm_sent(app::AppState& state, const core::Account& acc, const std::string& offer_id) {
    {
        std::lock_guard lk(g_mtx);
        if (!g_in_flight.insert(offer_id).second) return;
    }
    g_offer_errors.erase(offer_id);
    auto cap = acc;
    app::job_pump::submit([&state, cap, offer_id]() mutable {
        trade::ConfirmResult cr;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            cr = trade::confirm_sent_offer(cap, offer_id);
            if (cr.ok || !cr.not_found) break;
        }
        if (!cr.ok && cr.error.empty()) cr.error = "confirmation not found yet";
        apply_refreshed_tokens(state, cap);
        {
            std::lock_guard lk(g_mtx);
            g_in_flight.erase(offer_id);
        }
        state.post_ui_callback([&state, aid = cap.id, offer_id, ok = cr.ok, err = cr.error] {
            if (ok) {
                kit::show_toast(state, "Offer confirmed",
                                {.tone = widgets::ToastTone::Success, .id = "trade-" + offer_id, .account_id = aid});
                {
                    std::lock_guard lk(g_mtx);
                    g_states[aid].last_refresh_unix = 0;
                }
                if (auto* a = state.find_account(aid)) submit_account_refresh(state, *a);
                confirmations_trigger_refresh_all(state);
            } else {
                g_offer_errors[offer_id] = err;
                kit::show_toast(state, "Could not confirm the offer. " + trade_offers_ui::human_error(err),
                                {.tone = widgets::ToastTone::Destructive, .id = "trade-" + offer_id, .account_id = aid});
            }
        });
    });
}

std::uint64_t start_send_run(app::AppState& state, std::vector<SendJob> jobs, trade::TradeUrl tu, std::string message) {
    static std::uint64_t next_run = 0;
    const std::uint64_t run = ++next_run;
    if (!tu.ok || jobs.empty()) return 0;
    {
        std::lock_guard lk(g_mtx);
        g_send_run = {};
        g_send_run.id = run;
        for (const auto& j : jobs) {
            SendResult r;
            r.account_id = j.account.id;
            r.item_count = j.all_tradable ? 0 : static_cast<int>(j.give.size());
            g_send_run.rows.push_back(std::move(r));
        }
    }

    const int n = static_cast<int>(jobs.size());
    const bool bulk = n > 1;
    if (bulk) {
        kit::show_toast(state, "Sending offers from " + count_text(n, "account", "accounts"),
                        {.id = "bulk-send", .seconds = -1});
    }
    const int stagger = std::max(2000, state.settings.trade.refresh_stagger_ms);
    const bool auto_conf = state.settings.trade.auto_confirm_sent;

    app::job_pump::submit([&state, run, jobs = std::move(jobs), tu, message, stagger, auto_conf, n, bulk]() mutable {
        int sent_ok = 0;
        int failed = 0;
        int skipped = 0;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            SendJob& job = jobs[i];
            const core::Account seed = job.account;
            core::Account creds = seed;
            const trade::TradeAuditSource source =
                bulk ? trade::TradeAuditSource::UserBulk : trade::TradeAuditSource::UserSingle;

            auto record = [&state](trade::TradeAuditEntry e) {
                state.post_ui_callback([&state, e = std::move(e)]() mutable { state.trade_audit.record(std::move(e)); });
            };

            if (job.all_tradable) {
                set_row(run, i, SendStatus::LoadingInventory);
                auto inv = with_relogin(state, creds, [&](core::Account& a) {
                    return trade::fetch_inventory(a, a.steam_id_64, 730, 2, 2000, 0);
                });
                if (tokens_changed(seed, creds)) apply_refreshed_tokens(state, creds);
                if (!inv.ok) {
                    const std::string detail = "inventory fetch failed: " + inv.error;
                    set_row(run, i, SendStatus::Failed, -1, nullptr, &detail);
                    trade::TradeAuditEntry e;
                    e.unix_time = now_unix();
                    e.account_id = seed.id;
                    e.account_login = seed.login;
                    e.source = source;
                    e.outcome = trade::TradeAuditOutcome::Failed;
                    e.detail = detail;
                    record(std::move(e));
                    ++failed;
                    if (bulk) {
                        state.post_ui_callback([&state, done = static_cast<int>(i) + 1, n, sent_ok] {
                            kit::show_toast(state,
                                            "Sending offers: " + std::to_string(done) + " of " + std::to_string(n) +
                                                " accounts done, " + std::to_string(sent_ok) + " sent",
                                            {.id = "bulk-send", .seconds = -1});
                        });
                    } else {
                        state.post_ui_callback([&state, aid = seed.id, detail] {
                            kit::show_toast(state, "Could not send the offer. " + trade_offers_ui::human_error(detail),
                                            {.tone = widgets::ToastTone::Destructive, .id = "send-fail-" + aid,
                                             .account_id = aid});
                        });
                    }
                    continue;
                }
                job.give.clear();
                for (const auto& it : inv.items) {
                    if (!it.tradable) continue;
                    trade::TradeAssetRef r;
                    r.app_id = 730;
                    r.context_id = 2;
                    r.asset_id = it.asset_id;
                    r.amount = 1;
                    job.give.push_back(r);
                }
                if (job.give.empty()) {
                    const std::string detail = "no tradable items";
                    set_row(run, i, SendStatus::Skipped, 0, nullptr, &detail);
                    ++skipped;
                    continue;
                }
            }

            const int item_count = static_cast<int>(job.give.size());
            set_row(run, i, SendStatus::Sending, item_count);
            auto res = with_relogin(state, creds, [&](core::Account& a) {
                return trade::send_trade_offer(a, tu, job.give, message);
            });
            apply_refreshed_tokens(state, creds);

            trade::TradeAuditEntry e;
            e.unix_time = now_unix();
            e.account_id = seed.id;
            e.account_login = seed.login;
            e.item_count = item_count;
            e.source = source;

            if (!res.ok) {
                ++failed;
                const std::string err = res.error;
                SAM_LOG_INFO("trade send {}: failed items={} err={}", seed.login, item_count, err);
                set_row(run, i, SendStatus::Failed, -1, nullptr, &err);
                e.outcome = trade::TradeAuditOutcome::Failed;
                e.detail = err;
                record(std::move(e));
                if (!bulk) {
                    state.post_ui_callback([&state, aid = seed.id, err] {
                        kit::show_toast(state, "Could not send the offer. " + trade_offers_ui::human_error(err),
                                        {.tone = widgets::ToastTone::Destructive, .id = "send-fail-" + aid,
                                         .account_id = aid});
                    });
                }
            } else {
                ++sent_ok;
                const std::string oid = res.offer_id;
                bool confirmed = false;
                std::string conf_note;
                if (res.needs_confirmation && auto_conf) {
                    set_row(run, i, SendStatus::Confirming, -1, &oid);
                    for (int attempt = 0; attempt < 3 && !confirmed; ++attempt) {
                        if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                        const auto cr = trade::confirm_sent_offer(creds, oid);
                        if (cr.ok) {
                            confirmed = true;
                            break;
                        }
                        if (!cr.not_found) {
                            conf_note = cr.error;
                            break;
                        }
                    }
                    if (!confirmed && conf_note.empty()) conf_note = "confirmation not found yet";
                }
                const trade::TradeAuditOutcome outcome = !res.needs_confirmation ? trade::TradeAuditOutcome::Sent
                                                         : confirmed ? trade::TradeAuditOutcome::SentAndConfirmed
                                                                     : trade::TradeAuditOutcome::NeedsConfirmation;
                SAM_LOG_INFO("trade send {}: offer={} items={} outcome={}", seed.login, oid, item_count,
                             static_cast<int>(outcome));
                const SendStatus status = outcome == trade::TradeAuditOutcome::Sent ? SendStatus::Sent
                                          : outcome == trade::TradeAuditOutcome::SentAndConfirmed
                                              ? SendStatus::SentConfirmed
                                              : SendStatus::NeedsConfirmation;
                set_row(run, i, status, -1, &oid, &conf_note);
                e.offer_id = oid;
                e.outcome = outcome;
                if (outcome == trade::TradeAuditOutcome::NeedsConfirmation) e.detail = conf_note;
                record(std::move(e));

                if (!bulk) {
                    const bool needs_conf = res.needs_confirmation;
                    state.post_ui_callback([&state, aid = seed.id, oid, needs_conf, auto_conf, confirmed, conf_note] {
                        if (!needs_conf) {
                            kit::show_toast(state, "Offer sent",
                                            {.tone = widgets::ToastTone::Success, .id = "send-" + oid, .account_id = aid});
                        } else if (auto_conf && confirmed) {
                            kit::show_toast(state, "Offer sent and confirmed",
                                            {.tone = widgets::ToastTone::Success, .id = "send-" + oid, .account_id = aid});
                        } else {
                            std::string m = "Offer sent: confirm it in Confirmations";
                            if (auto_conf && !conf_note.empty()) m += " (" + conf_note + ")";
                            kit::show_toast(state, m,
                                            {.tone = widgets::ToastTone::Warning, .id = "send-" + oid, .account_id = aid});
                            confirmations_trigger_refresh_all(state);
                        }
                        {
                            std::lock_guard lk(g_mtx);
                            g_states[aid].last_refresh_unix = 0;
                        }
                        if (auto* a = state.find_account(aid)) submit_account_refresh(state, *a);
                    });
                }
            }

            if (bulk) {
                state.post_ui_callback([&state, done = static_cast<int>(i) + 1, n, sent_ok] {
                    kit::show_toast(state,
                                    "Sending offers: " + std::to_string(done) + " of " + std::to_string(n) +
                                        " accounts done, " + std::to_string(sent_ok) + " sent",
                                    {.id = "bulk-send", .seconds = -1});
                });
                if (i + 1 < jobs.size()) std::this_thread::sleep_for(std::chrono::milliseconds(stagger));
            }
        }

        {
            std::lock_guard lk(g_mtx);
            if (g_send_run.id == run) g_send_run.finished = true;
        }
        if (bulk) {
            state.post_ui_callback([&state, sent_ok, failed, skipped, n] {
                std::string msg = "Sent offers from " + std::to_string(sent_ok) + " of " +
                                  count_text(n, "account", "accounts");
                if (failed) msg += ", " + std::to_string(failed) + " failed";
                if (skipped) msg += ", " + std::to_string(skipped) + " had nothing to send";
                const int dwell = std::max(state.settings.notifications.toast_duration_seconds, 12);
                kit::show_toast(state, msg,
                                {.tone = sent_ok == n ? widgets::ToastTone::Success : widgets::ToastTone::Warning,
                                 .id = "bulk-send",
                                 .seconds = dwell});
                confirmations_trigger_refresh_all(state);
            });
        }
    });
    return run;
}

}  // namespace sam::ui::screens::trade_offers_detail
