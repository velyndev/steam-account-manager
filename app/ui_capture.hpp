#pragma once

// Dev-only screenshot harness. Compiled when SAM_UI_CAPTURE is defined (the Debug
// configuration):
//
//   steam-account-manager.exe --ui-capture <outdir> [--scene <id>] [--theme light|dark|both]
//                             [--size 1280x800] [--scale 1.0] [--gpu]
//
// renders the real UI offscreen and writes <outdir>/<scene>-<theme>.png for every scene (or
// only --scene), then exits with 0; any failure prints a message to stderr and to
// <outdir>/capture-data/logs and exits non-zero. --theme defaults to both, --size to
// 1280x800, --scale (the DPI scale) to 1.0. The WARP software rasterizer draws unless --gpu
// is given, so the pixels do not depend on the graphics driver.
//
// The run is hermetic: data_dir() is <outdir>/capture-data (wiped at start), every HTTP
// request fails at once (http::cancel_all), the Steam time sync is replaced by a fixed clock,
// and the run never elevates, takes the single-instance lock, creates a window, a tray icon,
// a global hotkey or a logon task, or touches a Steam process.
//
// Adding a scene: in any .cpp of the exe (wrapped in #ifdef SAM_UI_CAPTURE), write a setup
// function and register it at namespace scope:
//
//   #include "app/ui_capture.hpp"
//   namespace {
//   void setup_my_screen(sam::app::AppState& state) {
//       sam::app::ui_capture::open_demo_vault(state);
//       state.current_screen = sam::app::Screen::Settings;
//   }
//   }  // namespace
//   SAM_UI_CAPTURE_SCENE(my_screen, "my-screen", &setup_my_screen, 30)
//
// The setup gets a fresh AppState per scene and theme: global settings loaded from the
// throwaway data dir, the demo vault registry loaded (state.vault_registry), no vault open
// and no active vault. It runs before the first frame.

#include <cwchar>
#include <span>
#include <string>

namespace sam::app {
struct AppState;
}

namespace sam::app::ui_capture {

// True when the command line asks for the harness. Checked in every build: a build without
// SAM_UI_CAPTURE refuses to start when it sees the switch, so it never launches the real UI
// by mistake.
inline bool requested(const wchar_t* cmd_line) {
    return cmd_line != nullptr && std::wcsstr(cmd_line, L"--ui-capture") != nullptr;
}

#ifdef SAM_UI_CAPTURE

struct CaptureScene {
    const char* id;                  // file name stem and --scene value: lowercase, dashes
    void (*setup)(AppState& state);  // puts the state into the screen to capture
    int settle_frames;               // frames rendered before the readback (at least 2)
};

// Adds a scene; returns true so it can initialize a namespace-scope constant (see
// SAM_UI_CAPTURE_SCENE). The built-in scenes come first in a fixed order, then the
// registered ones sorted by id. A second scene with an id already taken is ignored.
bool register_scene(const CaptureScene& scene);

#define SAM_UI_CAPTURE_SCENE(ident, id_string, setup_fn, frames)                        \
    namespace {                                                                          \
    [[maybe_unused]] const bool sam_ui_capture_scene_##ident =                           \
        ::sam::app::ui_capture::register_scene({(id_string), (setup_fn), (frames)});    \
    }

// Runs the harness. `cmd_line` is wWinMain's command line. Returns the process exit code.
int run(const wchar_t* cmd_line);

// ---- Demo data, for scene setups ---------------------------------------------------------
// Everything below is deterministic and obviously fictional. Account ids are
// "demo-acc-00" .. "demo-acc-29"; the vault ids are kDemoVaultId and the two empty vaults
// "demo-vault-work" and "demo-vault-archive".

inline constexpr const char* kDemoVaultId = "demo-vault-main";
inline constexpr const char* kDemoMasterPassword = "capture-demo-password";
// The fixed time the harness reports to the code generator (time_aligner).
inline constexpr long long kDemoClockUnix = 1798761600;  // 2027-01-01 00:00:00 UTC

std::string demo_account_id(int index);

// Locked, with the demo vault active: the unlock screen asks for its master password.
void select_demo_vault(AppState& state);

// Opens the demo vault the way a real unlock does (vault, master password, bind_vault_session)
// and seeds the in-memory state the Authenticator, Confirmations, Trade Offers and CS2 screens
// read: pending confirmations, trade offers and the nav badge counts. Leaves
// current_screen at Accounts with no selection.
void open_demo_vault(AppState& state);

#endif  // SAM_UI_CAPTURE

}  // namespace sam::app::ui_capture
