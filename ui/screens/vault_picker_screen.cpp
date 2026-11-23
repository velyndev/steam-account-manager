#include "ui/screens/vault_picker_screen.hpp"

#include "ui/screens/unlock/locked_card.hpp"

namespace sam::ui::screens {

// Why the picker is the unlock card: the vaults are listed in the card with the last opened one
// selected and the password field under them, so choosing and unlocking are one step. The shell
// hosts both in the same region, so the card is drawn here from the first frame.
void draw_vault_picker(app::AppState& state) {
    unlock::preselect_vault(state);
    unlock::draw_locked_card(state);
}

}  // namespace sam::ui::screens
