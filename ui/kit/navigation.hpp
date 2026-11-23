#pragma once

// Moving between screens from UI code. The sidebar, the command palette, the shortcuts and the
// toast actions all go through these, so every way of reaching a screen applies the same rules.

#include <span>
#include <string>

#include <facet/core/icons.h>

#include "app/app_state.hpp"
#include "ui/screens/settings_screen.hpp"

namespace sam::ui::kit {

// Shows `screen` and applies the navigation rules: going to Add account clears
// state.selected_account_id, and going to any screen but Accounts clears
// state.selected_account_ids unless state.selection_mode is on. Going to CS2 while
// settings.cs2_gc.enabled is off goes to Accounts instead. Writes state.current_screen.
void go_to(app::AppState& state, app::Screen screen);

// Shows Settings on `category`: go_to(Settings), then state.pending_settings_category, which
// the Settings screen consumes on its next frame. A category hidden by safe mode opens General.
void open_settings(app::AppState& state, screens::SettingsCategory category);

// Selects the account and shows it on Accounts (state.selected_account_id and
// state.current_screen; the multi-selection is left alone). Used by toast actions and the
// command palette.
void open_account(app::AppState& state, const std::string& account_id);

// Switches to another vault: records `vault_id` as the pending vault, sets
// state.relaunch_switch and closes the main window; the exit path saves and locks this vault and
// relaunches the app with --switch. No-op for an empty or the active id.
void switch_vault(app::AppState& state, const std::string& vault_id);

// Whether `screen` can be shown now: CS2 only while settings.cs2_gc.enabled, Unlock never
// from the shell. Reads state.settings.
bool screen_available(const app::AppState& state, app::Screen screen);

// Navigation title and icon of a screen, the same in the sidebar, the palette and the
// shortcut sheet ("Trade offers", the sam-trade glyph). Unlock has an empty title.
const char* screen_title(app::Screen screen);
facet::Icon screen_icon(app::Screen screen);

// The Settings categories in display order, with their titles in sentence case. `unsafe`
// categories are hidden while safe mode is on. The Settings screen, the command palette and
// toasts share this list; SettingsCategory values index it.
struct SettingsCategoryInfo {
    screens::SettingsCategory category;
    const char* title;
    bool unsafe;
};
std::span<const SettingsCategoryInfo> settings_categories();
const char* settings_category_title(screens::SettingsCategory category);

// Whether safe mode hides `category` right now (reads state.settings.safe_mode).
bool settings_category_hidden(const app::AppState& state, screens::SettingsCategory category);

}  // namespace sam::ui::kit
