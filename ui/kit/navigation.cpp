#include "ui/kit/navigation.hpp"

#include <windows.h>

#include <iterator>

#include "platform/paths.hpp"
#include "ui/icons.hpp"

namespace sam::ui::kit {

namespace {

constexpr SettingsCategoryInfo kCategories[] = {
    {screens::SettingsCategory::General, "General", false},
    {screens::SettingsCategory::Appearance, "Appearance", false},
    {screens::SettingsCategory::AccountsRefresh, "Accounts and refresh", false},
    {screens::SettingsCategory::SteamGuard, "Steam Guard", false},
    {screens::SettingsCategory::SignInLaunch, "Sign-in and launch", false},
    {screens::SettingsCategory::Cs2, "CS2", false},
    {screens::SettingsCategory::Trading, "Trading", false},
    {screens::SettingsCategory::Notifications, "Notifications", false},
    {screens::SettingsCategory::Network, "Network", false},
    {screens::SettingsCategory::PrivacySecurity, "Privacy and security", false},
    {screens::SettingsCategory::VaultsStorage, "Vaults and storage", false},
    {screens::SettingsCategory::About, "About", false},
    {screens::SettingsCategory::Cleaner, "Cleaner", true},
    {screens::SettingsCategory::HwidSpoofer, "HWID spoofer", true},
    {screens::SettingsCategory::Loaders, "CS2 loaders", true},
};

// SettingsCategory values index this list (pending_settings_category, toasts).
static_assert(std::size(kCategories) == static_cast<std::size_t>(screens::kSettingsCategoryCount));

}  // namespace

void go_to(app::AppState& state, app::Screen screen) {
    if (!screen_available(state, screen)) screen = app::Screen::Accounts;
    state.current_screen = screen;
    if (screen == app::Screen::AddAccount) state.selected_account_id.clear();
    if (screen != app::Screen::Accounts && !state.selection_mode) state.selected_account_ids.clear();
}

void open_settings(app::AppState& state, screens::SettingsCategory category) {
    go_to(state, app::Screen::Settings);
    if (settings_category_hidden(state, category)) category = screens::SettingsCategory::General;
    state.pending_settings_category = static_cast<int>(category);
}

void open_account(app::AppState& state, const std::string& account_id) {
    state.selected_account_id = account_id;
    state.current_screen = app::Screen::Accounts;
}

void switch_vault(app::AppState& state, const std::string& vault_id) {
    if (vault_id.empty() || vault_id == platform::active_vault_id()) return;
    // The relaunched instance waits for this one's mutex to free, then opens the pending vault.
    platform::write_pending_vault(vault_id);
    state.relaunch_switch = true;
    if (state.main_hwnd) PostMessageW(state.main_hwnd, WM_CLOSE, 0, 0);
}

bool screen_available(const app::AppState& state, app::Screen screen) {
    switch (screen) {
        case app::Screen::Unlock: return false;
        case app::Screen::Cs2: return state.settings.cs2_gc.enabled;
        default: return true;
    }
}

const char* screen_title(app::Screen screen) {
    switch (screen) {
        case app::Screen::Unlock: return "";
        case app::Screen::Accounts: return "Accounts";
        case app::Screen::Authenticator: return "Authenticator";
        case app::Screen::Confirmations: return "Confirmations";
        case app::Screen::TradeOffers: return "Trade offers";
        case app::Screen::Cs2: return "CS2";
        case app::Screen::AddAccount: return "Add account";
        case app::Screen::Settings: return "Settings";
    }
    return "";
}

facet::Icon screen_icon(app::Screen screen) {
    switch (screen) {
        case app::Screen::Unlock: return facet::Icon("lock");
        case app::Screen::Accounts: return facet::Icon("user");
        case app::Screen::Authenticator: return icons::authenticator;
        case app::Screen::Confirmations: return icons::confirmations;
        case app::Screen::TradeOffers: return icons::trade;
        case app::Screen::Cs2: return icons::crosshair;
        case app::Screen::AddAccount: return facet::Icon("plus");
        case app::Screen::Settings: return facet::Icon("settings");
    }
    return {};
}

std::span<const SettingsCategoryInfo> settings_categories() {
    return kCategories;
}

const char* settings_category_title(screens::SettingsCategory category) {
    for (const SettingsCategoryInfo& c : kCategories) {
        if (c.category == category) return c.title;
    }
    return "";
}

bool settings_category_hidden(const app::AppState& state, screens::SettingsCategory category) {
    for (const SettingsCategoryInfo& c : kCategories) {
        if (c.category == category) return c.unsafe && state.settings.safe_mode;
    }
    return false;
}

}  // namespace sam::ui::kit
