#pragma once

#include "app/app_state.hpp"

namespace sam::ui::screens {

// The Settings categories, in display order. kit::settings_categories() (ui/kit/navigation.cpp)
// lists them in this order with their titles, and AppState::pending_settings_category holds one
// of these values as int to open a category on the next frame.
enum class SettingsCategory {
    General = 0,
    Appearance,
    AccountsRefresh,
    SteamGuard,
    SignInLaunch,
    Cs2,
    Trading,
    Notifications,
    Network,
    PrivacySecurity,
    VaultsStorage,
    About,
    // Safe mode hides these three, so they sit last.
    Cleaner,
    HwidSpoofer,
    Loaders,

    // Aliases used by the code that opens Settings on a topic. Each names the category that
    // holds the topic's options: the Steam Web API key (the reason toasts open NetworkData) is
    // under Accounts and refresh.
    LaunchSteam = SignInLaunch,
    NetworkData = AccountsRefresh,
    Vaults = VaultsStorage,
    SecurityPrivacy = PrivacySecurity,
};

inline constexpr int kSettingsCategoryCount = static_cast<int>(SettingsCategory::Loaders) + 1;

void draw_settings(app::AppState& state);

}  // namespace sam::ui::screens
