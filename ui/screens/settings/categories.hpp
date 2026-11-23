#pragma once

// One draw function per Settings category, each in its own file under ui/screens/settings/.
// Each draws its rows through the helpers of page.hpp and calls its own dialogs at the end,
// every frame it is drawn (the screen draws a category every frame while it is selected, and
// every category while a search is open).

#include "ui/screens/settings/page.hpp"

namespace sam::ui::screens::settings {

void draw_general(Page& p);
void draw_appearance(Page& p);
void draw_accounts_refresh(Page& p);
void draw_steam_guard(Page& p);
void draw_sign_in_launch(Page& p);
void draw_cs2(Page& p);
void draw_trading(Page& p);
void draw_notifications(Page& p);
void draw_network(Page& p);
void draw_privacy_security(Page& p);
void draw_vaults_storage(Page& p);
void draw_about(Page& p);
void draw_cleaner(Page& p);
void draw_hwid_spoofer(Page& p);
void draw_loaders(Page& p);

// Close the dialogs, menus and popovers each category file keeps open, and drop state that
// belongs to an object (the vault a menu was opened for, the account whose proxy is edited).
// The screen calls all of them through reset_transient when the category, the search, the
// screen, the vault or the lock state changes.
void reset_general_transient();
void reset_network_transient();
void reset_privacy_transient();
void reset_vaults_transient();
void reset_cleaner_transient();

}  // namespace sam::ui::screens::settings
