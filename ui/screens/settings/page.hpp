#pragma once

// Shared scaffolding of the Settings screen: the drawing context every category gets, the
// searchable settings row, lazy section headers, and the pending-changes model behind the
// unsaved changes bar.
//
// Timing model. Most options are "pending": a control writes state.settings at once (the app
// reads it live), and the unsaved changes bar counts every pending field that differs from the
// last saved state; Save persists, Discard copies the saved values back. A few options act on
// the system the moment they change (the logon task, elevation, streamproof, the DPAPI cache,
// safe mode, the proxy policy, the global hotkey, imported templates, and the fields other
// screens edit too): they are "immediate", call commit_now, and never show in the bar.

#include <span>

#include <facet/core/icons.h>
#include <facet/widgets/buttons.h>

#include "app/app_state.hpp"
#include "ui/kit/layout.hpp"
#include "ui/screens/settings_screen.hpp"

namespace sam::ui::screens::settings {

// Name of the Facet settings search (global, not hashed on the id stack).
inline constexpr const char* kSearchName = "sam.settings";

// What a category's draw function gets. One Page lives for one frame.
struct Page {
    app::AppState& state;
    app::Settings& s;          // state.settings
    const app::Settings& d;    // the defaults, for the modified marker and reset
    bool searching = false;    // a query is set: rows of every category are filtered by it
    float label_width = 0.0F;  // units; label column of every settings row this frame

    // Drawing state, written by begin_category / section / the row helpers.
    SettingsCategory category = SettingsCategory::General;
    const char* category_title = "";
    const char* section_title = nullptr;
    const char* section_description = nullptr;
    bool section_shown = false;
    bool category_shown = false;
    bool first_section = true;
    bool card_open = false;   // the rows of the current section are inside a Facet card
    int card_depth = 0;       // group_depth the card was opened at
    int rows_in_card = 0;
    int card_index = 0;       // numbers the cards of this frame for their ids
    int group_depth = 0;      // open dependent groups
    int indent_depth = 0;     // open dependent groups that indent their rows
    bool indent_stack[16] = {};  // per group depth: whether that group indents
    bool controls_disabled = false;  // rows keep their label and description readable, the control is disabled
    int hits[kSettingsCategoryCount] = {};  // rows shown per category this frame
    int open_category = -1;                 // a "Show in ..." button asked for this category
};

// Starts drawing `category`: resets the section state. In search mode the category title is
// drawn lazily above its first matching row.
void begin_category(Page& p, SettingsCategory category);

// Closes the current card. Called after each category; also before content that must not sit
// in a card when it does not go through block().
void end_category(Page& p);
void close_card(Page& p);

// A dependent group (Facet BeginDependentGroup, Disable mode) that keeps the section cards
// balanced: a card opened inside the group closes at its end. end_dependent only after true.
bool begin_dependent(Page& p, const char* id, bool enabled, bool indent = true);
void end_dependent(Page& p);

// Starts a group of rows. The header (Facet SectionHeader) is drawn lazily, just before the
// first row of the group that passes the search, so a search never shows an empty group.
void section(Page& p, const char* title, const char* description = nullptr);

struct RowSpec {
    const char* description = nullptr;  // one line: what the option really does
    const char* keywords = nullptr;     // extra search words, space separated
    bool modified = false;              // differs from its default: Facet's marker dot
    bool reset = false;                 // with modified: the reset button (pending options only)
    bool immediate = false;             // takes effect and is saved at once: the description says so
};

// The words every immediate option ends its description with, so the two kinds of options read
// differently everywhere: these never wait for Save.
inline constexpr const char* kImmediateNote = "Saved immediately.";

// A settings row that takes part in the search. Returns false when the search filters it out
// (then nothing was submitted and end_row must not be called). On true, submit the control
// ("##value") and call end_row, which returns true when the reset button was pressed.
bool begin_row(Page& p, const char* label, const RowSpec& spec = {});
bool end_row();

// Content that is not a row (a table, cards, a preview). Outside a search it returns true and
// the caller draws the content. In a search it draws, when `label` matches, a row named
// `label` with a "Show in <category>" button instead, and returns false.
bool block(Page& p, const char* label, const char* description, const char* keywords = nullptr);

// Draws the pending section header (and the category title in a search) now, for content
// that follows a row the caller already submitted.
void reveal(Page& p);

// Shorthands for the two most common rows. Both are pending options with a reset to `def`.
// Return true when the value was written this frame (by the control or the reset).
bool toggle_row(Page& p, const char* label, bool* value, bool def, const char* description,
                const char* keywords = nullptr);
bool number_row(Page& p, const char* label, int* value, int def, int min, int max, const char* unit,
                const char* description, const char* keywords = nullptr);

// Buttons that fit their label inside rows and cells come from the kit.
using kit::fit_button;
using kit::fit_width;

// Puts the cursor so that buttons of `total_units` (from fit_width, gaps included) end at the
// right edge of the available width, for dialog footers.
void align_trailing(float total_units);
// `units`, but never wider than the available width, for controls with a preferred width.
float cap(float units);

// --- Shared pieces ---------------------------------------------------------------------------

// The shared warning of the ban-risk categories (Cleaner, HWID spoofer, CS2 loaders): a
// Warning banner that says what the feature risks and offers "Turn on safe mode", which hides
// these categories at once.
void ban_risk_banner(Page& p, const char* message);

// Relaunches the app elevated (UAC prompt) and closes this instance when that started.
void restart_elevated(app::AppState& state);

// Appended to the description of options that apply to every vault.
inline constexpr const char* kGlobalNote = " Applies to every vault.";

// --- Pending changes ---------------------------------------------------------------------------

// Persists an immediate change: saves state.settings with every pending field held at its
// saved value, so a pending edit is never written by the side door. Call after writing the
// field into state.settings. When an immediate action also changes a pending field (the logon
// task turns on refresh_on_launch), write that field into saved() too.
void commit_now(app::AppState& state);

// The last saved values of the pending fields.
app::Settings& saved();

// Called once per frame before drawing: takes the saved state when the screen opens with
// nothing pending, after a vault switch, and after another part of the app saved everything
// (the title bar's privacy toggle).
void track_saved_state(app::AppState& state);
// Called once per frame after drawing.
void end_frame_tracking(const app::AppState& state);

// Whether any cleaner setting has a pending change.
bool cleaner_settings_pending(const app::AppState& state);

// The number of pending fields that differ from the saved state.
int pending_count(const app::AppState& state);
// Saves everything (state.save_settings) and makes it the saved state.
void save_pending(app::AppState& state);
// Puts every pending field back to its saved value (and re-applies the appearance preview).
void discard_pending(app::AppState& state);

}  // namespace sam::ui::screens::settings
