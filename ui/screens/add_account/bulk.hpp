#pragma once

// The flow shell every Add account method shares (step indicator, scrolling step body, commit bar
// fixed at the bottom), the step card, the bulk preview and result table, and friendly errors.
// The bundle dialogs reuse the table.

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "app/app_state.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/layout.hpp"

namespace sam::ui::screens::add_account_detail {

inline constexpr float kFormWidth = 600.0F;   // units: every one-account step
inline constexpr float kWideWidth = 1040.0F;  // units: every step with a table

using kit::begin_column;
using kit::end_column;

// One line of secondary text that wraps at the available width.
void hint(const char* text);

// Vertical space of `units` design units.
void gap(float units);

// --- Flow shell ---------------------------------------------------------------------------------

// Opens the flow of a method below the page header: a region `width` units wide (centered, at
// most the available width) and the free height of the page, the method's step indicator
// (`current` equal to the step count shows every step done), then the step body, which scrolls
// on its own. The commit bar stays under it, so the commit button is always on screen. Always
// follow with begin_commit_bar and end_flow.
void begin_flow(const char* id, std::span<const char* const> steps, int current, float width);

// Ends the step body and opens the commit bar: a hairline, `status` in secondary text at the
// leading edge (null for none), then the actions submitted until end_flow at the trailing edge
// in the order submitted (the Primary last). No ImGui::SameLine between them.
void begin_commit_bar(const char* status = nullptr);
void end_flow();

// The height left in the step body, in units (for a table that fills a card).
float body_height_left();

// A step as one object: a card with `title` (Headline; null for none) and an optional description,
// then the caller's content. With `fill` the card takes the rest of the step body, for a step
// whose table fills it. Always pair with end_step_card.
void begin_step_card(const char* id, const char* title, const char* description = nullptr, bool fill = false);
void end_step_card();

// The top of every Done step: the success mark (or a warning mark when `warning`), `title` in
// Title3 and `message`, centered.
void done_header(const char* title, const char* message, bool warning = false);

// A flow error (rule 5): an InlineAlert with `title` and the plain `message`, the raw text
// behind a "Details" disclosure when it is not empty, and `action` as its one action (null for
// none). Returns true when the action was activated.
bool flow_error(const char* id, const char* title, const char* message, const std::string& raw,
                const char* action = nullptr);

// The Primary "Open account" (one id) or "Open accounts" (empty id) that ends a flow: shows the
// account, or the Accounts list, and resets the screen. Submit it last in the commit bar.
void open_accounts_button(app::AppState& state, const std::string& account_id);

// --- Bulk imports -------------------------------------------------------------------------------

// What one row of a bulk import is. Before the import: New, Update, or Blocked (cannot be
// imported, for the reason in the note). After it: Imported, Updated, Skipped (not ticked),
// NotImported (was Blocked), Failed (tried and failed). The preview's "can't be imported" count
// becomes the result's "not imported" count, so the two screens add up.
enum class RowStatus { New, Update, Blocked, Imported, Updated, Skipped, NotImported, Failed };

struct BulkRow {
    std::string name;        // the login (or persona) the row adds
    std::string source;      // where it comes from: a file name, a Steam ID
    std::string note;        // the reason or the extra fact for the details column, in plain words
    RowStatus status = RowStatus::New;
    bool checked = false;    // ticked for import
    std::string account_id;  // the vault account after the import
    std::size_t payload = 0; // index into the caller's own data for this row
    std::string raw;         // the technical error text, shown as the details cell's tooltip
};

// New or Update: the row can be ticked.
bool row_importable(const BulkRow& row);
int selected_count(const std::vector<BulkRow>& rows);
// Ticks every importable row with `checked`.
void set_all(std::vector<BulkRow>& rows, bool checked);
// Turns the rows of a finished import into their result: unticked importable rows Skipped,
// Blocked rows NotImported (their notes stay). Rows the caller already set are left alone.
void finish_rows(std::vector<BulkRow>& rows);

// A tri-state "Select all" box. Writes the rows' checked flags.
void select_all_box(const char* id, std::vector<BulkRow>& rows);
// "36 of 37 selected" (", 3 can't be imported"), for the commit bar's status.
std::string selection_status(const std::vector<BulkRow>& rows);

struct BulkTableOptions {
    bool selectable = true;               // a checkbox column; a row's Enter or double click ticks it
    bool hide_names = false;              // privacy mode: the name column shows "Hidden login"
    const char* source_header = "Source";
    float height = 0.0F;                  // units; 0 fills what is left of the step body
};

// The rows as a Facet data table (virtualized, so a few hundred rows stay smooth): the
// checkbox, the name, the source, a status badge and the details (cut with the whole text and
// any raw error in the tooltip). Every cell sits on the row's centre. Writes `checked` only.
void bulk_table(const char* id, std::vector<BulkRow>& rows, const BulkTableOptions& options = {});

struct BulkCounts {
    int imported = 0;
    int updated = 0;
    int skipped = 0;
    int not_imported = 0;
    int failed = 0;
};
BulkCounts count_results(const std::vector<BulkRow>& rows);

// The header line of a bulk card: `title` in Headline and the counts as small status pills at
// the trailing end (new, updates, can't be imported; or added, updated, skipped, not imported,
// failed for a result), under the title in a narrow width. Counts of 0 are left out.
void bulk_header(const char* title, const std::vector<BulkRow>& rows, bool result);

}  // namespace sam::ui::screens::add_account_detail
