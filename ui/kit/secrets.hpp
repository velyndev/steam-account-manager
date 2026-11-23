#pragma once

// Secrets: the Steam Guard code display and the master password field.

#include <string>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"

namespace sam::ui::kit {

// --- Authenticator code ----------------------------------------------------------------------

struct AuthenticatorCodeOptions {
    bool copy_button = true;  // the "Copy code" button under the code
    // Hero layout: the code at display size (hero_code_size units, Semibold display face) with
    // wide letter spacing, the countdown ring beside it at the code's height (no number in it),
    // a line under it with the seconds left and the next code (when show_next_code), then the
    // buttons, everything centered in the available width. "Copy code" is then a
    // Primary button. False keeps the compact layout (LargeTitle code, ring, Secondary button).
    bool hero = false;
    float hero_code_size = 46.0F;  // units; the code's font size in the hero layout
};

struct AuthenticatorCodeResult {
    bool copied = false;      // the code was copied this frame (button, or a click on the code)
    bool synced = false;      // the Steam time sync is up, so a code was shown
    bool unreadable = false;  // the shared secret does not decode: nothing was drawn, the caller explains
};

// The current Steam Guard code of `account` (account.sda must be set; nothing is drawn
// otherwise) as the focal point of a view: the five characters in LargeTitle, a ProgressRing
// counting down the seconds of the 30 second window (Destructive tone for the last five), the
// next code in Footnote when settings.sda.show_next_code is on, and a "Copy code" button. With
// settings.sda.hide_current_code the codes show as dots until the code is clicked, which
// reveals them for five seconds (the reveal is kept per account id inside the kit). Copying
// puts the code on the clipboard with platform::clipboard::set_text_with_auto_clear and
// settings.clipboard_clear_seconds. While time_aligner is not synced it shows a spinner and
// "Waiting for the Steam time sync" instead (centered in the hero layout). A shared secret that
// does not decode draws nothing and returns unreadable. Reads state.settings, the account's shared secret
// and time_aligner; writes the clipboard only.
AuthenticatorCodeResult authenticator_code(const app::AppState& state, const core::Account& account,
                                           const AuthenticatorCodeOptions& options = {});

// --- Master password -------------------------------------------------------------------------

struct MasterPasswordOptions {
    bool show_strength = false;         // a strength meter under the field, for choosing a new password
    const char* placeholder = nullptr;  // null for none
    const char* error = nullptr;        // validation text under the field, in the destructive tone
    float width = 0.0F;                 // units; 0 fills the available width
};

// A Facet SecureField for a master password: dots, an eye button that shows the text while
// toggled (masked again when the field loses focus), the Caps Lock hint, and with
// show_strength a strength score (length and character classes) as a meter. The visible label part is drawn in front of the field ("##id" for none). Writes
// *password per keystroke and returns true when it changed. Keeps only Facet's reveal state
// under the label's id.
bool master_password_field(const char* label, std::string* password, const MasterPasswordOptions& options = {});

// The strength score of a master password, 0..1 (0 for an empty password).
float password_strength(const std::string& password);

}  // namespace sam::ui::kit
