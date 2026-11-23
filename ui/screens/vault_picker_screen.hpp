#pragma once

#include "app/app_state.hpp"

namespace sam::ui::screens {

// Startup vault chooser, called (instead of the unlock screen) while state.needs_vault_pick is
// set: several vaults exist and none opens by itself. The choice is part of the unlock card:
// this makes the last opened vault active, clears needs_vault_pick and draws the card, which
// lists every vault with that one selected.
void draw_vault_picker(app::AppState& state);

}  // namespace sam::ui::screens
