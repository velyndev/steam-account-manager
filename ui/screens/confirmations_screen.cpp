#include "ui/screens/confirmations_screen.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/job_pump.hpp"
#include "core/sda/confirmation.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/confirmations/view.hpp"
#include "ui/screens/confirmations_detail.hpp"

namespace sam::ui::screens {

using namespace confirmations_detail;

namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool session_looks_unusable(const core::Account& a) {
    return a.session_id.empty() || a.access_token.empty() || a.steam_id_64 == 0;
}

bool is_session_error(const std::string& err) {
    if (err.empty()) return false;

    if (err.find("missing maFile data") != std::string::npos) return false;
    if (err == "account has no active session") return true;
    if (err == "http 401" || err == "http 403") return true;
    if (err.find("session expired") != std::string::npos) return true;
    if (err.find("needauth")        != std::string::npos) return true;

    std::string lower(err);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("oh nooooooes") != std::string::npos) return true;

    if (err == sda::kRespondSuccessFalse) return true;

    return false;
}

std::string rate_limited_text(std::int64_t cd) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "login rate-limited, try again in %lld s", static_cast<long long>(cd));
    return buf;
}

void apply_refreshed_tokens(app::AppState& state, const core::Account& creds) {
    state.post_ui_callback([&state, aid = creds.id,
                            rt  = creds.refresh_token,
                            at  = creds.access_token,
                            exp = creds.access_token_expires,
                            ls  = creds.steam_login_secure,
                            sid = creds.session_id] {
        auto* a = state.find_account(aid);
        if (!a) return;
        a->refresh_token        = rt;
        a->access_token         = at;
        a->access_token_expires = exp;
        a->steam_login_secure   = ls;
        a->session_id           = sid;
        state.vault_dirty = true;
        state.save_vault_if_dirty();
    });
}

void recount_pending(app::AppState& state) {
    int total = 0;
    {
        std::lock_guard lk(g_mtx);
        for (const auto& [aid, pl] : g_lists) {
            total += static_cast<int>(pl.items.size());
        }
    }
    state.pending_confirmations_count.store(total, std::memory_order_relaxed);
    state.pending_confirmations_loaded.store(true, std::memory_order_relaxed);
}

void erase_item(app::AppState& state, const std::string& account_id, const std::string& conf_id) {
    {
        std::lock_guard lk(g_mtx);
        auto& items = g_lists[account_id].items;
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&](const sda::Confirmation& c) { return c.id == conf_id; }),
                    items.end());
    }
    recount_pending(state);
}

void record_audit(app::AppState& state, const core::Account& a, const sda::Confirmation& c, bool allow,
                  sda::AuditSource source, std::int64_t when) {
    sda::AuditEntry e;
    e.unix_time = when;
    e.account_id = a.id;
    e.account_login = a.login;
    e.conf_id = c.id;
    e.conf_type = c.type;
    e.headline = c.headline;
    e.allow = allow;
    e.source = source;
    state.conf_audit.record(std::move(e));
}

bool should_auto_approve(const app::AppState& state, const sda::Confirmation& c) {
    const auto& cfg = state.settings.confirmations;
    if (!cfg.auto_approve_enabled) return false;
    switch (c.type) {
        case sda::ConfirmationType::MarketListing:
            return cfg.auto_approve_market;
        case sda::ConfirmationType::PhoneNumberChange:
            return cfg.auto_approve_phone_change;
        default:
            return false;
    }
}

void submit_single(app::AppState& state, const core::Account& a, const sda::Confirmation& c, bool allow) {
    auto cap_a = a;
    auto cap_c = c;
    app::job_pump::submit([&state, cap_a, cap_c, allow]() mutable {
        std::string err;
        bool ok = sda::respond_to_confirmation(cap_a, cap_c, allow, &err);

        if (!ok && err == sda::kRespondSuccessFalse) {
            auto fresh = sda::fetch_confirmations(cap_a);
            if (fresh.ok) {
                auto it = std::find_if(fresh.items.begin(), fresh.items.end(),
                                       [&](const sda::Confirmation& f) { return f.id == cap_c.id; });
                if (it == fresh.items.end()) return;
                cap_c.nonce = it->nonce;
                err.clear();
                ok = sda::respond_to_confirmation(cap_a, cap_c, allow, &err);
            }
        }

        if (!ok && is_session_error(err)) {
            const auto cd = state.relogin_cooldown_seconds(cap_a.id);
            if (cd > 0) {
                err = rate_limited_text(cd);
            } else {
                std::string relogin_err;
                if (state.auto_relogin(cap_a.id, cap_a, &relogin_err)) {
                    apply_refreshed_tokens(state, cap_a);
                    err.clear();
                    ok = sda::respond_to_confirmation(cap_a, cap_c, allow, &err);
                } else {
                    err = "auto-relogin failed: " + relogin_err;
                }
            }
        }
        if (!ok) {
            std::lock_guard lk(g_mtx);
            g_lists[cap_a.id].last_error = err.empty() ? std::string{"respond failed"} : err;
        }
    });
}

// The bulk respond of one account on the calling worker, with its session repair.
void respond_bulk_now(app::AppState& state, core::Account& account, const std::vector<sda::Confirmation>& items,
                      bool allow) {
    std::string err;
    bool ok = sda::respond_bulk(account, items, allow, &err);
    if (!ok && is_session_error(err)) {
        const auto cd = state.relogin_cooldown_seconds(account.id);
        if (cd > 0) {
            err = rate_limited_text(cd);
        } else {
            std::string relogin_err;
            if (state.auto_relogin(account.id, account, &relogin_err)) {
                apply_refreshed_tokens(state, account);
                err.clear();
                ok = sda::respond_bulk(account, items, allow, &err);
            } else {
                err = "auto-relogin failed: " + relogin_err;
            }
        }
    }
    if (!ok) {
        std::lock_guard lk(g_mtx);
        g_lists[account.id].last_error = err.empty() ? std::string{"bulk respond failed"} : err;
    }
}

}  // namespace

namespace confirmations_detail {

std::int64_t refresh_cooldown_remaining(const app::AppState& state, const std::string& account_id) {
    const auto cooldown = state.settings.confirmations.per_account_cooldown_seconds;
    if (cooldown <= 0) return 0;
    const auto it = state.conf_last_refresh_unix.find(account_id);
    if (it == state.conf_last_refresh_unix.end()) return 0;
    const auto since = unix_now() - it->second;
    return since < cooldown ? cooldown - since : 0;
}

void submit_account_refresh(app::AppState& state, const core::Account& seed) {
    if (session_looks_unusable(seed) && seed.password.empty()) {
        std::lock_guard lk(g_mtx);
        auto& pl = g_lists[seed.id];
        pl.refreshing = false;
        pl.last_error = "no stored password, open Add Account and run the full login";
        return;
    }

    if (refresh_cooldown_remaining(state, seed.id) > 0) return;

    {
        std::lock_guard lk(g_mtx);
        auto& pl = g_lists[seed.id];
        if (pl.refreshing) return;
        pl.refreshing = true;
        pl.last_error.clear();
    }
    const auto snapshot = seed;
    app::job_pump::submit([&state, snapshot]() mutable {
        core::Account creds = snapshot;
        sda::ConfirmationFetchResult result;

        const bool need_session = session_looks_unusable(creds);
        if (!need_session) {
            result = sda::fetch_confirmations(creds);
        }

        if (need_session || (!result.ok && is_session_error(result.error))) {
            const auto cd = state.relogin_cooldown_seconds(creds.id);
            if (cd > 0) {
                result.ok = false;
                result.error = rate_limited_text(cd);
                result.items.clear();
            } else {
                std::string relogin_err;
                if (state.auto_relogin(creds.id, creds, &relogin_err)) {
                    apply_refreshed_tokens(state, creds);
                    result = sda::fetch_confirmations(creds);
                } else {
                    result.ok = false;
                    result.error = "auto-relogin failed: " + relogin_err;
                    result.items.clear();
                }
            }
        }

        const auto done_unix = unix_now();
        const bool ok = result.ok;
        const bool session_err = !ok && is_session_error(result.error);
        {
            std::lock_guard lk(g_mtx);
            auto& pl = g_lists[creds.id];
            pl.refreshing = false;
            if (ok) {
                pl.items = std::move(result.items);
                pl.last_error.clear();
            } else {
                pl.last_error = result.error;
            }
        }
        state.post_ui_callback([&state, aid = creds.id, done_unix, ok, session_err] {
            state.conf_last_refresh_unix[aid] = done_unix;
            const int done = state.conf_refresh_all_done.fetch_add(1, std::memory_order_relaxed) + 1;
            const int total = state.conf_refresh_all_total.load(std::memory_order_relaxed);
            if (ok) {
                state.conf_consecutive_failures.erase(aid);
                state.conf_permanent_failure.erase(aid);

                if (auto* a = state.find_account(aid)) {
                    std::vector<sda::Confirmation> auto_picks;
                    {
                        std::lock_guard lk(g_mtx);
                        for (const auto& c : g_lists[aid].items) {
                            if (should_auto_approve(state, c)) auto_picks.push_back(c);
                        }
                    }
                    for (const auto& c : auto_picks) {
                        submit_single(state, *a, c, true);
                        record_audit(state, *a, c, true, sda::AuditSource::Auto, done_unix);
                        erase_item(state, a->id, c.id);
                    }
                }
            } else if (session_err) {
                const int n = ++state.conf_consecutive_failures[aid];
                if (n >= state.settings.confirmations.permanent_failure_threshold) {
                    state.conf_permanent_failure.insert(aid);
                }
            }
            if (total > 0 && done >= total) {
                const int baseline = state.conf_refresh_batch_baseline.load(std::memory_order_relaxed);
                const int now_count = state.pending_confirmations_count.load(std::memory_order_relaxed);
                const int delta = now_count - baseline;
                if (delta > 0 && state.settings.confirmations.toast_on_new_confirmations) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), delta == 1 ? "%d new confirmation" : "%d new confirmations",
                                  delta);
                    kit::show_toast(state, buf, {.id = "conf-new-" + std::to_string(done_unix), .seconds = 6});
                }
                state.conf_refresh_all_total.store(0, std::memory_order_relaxed);
            }
        });
        recount_pending(state);
    });
}

void refresh_all(app::AppState& state) {
    std::vector<core::Account> snapshots;
    snapshots.reserve(state.vault.accounts.size());
    for (auto& a : state.vault.accounts) {
        if (!a.sda.has_value()) continue;
        if (state.conf_permanent_failure.count(a.id)) continue;
        if (refresh_cooldown_remaining(state, a.id) > 0) continue;
        snapshots.push_back(a);
    }
    if (snapshots.empty()) return;

    state.conf_refresh_all_total.store(static_cast<int>(snapshots.size()), std::memory_order_relaxed);
    state.conf_refresh_all_done.store(0, std::memory_order_relaxed);
    state.conf_refresh_batch_baseline.store(state.pending_confirmations_count.load(std::memory_order_relaxed),
                                            std::memory_order_relaxed);

    const auto stagger_ms = state.settings.confirmations.refresh_stagger_ms;
    app::job_pump::submit([&state, snapshots = std::move(snapshots), stagger_ms]() mutable {
        for (auto& a : snapshots) {
            state.post_ui_callback([&state, a] { submit_account_refresh(state, a); });
            if (stagger_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(stagger_ms));
            }
        }
    });
}

void respond(app::AppState& state, const std::vector<Pick>& picks, bool allow) {
    if (picks.empty()) return;
    const auto now = unix_now();

    if (picks.size() == 1) {
        const Pick& p = picks.front();
        const core::Account* a = state.find_account(p.account_id);
        if (a == nullptr) return;
        submit_single(state, *a, p.conf, allow);
        record_audit(state, *a, p.conf, allow, sda::AuditSource::UserSingle, now);
        erase_item(state, a->id, p.conf.id);
        return;
    }

    // One batch per account, in the order the accounts first appear among the picks.
    struct Batch {
        core::Account account;
        std::vector<sda::Confirmation> items;
    };
    std::vector<Batch> batches;
    for (const Pick& p : picks) {
        const core::Account* a = state.find_account(p.account_id);
        if (a == nullptr) continue;
        auto it = std::find_if(batches.begin(), batches.end(),
                               [&](const Batch& b) { return b.account.id == p.account_id; });
        if (it == batches.end()) {
            batches.push_back({*a, {}});
            it = std::prev(batches.end());
        }
        it->items.push_back(p.conf);
        record_audit(state, *a, p.conf, allow, sda::AuditSource::UserBulk, now);
    }
    for (const Pick& p : picks) erase_item(state, p.account_id, p.conf.id);
    if (batches.empty()) return;

    const int cap = state.settings.confirmations.bulk_size_cap;
    const int stagger_ms = state.settings.confirmations.refresh_stagger_ms;
    app::job_pump::submit([&state, batches = std::move(batches), cap, stagger_ms, allow]() mutable {
        bool first = true;
        for (Batch& b : batches) {
            if (!first && stagger_ms > 0 &&
                !app::job_pump::interruptible_sleep(std::chrono::milliseconds(stagger_ms))) {
                return;
            }
            first = false;
            const std::size_t step = cap > 0 ? static_cast<std::size_t>(cap) : b.items.size();
            for (std::size_t from = 0; from < b.items.size(); from += step) {
                const std::size_t to = std::min(b.items.size(), from + step);
                const std::vector<sda::Confirmation> chunk(b.items.begin() + static_cast<std::ptrdiff_t>(from),
                                                           b.items.begin() + static_cast<std::ptrdiff_t>(to));
                respond_bulk_now(state, b.account, chunk, allow);
            }
        }
    });
}

}  // namespace confirmations_detail

void draw_confirmations(app::AppState& state) {
    confirmations_view::draw(state);
}

void confirmations_trigger_refresh_all(app::AppState& state) {
    refresh_all(state);
}

}  // namespace sam::ui::screens
