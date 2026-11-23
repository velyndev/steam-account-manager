#pragma once

// Transient UI state (open dialogs, drawers, popovers, menus, half-typed dialog fields) never
// outlives its context (brief rule 1). Each screen registers the function that closes its
// transient state; the shell and the capture harness call them.

#include "app/app_state.hpp"

namespace sam::ui::kit {

using TransientReset = void (*)(app::AppState& state);

// Registers `fn`, called by reset_transient_ui: when the vault locks (the idle auto-lock, the
// Lock button and Ctrl+L alike), when another vault becomes active, and before every capture
// scene. Register once, at namespace scope like a capture scene, or on first use; a function
// registered twice is kept once. Use it for state that belongs to the session or to an account
// (a secrets dialog, a send flow, a drawer keyed to an account id).
//
//   namespace { void reset_my_dialogs(app::AppState&) { g = DialogState{}; } }
//   SAM_KIT_TRANSIENT_RESET(my_screen, &reset_my_dialogs)
void register_transient_reset(TransientReset fn);

// Registers `fn` to run when the user leaves `screen` for another one (the sidebar, the palette,
// a shortcut, a toast action or kit::go_to). Use it for state that belongs to the screen: a
// popover, a drawer, a filter menu. reset_transient_ui runs these too.
void register_screen_leave(app::Screen screen, TransientReset fn);

// Calls every registered function (the transient resets, then every screen-leave function).
// The shell calls it on lock and vault change; app/ui_capture.cpp before each scene.
void reset_transient_ui(app::AppState& state);

// Calls the screen-leave functions of `left`. The shell calls it when current_screen changes.
void leave_screen(app::AppState& state, app::Screen left);

}  // namespace sam::ui::kit

// Registers a transient reset at namespace scope (static initialisation, before main).
#define SAM_KIT_TRANSIENT_RESET(ident, fn)                                                         \
    namespace {                                                                                    \
    [[maybe_unused]] const bool sam_kit_transient_reset_##ident =                                  \
        (::sam::ui::kit::register_transient_reset(fn), true);                                     \
    }

// Registers a screen-leave function at namespace scope.
#define SAM_KIT_SCREEN_LEAVE(ident, screen, fn)                                                    \
    namespace {                                                                                    \
    [[maybe_unused]] const bool sam_kit_screen_leave_##ident =                                     \
        (::sam::ui::kit::register_screen_leave((screen), (fn)), true);                            \
    }
