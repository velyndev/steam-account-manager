#pragma once

// The Confirmations screen's UI: the table of everything pending, the toolbar and bulk bar,
// the per-account problems, the detail pane and the dialogs. Internal to the screen; the
// entry point stays screens::draw_confirmations.

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/icons.h>

#include "app/app_state.hpp"
#include "core/sda/confirmation.hpp"
#include "ui/screens/confirmations_detail.hpp"

namespace sam::ui::screens::confirmations_view {

// The groups the type filter chips stand for.
enum class Kind { Trade, Market, AccountChange, Other };
inline constexpr int kKindCount = 4;
Kind kind_of(sda::ConfirmationType type);

// "Trade", "Market listing", "Phone number change", "Account recovery", "Feature opt-out",
// "Other". `short_form` gives the compact table text ("Listing", "Phone", ...).
const char* type_title(sda::ConfirmationType type, bool short_form = false);
facet::Icon type_icon(sda::ConfirmationType type);

// The stable selection key of a confirmation (account and confirmation id), never 0.
ImGuiID row_key(const std::string& account_id, const std::string& conf_id);

// Kinds whose approval can hand the account to someone else: account recovery and phone number
// changes. A bulk approve leaves them out unless the user opts in; a single one asks first.
bool risky(sda::ConfirmationType type);

// A deny, or an approve that needs a second look, waiting for the user to confirm it.
struct Decision {
    bool open = false;
    bool allow = false;
    std::vector<confirmations_detail::Pick> picks;
    std::string title;
    std::string message;
    std::string warning;        // approve only: why a risky kind needs care; empty for none
    std::string optin_label;    // bulk approve with risky picks: the opt-in checkbox text
    bool include_risky = false;  // the opt-in's value
    int risky_count = 0;         // risky picks among `picks`
    std::string confirm_label;   // deny only (the approve label follows the opt-in)
};

// Everything the screen remembers between frames. One instance (view()); capture scenes
// reset it.
struct ViewState {
    std::string account_filter;        // empty: every account
    bool kinds[kKindCount] = {};       // none set: every kind
    ImGuiSelectionBasicStorage selection;
    ImGuiID detail_key = 0;            // the confirmation the context column shows; 0 for the overview
    bool drawer_open = false;          // narrow layout: the detail drawer is open
    bool overview_open = false;        // narrow layout: the overview popover is open
    int last_selected = 0;             // selected rows of the last frame
    float card_trim = 16.0F;           // units the context card adds around its scroll area (measured)
    int sort_column = 2;               // index into the table columns (2 is Created)
    bool sort_ascending = false;
    Decision decision;
    bool history_open = false;
    std::string history_query;
    int history_action = 0;            // 0 every decision, 1 approved, 2 denied
    bool scan_open = false;
};
ViewState& view();

// Closes every dialog, drawer and popover and drops the selection (rule 1 of the brief).
// `account_keyed` also clears the account filter, which belongs to the vault.
void reset_transient(bool account_keyed);

// The whole screen, inside the shell's content region.
void draw(app::AppState& state);

// Fills view().decision for `picks` and opens its dialog: a deny of any count, or an approve
// of more than one. A single approve needs no dialog; the caller responds at once.
void ask_decision(app::AppState& state, std::vector<confirmations_detail::Pick> picks, bool allow);

// The layers; each is called every frame at the same place.
void draw_decision(app::AppState& state, ViewState& v);
void draw_history(app::AppState& state, ViewState& v);
void draw_scan_result(app::AppState& state, ViewState& v);

// "Just now", "12 min ago", "3 h ago", "2 days ago", then the date. Empty for no time.
std::string age_text(std::int64_t unix_seconds, std::int64_t now);
// Local "2027-01-01 14:05".
std::string date_time_text(std::int64_t unix_seconds);
// The name an account is shown under here, privacy aware; `fallback_login` is used for an
// account that is no longer in the vault (audit entries).
std::string account_name(const app::AppState& state, const std::string& account_id,
                         const std::string& fallback_login = {});

}  // namespace sam::ui::screens::confirmations_view
