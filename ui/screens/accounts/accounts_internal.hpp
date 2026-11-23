#pragma once

// Shared between the translation units of the Accounts screen (ui/screens/accounts/*.cpp and
// ui/screens/accounts_screen.cpp). Nothing outside the screen includes this.

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "ui/widgets/bundle_dialogs.hpp"

namespace sam::ui::screens::accounts {

// What the screen keeps between frames besides app::AppState: the Facet selection storages,
// which layers are open and the input of the open dialogs.
struct GroupDialog {
    bool open = false;
    bool create = true;     // true: "New group"; false: edits group_id
    std::string group_id;
    std::string name;
    ImVec4 color{0.31F, 0.56F, 0.97F, 1.0F};
};

struct UsernameDialog {
    bool open = false;
    std::string account_id;
    std::string name;
    bool busy = false;
    std::string status;
    bool status_error = false;
};

struct ScreenState {
    ImGuiSelectionBasicStorage list_selection;
    ImGuiSelectionBasicStorage table_selection;
    bool detail_open = false;      // BeginListDetail in a Compact width
    bool inspector_open = true;    // the table view's inspector pane

    bool refresh_menu_open = false;
    bool more_menu_open = false;
    bool signin_menu_open = false;
    bool detail_more_open = false;

    bool remove_open = false;
    std::string remove_id;
    bool bulk_remove_open = false;
    bool quick_clean_open = false;
    bool delete_group_open = false;
    std::string delete_group_id;
    GroupDialog group;
    UsernameDialog username;
    widgets::ExportBundleState export_state;

    // Table sort, kept between frames so the order before BeginDataTable matches the headers.
    int table_sort_column = 0;
    bool table_sort_descending = false;

    // The selected account the open menus and dialogs belong to; when the selection moves on,
    // they close (rule 1 of the brief's section 1c).
    std::string context_account;
    // The account the list last scrolled to; empty after a reset so the next frame scrolls.
    std::string scrolled_to;
    // Selected accounts whose rows the list drew in the last frame (the rest are out of view).
    int selected_in_view = 0;
    // The account of the last sign-in, for "Try again" in the sign-in error dialog.
    std::string last_sign_in_id;
};

// Closes every menu, popover and dialog of the screen and clears their account-keyed input.
// Selection storages and the table sort stay.
void close_transient(ScreenState& s);

ScreenState& screen();

// Unix seconds now.
std::int64_t now_unix();

// Stable selection key of an account (its id hashed) and of a group header row.
ImGuiID account_key(const std::string& account_id);
ImGuiID group_key(const std::string& group_key_text);

// Indices into state.vault.accounts that pass the search and the quick filters, in `sort` order.
std::vector<std::size_t> visible_accounts(const app::AppState& state, int sort);

// Whether a search or a quick filter narrows the list.
bool filters_active(const app::AppState& state);
void clear_filters(app::AppState& state);

// --- Selection ----------------------------------------------------------------------------------

// Loads the storage from state.selected_account_ids (or the single selected_account_id).
void load_selection(ImGuiSelectionBasicStorage& storage, const app::AppState& state);

// Writes a selection read back from a Facet list into the app state: one account selects it
// (state.selected_account_id), several fill state.selected_account_ids and turn
// state.selection_mode on, none clears both. Returns true when a single account was newly
// chosen (the caller opens the detail).
bool store_selection(app::AppState& state, const std::vector<std::string>& ids);

// The accounts of a multi-selection (two or more), else empty.
bool multi_selection(const app::AppState& state);

// --- Actions (account_actions.cpp) ---------------------------------------------------------------

// The sign-in flow: HWID profile, token minting, launch, toasts.
void sign_in(app::AppState& state, core::Account& account);
// Puts the current Steam Guard code on the clipboard with auto clear and confirms with a toast.
void copy_code(app::AppState& state, const core::Account& account);
// Refreshes Steam data, spend (full access) and the GC validation of one account.
void refresh_account(app::AppState& state, core::Account& account);
// Whether refresh_account may run now; `reason` says why not.
bool can_refresh(const app::AppState& state, const core::Account& account, std::string* reason);
// Opens the dialogs of the screen.
void request_remove(app::AppState& state, const std::string& account_id);
void request_username(app::AppState& state, const core::Account& account);
void request_edit(app::AppState& state, const core::Account& account);
void request_notes(app::AppState& state, const core::Account& account);
void request_export(std::vector<std::string> ids);
// Marks `account_id` as the account the next dialog belongs to (selects it), so the dialog is
// not closed as belonging to a previous selection.
void select_for_dialog(app::AppState& state, const std::string& account_id);
void request_new_group();
void request_edit_group(const core::Group& group);
void request_delete_group(const std::string& group_id);
// Moves accounts into a group ("" for ungrouped) and saves.
void move_to_group(app::AppState& state, const std::vector<std::string>& ids, const std::string& group_id);
// Collapse state of a group header ("" is Ungrouped), kept in settings.collapsed_groups.
bool group_collapsed(const app::AppState& state, const std::string& key);
void set_group_collapsed(app::AppState& state, const std::string& key, bool collapsed);
// The per-frame pumps: loader file picks and the token-mint relaunch.
void run_pumps(app::AppState& state);

// --- Views ----------------------------------------------------------------------------------------

void draw_list_view(app::AppState& state, const std::vector<std::size_t>& visible);
void draw_table_view(app::AppState& state, const std::vector<std::size_t>& visible);
// The selected account (or the multi-selection summary) for the detail and inspector panes.
void draw_detail(app::AppState& state, const std::vector<std::size_t>& visible);
// A region inside a scrolling pane with the page's inner padding on every side, growing with
// its content (the kit page's child-window pattern). Always pair them.
void begin_padded(const char* id);
void end_padded();

// --- Menus (account_menus.cpp) ---------------------------------------------------------------------

// The entries of an account's context menu, inside an open BeginContextMenu.
void account_menu_items(app::AppState& state, core::Account& account);
// The entries of a group header's context menu.
void group_menu_items(app::AppState& state, const core::Group& group, const std::vector<std::size_t>& members);
// "Sign in to Steam" with its method menu as one split control. Returns nothing; runs sign_in.
void sign_in_button(app::AppState& state, core::Account& account);
// What the sign-in action does after Steam is up, as a short badge text; null for "only sign in".
const char* login_method_badge(core::LoginMethod method);
const char* login_method_tooltip(core::LoginMethod method);

// --- Dialogs (accounts_dialogs.cpp) ------------------------------------------------------------------

void draw_dialogs(app::AppState& state);

// --- Capture ------------------------------------------------------------------------------------------

// Capture runs only: forces a menu of the screen open for one scene.
enum class CaptureForce { None, SignInMenu, ContextMenu };
CaptureForce capture_force();
void set_capture_force(CaptureForce force);
#ifdef SAM_UI_CAPTURE
// While the ContextMenu force is on: makes the next facet::BeginContextMenu open. Facet has no
// public call for that, so this goes through its private item-state request (capture builds only).
void capture_open_next_context_menu();
#endif

// Closes the menus and dialogs of a previous selection when the selected account changed.
void track_context(app::AppState& state);

}  // namespace sam::ui::screens::accounts
