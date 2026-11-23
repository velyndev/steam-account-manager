#pragma once

// Account identity: names, the privacy-aware login, the avatar, a row for lists and the
// searchable account picker.

#include <string>

#include <facet/widgets/common.h>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"

namespace sam::ui::kit {

// --- Names -----------------------------------------------------------------------------------

// True while privacy mode hides this account's login: settings.privacy_mode is on and the
// account is not in state.revealed_logins. Reads state only.
bool login_hidden(const app::AppState& state, const core::Account& account);

// The login as it may be shown: the login itself, or kHiddenLogin while login_hidden. Use it
// for every text that carries a login (labels, combo previews, toasts, search haystacks), so a
// hidden login is never shown and never searchable. Reads state only.
inline constexpr const char* kHiddenLogin = "Hidden login";
std::string login_text(const app::AppState& state, const core::Account& account);

// The name an account is listed under: its Steam persona name, else login_text. Reads state.
std::string display_name(const app::AppState& state, const core::Account& account);

// The login as one item, privacy aware. Without privacy mode it is plain text. With privacy
// mode a hidden login is a small plain button "Hidden login" with the eye icon whose activation
// reveals it (inserts the id into state.revealed_logins until the vault locks); a revealed
// login is the text followed
// by a small icon button that hides it again (erases the id). Text in `style`, labelSecondary
// when `secondary`. Returns true on the frame the reveal state changed. Writes
// state.revealed_logins only.
struct LoginOptions {
    facet::TextStyle style = facet::TextStyle::Footnote;
    bool secondary = true;
};
bool login(app::AppState& state, const core::Account& account, const LoginOptions& options = {});

// --- Avatar ----------------------------------------------------------------------------------

struct AvatarOptions {
    float diameter = 0.0F;  // units; 0 for the control height
};

// The account's Steam avatar as a round image from the avatar cache (the download starts on
// the first call), or Facet's initials avatar of display_name while the picture is missing,
// loading, failed, or settings.show_avatars is off. One item of `diameter` units square; not
// interactive. Reads state.settings and the avatar cache.
void avatar(const app::AppState& state, const core::Account& account, const AvatarOptions& options = {});

// --- Rows ------------------------------------------------------------------------------------

struct AccountRowOptions {
    // TwoLines: the name in Body over the login in Footnote (labelSecondary), next to an avatar
    // of two lines' height. OneLine: avatar, name and the login after it on one line, for rows
    // of the list row height.
    enum class Layout { TwoLines, OneLine };
    Layout layout = Layout::TwoLines;
    bool show_login = true;     // false leaves the login line out (settings.list_view.hide_account_name)
    bool show_avatar = true;
    const char* detail = nullptr;  // extra secondary text after the login (a group, a count); null for none
    // Units; the height to center in. 0 centers on the line the caller is on: inside a
    // VirtualListRow or DataTableRow (after the row item) that is the row, so the avatar, the
    // name and the login share the row's center without any baseline patching.
    float row_height = 0.0F;
};

// The identity block of an account for list rows, picker rows, table cells and headers:
// avatar, display_name and the login (never clickable here: a row is the click target; while
// privacy mode hides the login it is left out). In a DataTableRow cell or after a
// VirtualListRow it centers on the row (see kit::RowCell). Not interactive; one item. Avatar and text are centered on one line: the row the
// caller is on, or row_height. The name is cut with an ellipsis at the available
// width and shows in full on hover. Reads state.
void account_row(const app::AppState& state, const core::Account& account, const AccountRowOptions& options = {});

// --- Picker ----------------------------------------------------------------------------------

// Which accounts a picker offers. Null offers every account.
using AccountFilter = bool (*)(const core::Account& account);

// Ready-made filters for the screens that pick an account.
bool has_authenticator(const core::Account& account);  // account.sda is set (Authenticator, Confirmations)
bool has_steam_id(const core::Account& account);       // steam_id_64 resolved (Trade offers, CS2)

struct AccountPickerOptions {
    AccountFilter filter = nullptr;
    const char* placeholder = "Choose an account";
    const char* empty_text = nullptr;  // shown when no account passes the filter; null for "No accounts"
    float width = 0.0F;                // units; 0 fills the available width
};

// A searchable dropdown of the vault's accounts in vault order (Facet Dropdown, fuzzy filter
// field at the top of the open list). Each entry shows display_name and, when the account has
// a persona, the login as detail; while privacy mode hides a login it is neither shown nor
// matched by the filter. The field shows the account whose id is *account_id (empty or unknown:
// the placeholder). The visible label part is drawn in front of the field ("##id" for none).
// Writes *account_id and returns true when an account is chosen. Reads state.vault and
// state.settings; keeps the open list's filter text under the label's id.
bool account_picker(const char* label, app::AppState& state, std::string* account_id,
                    const AccountPickerOptions& options = {});

}  // namespace sam::ui::kit
