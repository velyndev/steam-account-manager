// Capture scenes of the locked screen (wrong password, unlocking, first run, new vault, data
// folder dialog, remembered-password fallback). Debug builds only; see app/ui_capture.hpp.

#ifdef SAM_UI_CAPTURE

#include <vector>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "platform/paths.hpp"
#include "ui/screens/unlock/locked_card.hpp"

namespace {

using sam::app::AppState;
namespace card = sam::ui::screens::unlock::capture;

void locked_single(AppState& state) {
    // One vault, so the card shows that vault's name as its title.
    sam::app::ui_capture::select_demo_vault(state);
    auto& vaults = state.vault_registry.vaults;
    std::erase_if(vaults, [](const sam::app::VaultInfo& v) { return v.id != sam::app::ui_capture::kDemoVaultId; });
}

void setup_single(AppState& state) {
    locked_single(state);
    card::reset(state);
}

void setup_wrong_password(AppState& state) {
    sam::app::ui_capture::select_demo_vault(state);
    card::wrong_password(state, "not-the-password");
}

void setup_unlocking(AppState& state) {
    locked_single(state);
    card::unlocking(state, "capture-demo-password");
}

void setup_first_run(AppState& state) {
    // What init_vaults leaves on a first run: no registry entries, a fresh active id without a
    // vault file.
    state.vault_registry.vaults.clear();
    state.unlocked = false;
    state.needs_vault_pick = false;
    state.current_screen = sam::app::Screen::Unlock;
    sam::platform::set_active_vault_id("capture-first-run");
    card::reset(state);
}

void setup_new_vault(AppState& state) {
    sam::app::ui_capture::select_demo_vault(state);
    card::new_vault(state);
}

void setup_data_folder(AppState& state) {
    sam::app::ui_capture::select_demo_vault(state);
    card::data_folder(state, "D:\\Backups\\steam-account-manager");
}

void setup_remembered_fallback(AppState& state) {
    locked_single(state);
    state.settings.remember_master_password = true;
    card::remembered_fallback(state, "Windows could not decrypt the remembered master password for this user. "
                                     "Enter it once to remember it again.");
}

}  // namespace

SAM_UI_CAPTURE_SCENE(unlock_single, "unlock-single", &setup_single, 30)
SAM_UI_CAPTURE_SCENE(unlock_wrong_password, "unlock-wrong-password", &setup_wrong_password, 30)
SAM_UI_CAPTURE_SCENE(unlock_unlocking, "unlock-unlocking", &setup_unlocking, 30)
SAM_UI_CAPTURE_SCENE(unlock_first_run, "unlock-first-run", &setup_first_run, 30)
SAM_UI_CAPTURE_SCENE(unlock_new_vault, "unlock-new-vault", &setup_new_vault, 30)
SAM_UI_CAPTURE_SCENE(unlock_data_folder, "unlock-data-folder", &setup_data_folder, 30)
SAM_UI_CAPTURE_SCENE(unlock_remembered_fallback, "unlock-remembered-fallback", &setup_remembered_fallback, 30)

#endif  // SAM_UI_CAPTURE
