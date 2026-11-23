// Capture scenes of the Accounts screen (Debug builds only; see app/ui_capture.hpp), and the
// forced states they use.

#include "ui/screens/accounts/accounts_internal.hpp"

namespace sam::ui::screens::accounts {

namespace {
CaptureForce g_force = CaptureForce::None;
}  // namespace

CaptureForce capture_force() {
    return g_force;
}

// The harness runs the transient resets (which clear this) before every scene, so a forced menu
// never leaks into a later scene.
void set_capture_force(CaptureForce force) {
    g_force = force;
}

}  // namespace sam::ui::screens::accounts

#ifdef SAM_UI_CAPTURE

#include <string>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
// Private Facet header, for ForceNextItemState only: Facet has no public way to open a context
// menu without a right click. Kept to this capture-only file.
#include "third_party/facet/src/internal/item.h"

namespace sam::ui::screens::accounts {

void capture_open_next_context_menu() {
    if (g_force == CaptureForce::ContextMenu) {
        facet::internal::ForceNextItemState(facet::internal::ForcedItemState::Open);
    }
}

void capture_begin(CaptureForce force) {
    g_force = force;
}

}  // namespace sam::ui::screens::accounts

namespace {

using sam::app::AppState;
namespace acc = sam::ui::screens::accounts;
namespace cap = sam::app::ui_capture;

void base(AppState& state, sam::app::AccountsViewMode view, int selected, acc::CaptureForce force = acc::CaptureForce::None) {
    cap::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Accounts;
    state.settings.accounts_view = view;
    if (selected >= 0) state.selected_account_id = cap::demo_account_id(selected);
    acc::screen().context_account = state.selected_account_id;
    acc::capture_begin(force);
}

// A VAC and game banned account that also sits in a cooldown.
void setup_list_problem(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 18);
    if (auto* a = state.find_account(cap::demo_account_id(18))) {
        a->cs2.cooldown_expires_unix = acc::now_unix() + 2 * 86400LL + 5 * 3600LL;
        a->cs2.cooldown_reason = "Griefing";
    }
}

void setup_list_clean(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 10);
}

void setup_table(AppState& state) {
    base(state, sam::app::AccountsViewMode::Table, 5);
}

void setup_bulk(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 3);
    for (int i : {3, 4, 7, 11}) state.selected_account_ids.insert(cap::demo_account_id(i));
    state.selection_mode = true;
}

void setup_signin_menu(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 0, acc::CaptureForce::SignInMenu);
}

void setup_context_menu(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 1, acc::CaptureForce::ContextMenu);
}

void setup_remove(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 12);
    acc::request_remove(state, cap::demo_account_id(12));
}

void setup_empty(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, -1);
    state.vault.accounts.clear();
}

void setup_no_results(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 2);
    state.search_query = "nothing like this";
    state.settings.quick_filters.only_banned = true;
}

void setup_group_dialog(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 0);
    acc::request_new_group();
    acc::screen().group.name = "Weekly drop farm";
}

void setup_username(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 3);
    if (auto* a = state.find_account(cap::demo_account_id(3))) acc::request_username(state, *a);
}

void setup_signin_failed(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 6);
    state.launch_error = "Steam did not start: the Steam client could not be found. Check the Steam path in "
                         "Settings, Sign-in and launch.";
}

void setup_quick_clean(AppState& state) {
    base(state, sam::app::AccountsViewMode::List, 0);
    acc::screen().quick_clean_open = true;
}

}  // namespace

SAM_UI_CAPTURE_SCENE(accounts_group_dialog, "accounts-group-dialog", &setup_group_dialog, 30)
SAM_UI_CAPTURE_SCENE(accounts_username, "accounts-username", &setup_username, 30)
SAM_UI_CAPTURE_SCENE(accounts_signin_failed, "accounts-signin-failed", &setup_signin_failed, 30)
SAM_UI_CAPTURE_SCENE(accounts_quick_clean, "accounts-quick-clean", &setup_quick_clean, 30)
SAM_UI_CAPTURE_SCENE(accounts_list_problem, "accounts-list-problem", &setup_list_problem, 30)
SAM_UI_CAPTURE_SCENE(accounts_list_clean, "accounts-list-clean", &setup_list_clean, 30)
SAM_UI_CAPTURE_SCENE(accounts_table, "accounts-table", &setup_table, 30)
SAM_UI_CAPTURE_SCENE(accounts_bulk, "accounts-bulk", &setup_bulk, 30)
SAM_UI_CAPTURE_SCENE(accounts_signin_menu, "accounts-signin-menu", &setup_signin_menu, 30)
SAM_UI_CAPTURE_SCENE(accounts_context_menu, "accounts-context-menu", &setup_context_menu, 30)
SAM_UI_CAPTURE_SCENE(accounts_remove, "accounts-remove", &setup_remove, 30)
SAM_UI_CAPTURE_SCENE(accounts_empty, "accounts-empty", &setup_empty, 30)
SAM_UI_CAPTURE_SCENE(accounts_no_results, "accounts-no-results", &setup_no_results, 30)

#endif  // SAM_UI_CAPTURE
