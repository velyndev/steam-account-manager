#pragma once

// Internal to the shell (ui/main_window.cpp and ui/shell/*.cpp). Screens include
// ui/main_window.hpp and the kit, never this header.

#include <string>
#include <vector>

#include <facet/core/input.h>

#include "app/app_state.hpp"

namespace sam::ui::shell {

// The app's registered shortcuts (facet::RegisterShortcut). Registered once, on first use.
struct Shortcuts {
    facet::ShortcutId palette;
    facet::ShortcutId shortcut_sheet;
    facet::ShortcutId lock;
    facet::ShortcutId privacy;
    facet::ShortcutId accounts;
    facet::ShortcutId authenticator;
    facet::ShortcutId confirmations;
    facet::ShortcutId trade_offers;
    facet::ShortcutId cs2;
    facet::ShortcutId add_account;
    facet::ShortcutId settings;
};
const Shortcuts& shortcuts();

// Shell UI state that lives across frames. Reset by reset_state (each capture scene starts
// from a fresh shell).
struct State {
    bool palette_open = false;
    bool sheet_open = false;
    bool update_open = false;     // the "Update available" dialog
    bool update_offered = false;  // the dialog opened by itself once this session
    bool vault_menu_open = false; // the rail's vault popover
    bool rail = false;            // the sidebar shows as the icon rail this frame
    bool force_rail = false;      // capture scenes: the rail whatever the width
    bool caption_blocked = false; // a popup is open, so the title bar must not start a drag
    // Capture scenes: text typed into the command palette once it has been open this many
    // frames (-1: nothing pending).
    std::string pending_palette_query;
    int pending_palette_frames = -1;
    void (*content_override)(app::AppState& state) = nullptr;  // capture scenes: replaces the screen
    bool activity_open = false;   // the job list popover of the sidebar's activity item
    float pinned_height = 0.0F;   // pixels of the sidebar's pinned bottom block in the last frame
    // What the last frame showed, to notice a lock, a vault change and a screen change.
    bool seen = false;
    bool was_unlocked = false;
    std::string last_vault;
    app::Screen last_screen = app::Screen::Unlock;
};
State& state();
void reset_state();

// Per-frame parts, in the order ui::draw calls them.
void handle_shortcuts(app::AppState& app);      // enables, routes and runs the shortcuts
void draw_title_bar(app::AppState& app);        // the bar and its hairline; handles the window buttons
void draw_sidebar(app::AppState& app, float height);  // at the cursor, `height` pixels tall
void draw_command_palette(app::AppState& app);  // the palette and the shortcut sheet
void draw_update_dialog(app::AppState& app);
void render_toasts(app::AppState& app);

// The global actions, shared by the title bar, the palette and the shortcuts.
void lock_vault(app::AppState& app);
void toggle_privacy(app::AppState& app);
bool can_refresh_all(const app::AppState& app);
void refresh_all(app::AppState& app);
// One background job the app tracks (Refresh all, the CS2 pull, funds, confirmations, the
// cleaner, single refreshes), as the activity item shows it. total 0: progress unknown.
struct Job {
    std::string text;
    int done = 0;
    int total = 0;
};
std::vector<Job> running_jobs(const app::AppState& app);

// The sidebar's activity item (only while jobs run) and update notice (only while a newer,
// unskipped release is known), each in its expanded or rail form. Return whether they drew.
bool draw_activity(app::AppState& app, bool rail);
bool draw_update_notice(app::AppState& app, bool rail);
// The newer release's tag while one is known and not skipped, else "".
std::string pending_update(app::AppState& app);
// Opens the repository page in the browser.
void open_repository();

// The active vault's display name ("" when none is active).
std::string active_vault_name(const app::AppState& app);

}  // namespace sam::ui::shell
