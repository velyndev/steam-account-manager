#ifdef SAM_UI_CAPTURE

#include <vector>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "app/ui_capture_internal.hpp"

namespace sam::app::ui_capture {

namespace {

constexpr int kSettleFrames = 30;

void setup_unlock(AppState& state) {
    select_demo_vault(state);
}

void setup_vault_picker(AppState& state) {
    state.needs_vault_pick = true;
    state.unlocked = false;
    state.current_screen = Screen::Unlock;
}

void setup_accounts(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::Accounts;
    state.selected_account_id = demo_account_id(3);
}

void setup_authenticator(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::Authenticator;
    state.selected_account_id = demo_account_id(0);
}

void setup_confirmations(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::Confirmations;
}

void setup_trade_offers(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::TradeOffers;
}

void setup_cs2(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::Cs2;
    state.selected_account_id = demo_account_id(0);
}

void setup_add_account(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::AddAccount;
}

void setup_settings(AppState& state) {
    open_demo_vault(state);
    state.current_screen = Screen::Settings;
}

}  // namespace

std::vector<CaptureScene> builtin_scenes() {
    return {
        {"unlock", &setup_unlock, kSettleFrames},
        {"vault-picker", &setup_vault_picker, kSettleFrames},
        {"accounts", &setup_accounts, kSettleFrames},
        {"authenticator", &setup_authenticator, kSettleFrames},
        {"confirmations", &setup_confirmations, kSettleFrames},
        {"trade-offers", &setup_trade_offers, kSettleFrames},
        {"cs2", &setup_cs2, kSettleFrames},
        {"add-account", &setup_add_account, kSettleFrames},
        {"settings", &setup_settings, kSettleFrames},
    };
}

}  // namespace sam::app::ui_capture

#endif  // SAM_UI_CAPTURE
