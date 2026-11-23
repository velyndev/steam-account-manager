#pragma once

// Screenshot harness only (SAM_UI_CAPTURE): puts the Settings screen into states a scene setup
// cannot reach through AppState alone, because they live in the screen's own UI state.

#ifdef SAM_UI_CAPTURE

#include <string>

#include "app/app_state.hpp"

namespace sam::ui::screens::settings::capture {

// The search query the screen shows from its next frame.
void set_query(const char* query);
// Runs `fn` once, right after the screen took its saved state on its first frame, so the
// changes `fn` makes show as pending in the unsaved changes bar.
void after_baseline(void (*fn)(app::AppState& state));
void open_create_vault();
void open_delete_vault(const std::string& vault_id);
void open_safe_mode_confirm();
void open_vault_menu(const std::string& vault_id);
void edit_proxy_of(const std::string& account_id);
// Keeps the detail pane scrolled to its end.
void scroll_to_end();
bool scrolling_to_end();

// Called by the screen.
void run_after_baseline(app::AppState& state);

}  // namespace sam::ui::screens::settings::capture

#endif
