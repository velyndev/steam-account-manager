#include "ui/screens/trade_offers_screen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "app/job_pump.hpp"
#include "core/trade/actions.hpp"
#include "core/trade/inventory.hpp"
#include "core/trade/offers.hpp"
#include "core/trade/trade_url.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/confirmations_screen.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"
#include "ui/screens/trade_offers_detail.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens {

namespace trade_offers_detail {

namespace {
namespace trade = core::trade;
}  // namespace

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const char* cannot_trade_reason(const core::Account& a) {
    if (a.is_nfa) return "Token-only (NFA) account: trades need a full sign-in with password";
    if (a.steam_id_64 == 0) return "Steam ID not known yet: sign in once to resolve it";
    if (a.refresh_token.empty()) return "No saved sign-in: sign in to Steam once from this app";
    return nullptr;
}

bool account_can_trade(const core::Account& a) {
    return cannot_trade_reason(a) == nullptr;
}

std::string economy_image_url(const std::string& icon_url) {
    if (icon_url.empty()) return {};
    return "https://community.cloudflare.steamstatic.com/economy/image/" + icon_url + "/62fx62f";
}

std::string format_day(std::int64_t unix_s) {
    return ui::format_date(unix_s);
}

void apply_refreshed_tokens(app::AppState& state, const core::Account& creds) {
    state.post_ui_callback([&state, aid = creds.id, rt = creds.refresh_token, at = creds.access_token,
                            exp = creds.access_token_expires, ls = creds.steam_login_secure,
                            sid = creds.session_id] {
        auto* a = state.find_account(aid);
        if (!a) return;
        a->refresh_token = rt;
        a->access_token = at;
        a->access_token_expires = exp;
        a->steam_login_secure = ls;
        a->session_id = sid;
        state.vault_dirty = true;
        state.save_vault_if_dirty();
    });
}

bool tokens_changed(const core::Account& before, const core::Account& after) {
    return before.access_token != after.access_token || before.refresh_token != after.refresh_token ||
           before.steam_login_secure != after.steam_login_secure || before.session_id != after.session_id;
}

void recount_incoming(app::AppState& state) {
    int total = 0;
    {
        std::lock_guard lk(g_mtx);
        for (const auto& [aid, st] : g_states) {
            for (const auto& o : st.received) {
                if (o.state == trade::TradeOfferState::Active) ++total;
            }
        }
    }
    state.pending_trade_offers_count.store(total, std::memory_order_relaxed);
}

void erase_offer(app::AppState& state, const std::string& account_id, const std::string& offer_id,
                 bool from_received) {
    {
        std::lock_guard lk(g_mtx);
        auto& st = g_states[account_id];
        auto& vec = from_received ? st.received : st.sent;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&](const trade::TradeOffer& o) { return o.offer_id == offer_id; }),
                  vec.end());
    }
    recount_incoming(state);
}

std::int64_t cooldown_remaining(std::int64_t last, int window) {
    if (window <= 0 || last == 0) return 0;
    const auto since = now_unix() - last;
    return since < window ? window - since : 0;
}

std::int64_t refresh_cooldown_remaining(const app::AppState& state, const std::string& aid) {
    std::int64_t last = 0;
    {
        std::lock_guard lk(g_mtx);
        if (auto it = g_states.find(aid); it != g_states.end()) last = it->second.last_refresh_unix;
    }
    return cooldown_remaining(last, state.settings.trade.per_account_cooldown_seconds);
}

void submit_account_refresh(app::AppState& state, const core::Account& seed) {
    if (!account_can_trade(seed)) return;
    if (refresh_cooldown_remaining(state, seed.id) > 0) return;
    {
        std::lock_guard lk(g_mtx);
        auto& st = g_states[seed.id];
        if (st.refreshing) return;
        st.refreshing = true;
        st.last_error.clear();
    }

    const auto snapshot = seed;
    app::job_pump::submit([&state, snapshot]() mutable {
        core::Account creds = snapshot;
        auto result = trade::get_trade_offers(creds, true);

        if (!result.ok && result.needs_relogin) {
            const auto cd = state.relogin_cooldown_seconds(creds.id);
            if (cd > 0) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "login rate-limited, try again in %lld s", static_cast<long long>(cd));
                result.error = buf;
            } else {
                std::string relogin_err;
                if (state.auto_relogin(creds.id, creds, &relogin_err)) {
                    apply_refreshed_tokens(state, creds);
                    result = trade::get_trade_offers(creds, true);
                } else {
                    result.error = "auto-relogin failed: " + relogin_err;
                }
            }
        }

        if (tokens_changed(snapshot, creds)) apply_refreshed_tokens(state, creds);

        const auto done = now_unix();
        {
            std::lock_guard lk(g_mtx);
            auto& st = g_states[creds.id];
            st.refreshing = false;
            st.last_refresh_unix = done;
            if (result.ok) {
                st.received = std::move(result.received);
                st.sent = std::move(result.sent);
                st.last_error.clear();
            } else {
                st.last_error = result.error;
            }
        }
        recount_incoming(state);
    });
}

void submit_inventory_load(app::AppState& state, const core::Account& seed) {
    if (!account_can_trade(seed)) return;
    {
        std::lock_guard lk(g_mtx);
        auto& st = g_states[seed.id];
        if (st.inv_loading) return;
        st.inv_loading = true;
        st.inv_error.clear();
    }

    const auto snapshot = seed;
    app::job_pump::submit([&state, snapshot]() mutable {
        core::Account creds = snapshot;
        auto res = trade::fetch_inventory(creds, creds.steam_id_64, 730, 2, 2000, 0);

        if (!res.ok && res.needs_relogin) {
            const auto cd = state.relogin_cooldown_seconds(creds.id);
            if (cd <= 0) {
                std::string e;
                if (state.auto_relogin(creds.id, creds, &e)) {
                    apply_refreshed_tokens(state, creds);
                    res = trade::fetch_inventory(creds, creds.steam_id_64, 730, 2, 2000, 0);
                } else {
                    res.error = "auto-relogin failed: " + e;
                }
            }
        }
        if (tokens_changed(snapshot, creds)) apply_refreshed_tokens(state, creds);

        {
            std::lock_guard lk(g_mtx);
            auto& st = g_states[creds.id];
            st.inv_loading = false;
            if (res.ok) {
                st.inventory = std::move(res.items);
                st.inv_error.clear();
                st.inv_loaded_unix = now_unix();
            } else {
                st.inv_error = res.error;
            }
        }
    });
}

void submit_action(app::AppState& state, const core::Account& acc, const trade::TradeOffer& offer, char kind) {
    const std::string aid = acc.id;
    const std::string oid = offer.offer_id;
    const bool from_received = !offer.is_our_offer;
    const std::uint64_t partner = offer.partner_steam_id_64;
    {
        std::lock_guard lk(g_mtx);
        g_in_flight.insert(oid);
    }
    g_offer_errors.erase(oid);

    auto cap_acc = acc;
    app::job_pump::submit([&state, cap_acc, aid, oid, kind, from_received, partner]() mutable {
        auto run = [&](core::Account& a) {
            switch (kind) {
                case 'a': return trade::accept_trade_offer(a, oid, partner);
                case 'd': return trade::decline_trade_offer(a, oid);
                default: return trade::cancel_trade_offer(a, oid);
            }
        };

        auto result = run(cap_acc);
        if (!result.ok && result.needs_relogin) {
            const auto cd = state.relogin_cooldown_seconds(aid);
            if (cd > 0) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "login rate-limited, try again in %lld s", static_cast<long long>(cd));
                result.error = buf;
            } else {
                std::string relogin_err;
                if (state.auto_relogin(aid, cap_acc, &relogin_err)) {
                    apply_refreshed_tokens(state, cap_acc);
                    result = run(cap_acc);
                } else {
                    result.error = "auto-relogin failed: " + relogin_err;
                }
            }
        }

        apply_refreshed_tokens(state, cap_acc);

        {
            std::lock_guard lk(g_mtx);
            g_in_flight.erase(oid);
        }

        const bool ok = result.ok;
        const bool needs_conf = result.needs_confirmation;
        const std::string err = result.error;
        state.post_ui_callback([&state, aid, oid, kind, from_received, ok, needs_conf, err] {
            const char* verb = kind == 'a' ? "accepted" : (kind == 'd' ? "declined" : "canceled");
            if (ok) {
                erase_offer(state, aid, oid, from_received);
                if (kind == 'a' && needs_conf) {
                    kit::show_toast(state, "Trade accepted: confirm it in Confirmations",
                                    {.tone = widgets::ToastTone::Warning, .id = "trade-" + oid, .account_id = aid});
                    confirmations_trigger_refresh_all(state);
                } else {
                    kit::show_toast(state, std::string("Trade ") + verb,
                                    {.tone = widgets::ToastTone::Success, .id = "trade-" + oid, .account_id = aid});
                }
            } else {
                g_offer_errors[oid] = err;
                const char* what = kind == 'a' ? "Accept" : (kind == 'd' ? "Decline" : "Cancel");
                kit::show_toast(state, std::string(what) + " failed. " + trade_offers_ui::human_error(err),
                                {.tone = widgets::ToastTone::Destructive, .id = "trade-" + oid, .account_id = aid});
            }
        });
    });
}

void refresh_all(app::AppState& state, const std::string& scope) {
    std::vector<core::Account> snaps;
    for (auto& a : state.vault.accounts) {
        if (!scope.empty() && a.id != scope) continue;
        if (!account_can_trade(a)) continue;
        if (refresh_cooldown_remaining(state, a.id) > 0) continue;
        snaps.push_back(a);
    }
    if (snaps.empty()) return;

    const auto stagger = state.settings.trade.refresh_stagger_ms;
    app::job_pump::submit([&state, snaps = std::move(snaps), stagger]() mutable {
        for (auto& a : snaps) {
            state.post_ui_callback([&state, a] { submit_account_refresh(state, a); });
            if (stagger > 0) std::this_thread::sleep_for(std::chrono::milliseconds(stagger));
        }
    });
}

}  // namespace trade_offers_detail

namespace trade_offers_ui {

namespace {

namespace trade = core::trade;
using namespace trade_offers_detail;

// --- Small text helpers ----------------------------------------------------------------------

void note(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true});
}

std::string seconds_text(std::int64_t s) {
    return kit::format_count(s) + " s";
}

std::string plural(int n, const char* one, const char* many) {
    return kit::format_count(n) + " " + (n == 1 ? one : many);
}

bool in_scope(const core::Account& a) {
    return g_ui.scope.empty() || a.id == g_ui.scope;
}

// --- Data of one frame -----------------------------------------------------------------------

struct OfferRow {
    const core::Account* account = nullptr;
    trade::TradeOffer offer;
    bool busy = false;
};

bool shown_state(trade::TradeOfferState s) {
    return s == trade::TradeOfferState::Active || s == trade::TradeOfferState::CreatedNeedsConfirmation ||
           s == trade::TradeOfferState::InEscrow;
}

// An incoming offer that takes items and gives nothing back: the dangerous case to flag.
bool gives_away(const trade::TradeOffer& o) {
    return !o.is_our_offer && !o.items_to_give.empty() && o.items_to_receive.empty();
}

// What the accounts in scope look like this frame, read once under the lock. `waiting` is the
// one count of the screen: incoming offers waiting for the user's decision, the same set the
// sidebar badge counts (pending_trade_offers_count).
struct ScopeFacts {
    int tradable = 0;
    int refreshing = 0;
    int eligible = 0;  // tradable, not refreshing, off cooldown
    std::int64_t min_cooldown = 0;
    std::int64_t last_checked = 0;
    int checked = 0;  // accounts checked at least once
    int waiting = 0;
    int waiting_gifts = 0;
    int waiting_giveaways = 0;
    struct Failure {
        const core::Account* account;
        std::string error;
    };
    std::vector<Failure> failures;
    std::vector<const core::Account*> cannot_trade;
};

ScopeFacts read_facts(const app::AppState& state) {
    ScopeFacts f;
    const int window = state.settings.trade.per_account_cooldown_seconds;
    std::lock_guard lk(g_mtx);
    for (const auto& a : state.vault.accounts) {
        if (!in_scope(a)) continue;
        if (!account_can_trade(a)) {
            f.cannot_trade.push_back(&a);
            continue;
        }
        ++f.tradable;
        const auto it = g_states.find(a.id);
        if (it == g_states.end()) {
            ++f.eligible;
            continue;
        }
        const AccountTradeState& st = it->second;
        if (st.refreshing) {
            ++f.refreshing;
        } else if (const auto cd = cooldown_remaining(st.last_refresh_unix, window); cd > 0) {
            f.min_cooldown = f.min_cooldown == 0 ? cd : std::min(f.min_cooldown, cd);
        } else {
            ++f.eligible;
        }
        if (st.last_refresh_unix > 0) {
            ++f.checked;
            f.last_checked = std::max(f.last_checked, st.last_refresh_unix);
        }
        if (!st.last_error.empty() && !st.refreshing) f.failures.push_back({&a, st.last_error});
        for (const auto& o : st.received) {
            if (o.state != trade::TradeOfferState::Active) continue;
            ++f.waiting;
            if (o.items_to_give.empty()) ++f.waiting_gifts;
            else ++f.waiting_giveaways;
        }
    }
    return f;
}

std::vector<OfferRow> collect_offers(const app::AppState& state, bool incoming) {
    std::vector<OfferRow> rows;
    std::lock_guard lk(g_mtx);
    for (const auto& a : state.vault.accounts) {
        if (!in_scope(a) || !account_can_trade(a)) continue;
        const auto it = g_states.find(a.id);
        if (it == g_states.end()) continue;
        for (const auto& o : incoming ? it->second.received : it->second.sent) {
            if (!shown_state(o.state)) continue;
            rows.push_back({&a, o, g_in_flight.count(o.offer_id) != 0});
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const OfferRow& x, const OfferRow& y) { return x.offer.created_unix > y.offer.created_unix; });
    return rows;
}

// --- Actions ---------------------------------------------------------------------------------

void accept(app::AppState& state, const OfferRow& r) {
    if (gives_away(r.offer)) {
        g_ui.confirm_offer_id = r.offer.offer_id;
        g_ui.confirm_account_id = r.account->id;
        g_ui.confirm_offer_open = true;
        return;
    }
    submit_action(state, *r.account, r.offer, 'a');
}

// The buttons the offer's state allows. In a table row each button is one item of the row's
// cell; in the detail pane they sit on one line.
void offer_actions(app::AppState& state, const OfferRow& r, kit::RowCell* cell) {
    const trade::TradeOffer& o = r.offer;
    const facet::ControlSize size = cell != nullptr ? facet::ControlSize::Small : facet::ControlSize::Regular;
    const auto next = [&] {
        if (cell != nullptr) cell->next();
        else ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.sm));
    };
    ImGui::BeginDisabled(r.busy);
    if (!o.is_our_offer && o.state == trade::TradeOfferState::Active) {
        if (facet::Button("Accept", {.icon = facet::Icon("check"), .size = size, .loading = r.busy})) accept(state, r);
        next();
        if (facet::Button("Decline", {.variant = facet::ButtonVariant::Plain, .size = size})) {
            submit_action(state, *r.account, o, 'd');
        }
    } else if (o.is_our_offer && (o.state == trade::TradeOfferState::Active ||
                                  o.state == trade::TradeOfferState::CreatedNeedsConfirmation)) {
        if (o.state == trade::TradeOfferState::CreatedNeedsConfirmation && r.account->sda.has_value()) {
            if (facet::Button("Confirm", {.icon = icons::confirmations, .size = size, .loading = r.busy})) {
                submit_confirm_sent(state, *r.account, o.offer_id);
            }
            facet::ItemTooltip("Approve the mobile confirmation with this account's authenticator");
            next();
        }
        if (kit::quiet_destructive_button("Cancel offer", facet::Icon("close"), size)) {
            g_ui.confirm_cancel_id = o.offer_id;
            g_ui.confirm_account_id = r.account->id;
            g_ui.confirm_cancel_open = true;
        }
    }
    ImGui::EndDisabled();
}

void offer_status(const OfferRow& r) {
    const trade::TradeOffer& o = r.offer;
    if (r.busy) {
        facet::Spinner("Working", {.size = facet::ControlSize::Small});
        return;
    }
    if (auto it = g_offer_errors.find(o.offer_id); it != g_offer_errors.end()) {
        facet::StatusPill("Action failed###status", {.tone = facet::Tone::Destructive, .icon = facet::Icon("error")});
        facet::ItemTooltip("%s", human_error(it->second).c_str());
        return;
    }
    switch (o.state) {
        case trade::TradeOfferState::Active:
            if (o.is_our_offer) {
                facet::StatusPill("Waiting for partner###status", {.tone = facet::Tone::Neutral});
            } else {
                facet::StatusPill("Waiting for you###status", {.tone = facet::Tone::Accent});
            }
            break;
        case trade::TradeOfferState::CreatedNeedsConfirmation:
            facet::StatusPill("Needs mobile confirmation###status",
                              {.tone = facet::Tone::Warning, .icon = icons::confirmations});
            break;
        case trade::TradeOfferState::InEscrow: {
            const std::string text = "In escrow until " + format_day(o.escrow_end_unix) + "###status";
            facet::StatusPill(text.c_str(), {.tone = facet::Tone::Warning, .icon = facet::Icon("clock")});
            break;
        }
        default:
            facet::StatusPill((std::string(trade::to_string(o.state)) + "###status").c_str());
            break;
    }
}

std::string age_text(const trade::TradeOffer& o) {
    if (o.created_unix <= 0) return {};
    return std::string(o.is_our_offer ? "Sent " : "Received ") + ui::format_relative(o.created_unix);
}

// --- Account status beside the tabs ----------------------------------------------------------

// Why a button with a popover instead of alerts above the list: an account that failed to load
// must not push the offers down, so the list stays where the eye left it.
void draw_failures(app::AppState& state, const ScopeFacts& facts) {
    if (facts.failures.empty()) return;
    const std::string label = plural(static_cast<int>(facts.failures.size()), "account", "accounts") +
                              " could not be checked###failures";
    if (facet::Button(label.c_str(), {.variant = facet::ButtonVariant::Plain, .icon = facet::Icon("error"),
                                      .size = facet::ControlSize::Small})) {
        g_ui.failures_open = !g_ui.failures_open;
    }
    if (!facet::BeginPopover("##failures-popover", &g_ui.failures_open, {.width = 380.0F})) return;
    const facet::Theme& t = facet::T();
    facet::Text(facet::TextStyle::Headline, "Accounts that could not be checked");
    for (const auto& f : facts.failures) {
        ImGui::PushID(f.account->id.c_str());
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
        kit::account_row(state, *f.account, {.layout = kit::AccountRowOptions::Layout::OneLine});
        facet::StyledText(human_error(f.error).c_str(),
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        facet::ItemTooltip("%s", f.error.c_str());
        if (error_needs_sign_in(f.error)) {
            if (facet::Button("Show account", {.size = facet::ControlSize::Small})) {
                g_ui.failures_open = false;
                kit::open_account(state, f.account->id);
            }
        } else {
            const auto cd = refresh_cooldown_remaining(state, f.account->id);
            if (cd > 0) {
                const std::string wait = "You can try again in " + seconds_text(cd) + ".";
                note(wait.c_str());
            } else if (facet::Button("Try again", {.icon = facet::Icon("refresh"), .size = facet::ControlSize::Small})) {
                submit_account_refresh(state, *f.account);
            }
        }
        ImGui::PopID();
    }
    facet::EndPopover();
}

void draw_cannot_trade(app::AppState& state, const ScopeFacts& facts) {
    if (facts.cannot_trade.empty()) return;
    const std::string label = plural(static_cast<int>(facts.cannot_trade.size()), "account", "accounts") +
                              " can't trade###cant-trade";
    if (facet::Button(label.c_str(), {.variant = facet::ButtonVariant::Plain, .icon = facet::Icon("info"),
                                      .size = facet::ControlSize::Small})) {
        g_ui.cant_trade_open = !g_ui.cant_trade_open;
    }
    if (!facet::BeginPopover("##cant-trade-popover", &g_ui.cant_trade_open, {.width = 360.0F})) return;
    facet::Text(facet::TextStyle::Headline, "Accounts that can't trade");
    const facet::Theme& t = facet::T();
    const int limit = 12;
    int n = 0;
    for (const core::Account* a : facts.cannot_trade) {
        if (n++ == limit) {
            const std::string rest = "and " + plural(static_cast<int>(facts.cannot_trade.size()) - limit,
                                                     "more account", "more accounts");
            note(rest.c_str());
            break;
        }
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
        kit::account_row(state, *a, {.layout = kit::AccountRowOptions::Layout::OneLine});
        facet::StyledText(cannot_trade_reason(*a), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    }
    facet::EndPopover();
}

// --- The offer list --------------------------------------------------------------------------

ImGuiID row_storage_id(ImGuiSelectionBasicStorage* self, int index) {
    const auto* rows = static_cast<const std::vector<OfferRow>*>(self->UserData);
    if (rows == nullptr || index < 0 || index >= static_cast<int>(rows->size())) return 0;
    return ImHashStr((*rows)[static_cast<std::size_t>(index)].offer.offer_id.c_str());
}

void select_offer(const OfferRow& r) {
    g_ui.selection.Clear();
    g_ui.selection.SetItemSelected(ImHashStr(r.offer.offer_id.c_str()), true);
    g_ui.selected_offer = r.offer.offer_id;
    g_ui.detail_open = true;
}

// Two lines as one item of a row cell: a name in Body over a line in Footnote labelSecondary.
void two_lines(const char* first, const char* second) {
    facet::BeginStack("##lines", {.gap = facet::Gap::None});
    facet::TruncatedText(first, {.style = facet::TextStyle::Body});
    if (second != nullptr && second[0] != '\0') {
        facet::TruncatedText(second, {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    facet::EndStack();
}

constexpr float kThumb = 32.0F;

float offer_row_height() {
    return kThumb + 2.0F * facet::T().spacing.md;
}

// Why a data table: offers are rows with several facts side by side, and the table lets the
// action buttons in a cell take their own clicks while the row itself selects the offer. The
// table is as tall as its rows, so a short list does not sit in an empty frame.
void draw_offer_table(app::AppState& state, std::vector<OfferRow>& rows, bool incoming) {
    const facet::Theme& t = facet::T();
    const bool with_account = g_ui.scope.empty();
    // With the detail open, its pane shows the status and the actions, so the list keeps the
    // room for both sides of the trade.
    const bool with_actions = !g_ui.detail_open;
    const float row_h = offer_row_height();

    std::vector<facet::DataTableColumn> cols;
    cols.push_back({.label = with_account ? "Offer" : "Partner", .width = 180.0F});
    cols.push_back({.label = "You give"});
    cols.push_back({.label = "You receive"});
    if (with_actions) {
        cols.push_back({.label = "Status", .width = 176.0F});
        cols.push_back({.label = "##actions", .width = incoming ? 168.0F : 196.0F});
    }

    const float needed = static_cast<float>(rows.size()) * row_h + t.list.rowHeight + t.spacing.md;
    const float avail = ImGui::GetContentRegionAvail().y / facet::Scale();
    const float height = std::min(needed, std::max(avail, row_h + t.list.rowHeight));

    g_ui.selection.UserData = &rows;
    g_ui.selection.AdapterIndexToStorageId = &row_storage_id;
    if (!facet::BeginDataTable("##offers", cols, static_cast<int>(rows.size()), &g_ui.selection,
                               {.singleSelect = true, .rowHeight = row_h, .height = height})) {
        return;
    }
    facet::RowRange range;
    while (facet::NextVisibleRows(&range)) {
        for (int i = range.first; i < range.last; ++i) {
            OfferRow& r = rows[static_cast<std::size_t>(i)];
            ImGui::PushID(r.offer.offer_id.c_str());
            if (facet::DataTableRow(i)) select_offer(r);

            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                if (with_account) {
                    const std::string name = kit::display_name(state, *r.account);
                    const std::string with = "with " + partner_name(state, r.offer);
                    two_lines(name.c_str(), with.c_str());
                } else {
                    const std::string name = partner_name(state, r.offer);
                    two_lines(name.c_str(), age_text(r.offer).c_str());
                }
            }

            for (int side = 0; side < 2; ++side) {
                ImGui::TableNextColumn();
                kit::RowCell cell;
                const auto& items = side == 0 ? r.offer.items_to_give : r.offer.items_to_receive;
                const float width = ImGui::GetContentRegionAvail().x / facet::Scale();
                if (side == 1 && gives_away(r.offer)) {
                    facet::Badge("Nothing", {.tone = facet::Tone::Destructive, .icon = facet::Icon("warning")});
                    facet::ItemTooltip("You give items and receive nothing. Check this offer before accepting.");
                } else if (items.empty()) {
                    note("Nothing");
                } else if (item_strip(side == 0 ? "give" : "receive", items, width, kThumb)) {
                    select_offer(r);
                }
            }

            if (with_actions) {
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    facet::BeginStack("##status", {.gap = facet::Gap::None});
                    offer_status(r);
                    if (with_account) note(age_text(r.offer).c_str());
                    facet::EndStack();
                }
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    offer_actions(state, r, &cell);
                }
            }
            ImGui::PopID();
        }
    }
    facet::EndDataTable();

    // The table writes the selection; the detail follows it.
    std::string picked;
    for (const auto& r : rows) {
        if (g_ui.selection.Contains(ImHashStr(r.offer.offer_id.c_str()))) {
            picked = r.offer.offer_id;
            break;
        }
    }
    if (!picked.empty() && picked != g_ui.selected_offer) {
        g_ui.selected_offer = picked;
        g_ui.detail_open = true;
    }
}

// Why the warnings come before the actions: the reason to hesitate has to be read before the
// button that cannot be taken back.
void draw_offer_detail(app::AppState& state, const OfferRow& r) {
    const facet::Theme& t = facet::T();
    const trade::TradeOffer& o = r.offer;

    const std::string title = (o.is_our_offer ? "Offer to " : "Offer from ") + partner_name(state, o);
    facet::StyledText(title.c_str(), {.style = facet::TextStyle::Title3, .wrap = true});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
    kit::account_row(state, *r.account, {.layout = kit::AccountRowOptions::Layout::OneLine});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
    {
        kit::RowCell cell;
        offer_status(r);
        cell.next();
        note(age_text(o).c_str());
    }
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.md)));

    if (gives_away(o)) {
        facet::InlineAlert("You give items and receive nothing", nullptr,
                           "Accepting moves these items to the partner for good. Accept only if you expect this "
                           "trade.",
                           {.tone = facet::Tone::Destructive});
    }
    if (o.escrow_end_unix > now_unix()) {
        const std::string msg = "Steam holds the items until " + format_day(o.escrow_end_unix) +
                                ". They move when the hold ends, unless the trade is canceled.";
        facet::InlineAlert("Trade hold", nullptr, msg.c_str(), {.tone = facet::Tone::Warning});
    }
    if (auto it = g_offer_errors.find(o.offer_id); it != g_offer_errors.end()) {
        facet::InlineAlert("The last action failed", nullptr, human_error(it->second).c_str(),
                           {.tone = facet::Tone::Destructive});
        facet::ItemTooltip("%s", it->second.c_str());
    }

    offer_actions(state, r, nullptr);
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));

    if (!o.message.empty()) {
        facet::SectionHeader("Message");
        facet::StyledText(o.message.c_str(), {.wrap = true});
    }

    for (int side = 0; side < 2; ++side) {
        const auto& items = side == 0 ? o.items_to_give : o.items_to_receive;
        const std::string head = std::string(side == 0 ? "You give" : "You receive") + " (" +
                                 kit::format_count(static_cast<std::int64_t>(items.size())) + ")";
        facet::SectionHeader(head.c_str());
        if (items.empty()) {
            note("Nothing");
            continue;
        }
        ImGui::PushID(side);
        for (std::size_t i = 0; i < items.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            item_row("##item", items[i]);
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    facet::SectionHeader("Details");
    facet::BeginKeyValueList("##details", {.copyable = true});
    facet::KeyValue("Offer ID", o.offer_id.c_str());
    const std::string partner_id = std::to_string(o.partner_steam_id_64);
    facet::KeyValue("Partner Steam ID", partner_id.c_str());
    if (o.created_unix > 0) facet::KeyValue("Created", ui::format_date(o.created_unix).c_str());
    if (o.expiration_unix > 0) facet::KeyValue("Expires", ui::format_date(o.expiration_unix).c_str());
    facet::EndKeyValueList();
    const std::string profile = "https://steamcommunity.com/profiles/" + partner_id;
    facet::Link("Open the partner's Steam profile", {.url = profile.c_str()});
}

}  // namespace

// Why an inspector pane: the list stays the focal point and in view while one offer is read in
// full beside it; in a narrow window the pane floats over the list as a drawer.
void draw_offers_tab(app::AppState& state, bool incoming) {
    const facet::Theme& t = facet::T();
    const ScopeFacts facts = read_facts(state);
    std::vector<OfferRow> rows = collect_offers(state, incoming);

    const OfferRow* selected = nullptr;
    for (const auto& r : rows) {
        if (r.offer.offer_id == g_ui.selected_offer) selected = &r;
    }
    if (selected == nullptr && g_ui.detail_open) g_ui.detail_open = false;
    if (!g_ui.detail_open) {
        g_ui.selection.Clear();
        g_ui.selected_offer.clear();
    }

    // The line above the list holds what needs attention and the bulk action of the tab; the
    // status itself is said once, in the page header. It sits above the pane, so the pane and
    // the list start on one line.
    const bool bar = !facts.failures.empty() || !facts.cannot_trade.empty() || (incoming && facts.waiting > 0);
    if (bar) {
        facet::BeginStack("##list-bar", {.orientation = facet::Orientation::Horizontal,
                                         .align = facet::LayoutAlign::Start,
                                         .width = facet::AvailableWidthUnits()});
        if (!facts.failures.empty()) {
            ImGui::BeginGroup();
            draw_failures(state, facts);
            ImGui::EndGroup();
        }
        if (!facts.cannot_trade.empty()) {
            ImGui::BeginGroup();
            draw_cannot_trade(state, facts);
            ImGui::EndGroup();
        }
        facet::StackSpacer();
        if (incoming && facts.waiting > 0) {
            const std::string label =
                facts.waiting == 1 ? std::string("Accept 1 offer") : "Accept all " + kit::format_count(facts.waiting);
            if (facet::Button((label + "###accept-all").c_str(),
                              {.icon = facet::Icon("check"), .size = facet::ControlSize::Small})) {
                g_bulk_accept_count = facts.waiting;
                g_bulk_accept_gifts = facts.waiting_gifts;
                g_bulk_accept_giveaways = facts.waiting_giveaways;
                g_confirm_accept_open = true;
            }
        }
        facet::EndStack();
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
    }

    facet::BeginInspectorPane("Offer details###offer-detail", &g_ui.detail_open,
                              {.paneWidth = 340.0F, .minPaneWidth = 280.0F});

    if (!rows.empty()) {
        draw_offer_table(state, rows, incoming);
    } else if (facts.tradable == 0) {
        if (kit::empty_state("No account here can trade",
                             {.message = "Trades need an account with a full sign-in (login and password) and a "
                                         "known Steam ID. Token-only accounts can't trade.",
                              .icon = icons::trade,
                              .action_label = "Add account",
                              .action_icon = facet::Icon("plus"),
                              .height = 260.0F})) {
            kit::go_to(state, app::Screen::AddAccount);
        }
    } else if (facts.checked == 0) {
        if (facts.refreshing > 0) {
            if (facet::BeginDataStateView("##first-load", facet::DataState::Loading, 0, {.skeletonRows = 4})) {
                facet::EndDataStateView();
            }
        } else {
            kit::empty_state("Offers have not been checked yet",
                             {.message = "Steam limits how often trade offers can be read, so this app checks them "
                                         "only when you choose Refresh all accounts.",
                              .icon = icons::trade,
                              .height = 260.0F});
        }
    } else {
        kit::empty_state(incoming ? "No incoming offers" : "No sent offers waiting",
                         {.message = incoming ? "Nothing is waiting for your decision."
                                              : "Offers you send stay here until the partner answers.",
                          .icon = icons::trade,
                          .height = 260.0F});
    }

    if (facet::InspectorPaneNext() && selected != nullptr) {
        if (facet::BeginScrollArea("##detail-scroll")) {
            draw_offer_detail(state, *selected);
            facet::EndScrollArea();
        }
    }
    facet::EndInspectorPane();
}

}  // namespace trade_offers_ui

namespace {

using namespace trade_offers_detail;
using namespace trade_offers_ui;

struct HeaderFacts {
    int tradable = 0;
    int refreshing = 0;
    int eligible = 0;
    std::int64_t min_cooldown = 0;
    std::int64_t last_checked = 0;
    int waiting = 0;
};

HeaderFacts header_facts(const app::AppState& state) {
    HeaderFacts f;
    const int window = state.settings.trade.per_account_cooldown_seconds;
    std::lock_guard lk(g_mtx);
    for (const auto& a : state.vault.accounts) {
        if (!g_ui.scope.empty() && a.id != g_ui.scope) continue;
        if (!account_can_trade(a)) continue;
        ++f.tradable;
        const auto it = g_states.find(a.id);
        if (it == g_states.end()) {
            ++f.eligible;
            continue;
        }
        if (it->second.refreshing) {
            ++f.refreshing;
        } else if (const auto cd = cooldown_remaining(it->second.last_refresh_unix, window); cd > 0) {
            f.min_cooldown = f.min_cooldown == 0 ? cd : std::min(f.min_cooldown, cd);
        } else {
            ++f.eligible;
        }
        f.last_checked = std::max(f.last_checked, it->second.last_refresh_unix);
        for (const auto& o : it->second.received) {
            if (o.state == core::trade::TradeOfferState::Active) ++f.waiting;
        }
    }
    return f;
}

std::string count_text(int n, const char* one, const char* many) {
    return kit::format_count(n) + " " + (n == 1 ? one : many);
}

// Accept all, the guarded single accept and the cancel of a sent offer, drawn at page level so
// they stack over everything.
void draw_confirms(app::AppState& state) {
    {
        const int n = g_bulk_accept_count;
        std::string msg;
        if (g_bulk_accept_giveaways > 0) {
            msg = count_text(g_bulk_accept_gifts, "only gives you items", "only give you items") + ". " +
                  count_text(g_bulk_accept_giveaways, "gives items of yours away", "give items of yours away") +
                  ": check those first, accepting cannot be undone.";
        } else {
            msg = n == 1 ? std::string("It only gives you items.") : "All " + kit::format_count(n) + " only give you items.";
        }
        msg += " Accepts run one after another to respect Steam's limits.";
        const std::string title = "Accept " + count_text(n, "incoming offer", "incoming offers") + "?";
        const std::string confirm = "Accept " + count_text(n, "offer", "offers");
        const facet::DialogResult r =
            g_bulk_accept_giveaways > 0
                ? kit::destructive_dialog(title.c_str(), &g_confirm_accept_open, msg.c_str(), confirm.c_str())
                : kit::confirm_dialog(title.c_str(), &g_confirm_accept_open, msg.c_str(), confirm.c_str());
        if (r == facet::DialogResult::Confirmed) bulk_accept_all(state, g_ui.scope);
    }

    // Finds the offer a dialog was opened for; the dialog acts only if it still exists.
    const auto find_offer = [](const std::string& aid, const std::string& oid, bool received,
                               core::trade::TradeOffer* out) {
        std::lock_guard lk(g_mtx);
        const auto it = g_states.find(aid);
        if (it == g_states.end()) return false;
        for (const auto& o : received ? it->second.received : it->second.sent) {
            if (o.offer_id == oid) {
                *out = o;
                return true;
            }
        }
        return false;
    };

    if (kit::destructive_dialog("Accept an offer that gives items away?", &g_ui.confirm_offer_open,
                                "You give items and receive nothing in return. The items leave this account for good.",
                                "Accept and give items") == facet::DialogResult::Confirmed) {
        core::trade::TradeOffer offer;
        const core::Account* acc = state.find_account(g_ui.confirm_account_id);
        if (acc != nullptr && find_offer(g_ui.confirm_account_id, g_ui.confirm_offer_id, true, &offer)) {
            submit_action(state, *acc, offer, 'a');
        }
    }
    // Facet's dialog directly, because its own Cancel would sit next to "Cancel offer": the
    // dismiss button says "Keep offer" instead.
    if (facet::ConfirmDialog("Cancel this offer?", &g_ui.confirm_cancel_open,
                             "The partner can no longer accept it. The items stay in this account.",
                             {.confirmLabel = "Cancel offer", .cancelLabel = "Keep offer", .destructive = true}) ==
        facet::DialogResult::Confirmed) {
        core::trade::TradeOffer offer;
        const core::Account* acc = state.find_account(g_ui.confirm_account_id);
        if (acc != nullptr && find_offer(g_ui.confirm_account_id, g_ui.confirm_cancel_id, false, &offer)) {
            submit_action(state, *acc, offer, 'c');
        }
    }
}

// Closes everything transient of the screen: the send drawer, the detail pane, the popovers and
// the dialogs (brief rule 1). A send that is running goes on; its toasts report the result.
void reset_trade_offers_ui(app::AppState&) {
    g_ui.detail_open = false;
    g_ui.selected_offer.clear();
    g_ui.selection.Clear();
    g_ui.confirm_offer_open = false;
    g_ui.confirm_cancel_open = false;
    g_ui.cant_trade_open = false;
    g_ui.failures_open = false;
    g_confirm_accept_open = false;
    g_send.open = false;
    g_send.delete_open = false;
    g_send.renaming = false;
    g_send.menu_open = false;
}

}  // namespace

SAM_KIT_TRANSIENT_RESET(trade_offers, &reset_trade_offers_ui)
SAM_KIT_SCREEN_LEAVE(trade_offers, app::Screen::TradeOffers, &reset_trade_offers_ui)

// Why tabs under one header: incoming, sent and the history are three lists of the same
// objects, and the header keeps the account scope, the one status line and the refresh that
// apply to all of them.
void draw_trade_offers(app::AppState& state) {
    kit::PageScope page("trade-offers", {.scroll = false});
    const HeaderFacts hf = header_facts(state);

    if (!g_ui.scope.empty() && state.find_account(g_ui.scope) == nullptr) g_ui.scope.clear();

    std::string context;
    if (hf.tradable == 0) {
        context = "No account can trade yet";
    } else if (hf.last_checked == 0) {
        context = hf.refreshing > 0 ? "Checking offers for the first time" : "Not checked yet";
    } else {
        context = hf.waiting > 0 ? count_text(hf.waiting, "offer waits for your decision", "offers wait for your decision")
                                 : std::string("Nothing waits for your decision");
        context += ", checked " + ui::format_relative(hf.last_checked);
    }
    if (hf.refreshing > 0 && hf.last_checked != 0) {
        context += ", checking " + count_text(hf.refreshing, "account", "accounts");
    }

    {
        kit::PageHeaderScope header("Trade offers", {.context = context.c_str()});
        if (kit::account_picker("##scope", state, &g_ui.scope,
                                {.filter = &kit::has_steam_id, .placeholder = "All accounts", .width = 200.0F})) {
            g_ui.detail_open = false;
        }
        if (!g_ui.scope.empty()) {
            if (facet::IconButton("Show all accounts", facet::Icon("close"))) {
                g_ui.scope.clear();
                g_ui.detail_open = false;
            }
        }

        std::string refresh_label =
            std::string(g_ui.scope.empty() ? "Refresh all accounts" : "Refresh this account") + "###refresh";
        bool refresh_off = hf.tradable == 0;
        if (hf.refreshing == 0 && hf.eligible == 0 && hf.min_cooldown > 0) {
            refresh_label = "Refresh in " + kit::format_count(hf.min_cooldown) + " s###refresh";
            refresh_off = true;
        }
        ImGui::BeginDisabled(refresh_off);
        if (facet::Button(refresh_label.c_str(), {.icon = facet::Icon("refresh"), .loading = hf.refreshing > 0})) {
            refresh_all(state, g_ui.scope);
        }
        ImGui::EndDisabled();
        if (hf.min_cooldown > 0 || hf.refreshing > 0) {
            facet::ItemTooltip("Steam limits how often offers can be read: each account can be checked again %d s "
                               "after its last check.",
                               state.settings.trade.per_account_cooldown_seconds);
        }

        ImGui::BeginDisabled(hf.tradable == 0);
        if (facet::Button("Send items", {.variant = facet::ButtonVariant::Primary})) {
            open_send_flow(state, g_ui.scope);
        }
        ImGui::EndDisabled();
    }

    const std::string waiting_badge = hf.waiting > 0 ? kit::format_count(hf.waiting) : std::string();
    const facet::ChoiceItem tabs[] = {
        {.label = "Incoming", .detail = waiting_badge.empty() ? nullptr : waiting_badge.c_str()},
        {.label = "Sent"},
        {.label = "History"},
    };
    if (facet::TabBar("##tabs", &g_ui.tab, tabs)) {
        g_ui.detail_open = false;
    }
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));

    switch (g_ui.tab) {
        case kIncoming: draw_offers_tab(state, true); break;
        case kSent: draw_offers_tab(state, false); break;
        default: draw_history_tab(state); break;
    }

    draw_confirms(state);
    draw_send_flow(state);
}

}  // namespace sam::ui::screens
