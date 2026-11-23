#pragma once

#include "app/app_state.hpp"

namespace sam::ui::screens {
namespace settings_detail {

// Writes the DPAPI-wrapped master password so the next launch (or headless logon
// run) opens the vault without a prompt. False if unavailable or the write fails.
bool write_master_pw_cache(app::AppState& state);

}  // namespace settings_detail
}  // namespace sam::ui::screens
