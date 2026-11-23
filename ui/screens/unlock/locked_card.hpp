#pragma once

// The locked screen: one card centred in the content region the shell gives while no vault is
// open. It unlocks the active vault, lets the user pick another vault when there are several,
// creates the first or a new vault, and offers an existing data folder through a dialog.

#include "app/app_state.hpp"

namespace sam::ui::screens::unlock {

// Draws the card for the active vault (platform::active_vault_id) and runs what it started:
// unlocking, opening from the remembered password, creating a vault. The work runs on the frame
// after the button showed its loading state, on the UI thread. Keeps its
// own state (typed text, errors, the dialog) in this file; typed passwords are cleared once a
// vault is open and whenever the active vault changes.
void draw_locked_card(app::AppState& state);

// The startup picker's hand-over: when several vaults exist and none opens by itself, makes the
// last opened vault active (the first in registry order when none was opened yet) and clears
// state.needs_vault_pick, so the card shows that vault with the others one choice away. Opens
// nothing.
void preselect_vault(app::AppState& state);

#ifdef SAM_UI_CAPTURE
// Capture harness only: puts the card into a state a scene wants to show. Each starts from a
// clean card for the active vault, so call it after the scene chose its vault.
namespace capture {
void reset(const app::AppState& state);
void wrong_password(const app::AppState& state, const char* typed);
void unlocking(const app::AppState& state, const char* typed);  // held in the loading state, nothing runs
void new_vault(const app::AppState& state);
void data_folder(const app::AppState& state, const char* typed_path);  // the dialog open with a path typed in
void remembered_fallback(const app::AppState& state, const char* reason);
}  // namespace capture
#endif

}  // namespace sam::ui::screens::unlock
