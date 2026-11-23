#include <string>

#include "core/trade/trade_url.hpp"
#include "ui/kit/account.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"

namespace sam::ui::screens::trade_offers_ui {

namespace {

bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

bool error_needs_sign_in(const std::string& error) {
    return has(error, "session expired") || has(error, "auto-relogin failed") || has(error, "no access token");
}

// The raw texts come from core/trade (actions, inventory, offers), from Steam's strError codes in
// parentheses, and from the auto sign-in. Each becomes what happened and what to do.
std::string human_error(const std::string& error) {
    if (error.empty()) return {};
    if (has(error, "429") || has(error, "rate limited") || has(error, "rate-limited")) {
        return "Steam is limiting requests from this account. Wait a minute, then try again.";
    }
    if (error_needs_sign_in(error)) {
        return "The saved sign-in of this account no longer works. Open the account and sign in again.";
    }
    if (has(error, "private") || has(error, "inventory unavailable")) {
        return "Steam did not return the inventory. It may be private: make it public in the account's Steam "
               "privacy settings, or try again later.";
    }
    if (has(error, "invalid trade URL")) {
        return "The trade link is not valid. Copy it again from the partner's Steam trade settings.";
    }
    if (has(error, "authenticator")) {
        return "This account can't trade from here: trades need a full sign-in and Steam Guard in this app.";
    }
    if (has(error, "(15)")) {
        return "Steam refused the offer. One side may have a trade hold or a trade ban, or the partner's "
               "inventory is full.";
    }
    if (has(error, "(26)")) {
        return "Some items are no longer in the inventory. Load it again and choose the items anew.";
    }
    if (has(error, "(16)")) {
        return "Steam did not answer in time. Check the Sent tab before sending again: the offer may have gone out.";
    }
    if (has(error, "(25)")) {
        return "The partner's inventory is full.";
    }
    if (has(error, "confirmation not found")) {
        return "Steam has not shown the mobile confirmation yet. Approve it on the Confirmations screen in a moment.";
    }
    if (has(error, "no tradable items")) {
        return "This account has no item that can be traded right now.";
    }
    if (has(error, "HTTP 5") || has(error, "http 5") || has(error, "unexpected response")) {
        return "Steam is not answering properly right now. Try again in a few minutes.";
    }
    return "Steam did not accept the request. Try again; if it keeps failing, sign in to the account again.";
}

const core::Account* partner_account(const app::AppState& state, const core::trade::TradeOffer& offer) {
    const std::uint64_t sid = offer.partner_steam_id_64 != 0
                                  ? offer.partner_steam_id_64
                                  : core::trade::account_id_to_steam_id_64(offer.partner_account_id);
    if (sid == 0) return nullptr;
    for (const auto& a : state.vault.accounts) {
        if (a.steam_id_64 == sid) return &a;
    }
    return nullptr;
}

std::string partner_name(const app::AppState& state, const core::trade::TradeOffer& offer) {
    if (const core::Account* a = partner_account(state, offer)) return kit::display_name(state, *a) + " (yours)";
    return "Steam user " + std::to_string(offer.partner_account_id);
}

}  // namespace sam::ui::screens::trade_offers_ui
