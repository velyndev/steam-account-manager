// Settings > Privacy and security: what is hidden on screen, whether the vault opens without
// the password, and safe mode. Every option here acts at once.

#include <filesystem>
#include <system_error>

#include <imgui.h>

#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/app_paths.hpp"
#include "core/log.hpp"
#include "platform/window_affinity.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/settings/capture_hooks.hpp"
#include "ui/screens/settings/categories.hpp"
#include "ui/screens/settings_sections.hpp"

namespace sam::ui::screens::settings {

namespace {

bool& safe_mode_confirm_open() {
    static bool open = false;
    return open;
}

}  // namespace

void reset_privacy_transient() {
    safe_mode_confirm_open() = false;
}

namespace {

void draw_on_screen(Page& p) {
    app::AppState& state = p.state;
    section(p, "On screen");
    if (begin_row(p, "Privacy mode",
                  {.description = "Replaces login names with Hidden login. Click one to reveal it until the vault "
                                  "locks. Also in the title bar.",
                   .keywords = "privacy hide login names hidden stream",
                   .modified = p.s.privacy_mode != p.d.privacy_mode,
                   .immediate = true})) {
        if (facet::Toggle("##value", &p.s.privacy_mode)) {
            state.clear_session_secrets();
            commit_now(state);
        }
        end_row();
    }
    if (begin_row(p, "Hide from screen capture",
                  {.description = "OBS, Discord and other capture software record a blank where the window is. "
                                  "Still visible on your monitor. Needs Windows 10 2004 or newer.",
                   .keywords = "streamproof stream capture obs discord record screenshot",
                   .modified = p.s.streamproof != p.d.streamproof,
                   .immediate = true})) {
        if (facet::Toggle("##value", &p.s.streamproof)) {
            platform::set_capture_excluded(state.main_hwnd, p.s.streamproof);
            commit_now(state);
        }
        end_row();
    }
}

void draw_unlock(Page& p) {
    app::AppState& state = p.state;
    section(p, "Unlock");
    if (begin_row(p, "Open the vault without the password",
                  {.description = "Saves the master password with Windows DPAPI and opens the vault at launch. Anyone "
                                  "signed in as you on this PC can then open it. Applies to every vault.",
                   .keywords = "remember master password dpapi auto unlock skip prompt",
                   .modified = p.s.remember_master_password != p.d.remember_master_password,
                   .immediate = true})) {
        bool on = p.s.remember_master_password;
        if (facet::Toggle("##value", &on)) {
            if (on) {
                if (settings_detail::write_master_pw_cache(state)) {
                    p.s.remember_master_password = true;
                } else {
                    kit::show_toast(state, "The master password could not be saved. See the log for the error.",
                                    {.tone = widgets::ToastTone::Destructive});
                }
            } else {
                p.s.remember_master_password = false;
                std::error_code ec;
                std::filesystem::remove(app::master_pw_cache_path(), ec);
                SAM_LOG_INFO("auto-unlock: DPAPI cache removed");
            }
            commit_now(state);
        }
        end_row();
    }
}

// Why turning safe mode off asks first and turning it on does not: off brings back features
// that touch the account's standing; on only hides them.
void draw_safe_mode(Page& p) {
    app::AppState& state = p.state;
    section(p, "Ban-risk features");
    if (begin_row(p, "Safe mode",
                  {.description = "Hides and turns off the HWID spoofer, the CS2 loaders and the cleaner. Nothing is "
                                  "erased; turning it off brings everything back.",
                   .keywords = "safe mode ban risk spoofer loader cleaner hide disable",
                   .modified = p.s.safe_mode != p.d.safe_mode,
                   .immediate = true})) {
        bool on = p.s.safe_mode;
        if (facet::Toggle("##value", &on)) {
            if (on) {
                p.s.safe_mode = true;
                commit_now(state);
            } else {
                safe_mode_confirm_open() = true;
            }
        }
        if (p.s.safe_mode) {
            facet::StatusPill("Ban-risk features are off",
                              {.tone = facet::Tone::Success, .icon = facet::Icon("success"),
                               .size = facet::ControlSize::Small});
        }
        end_row();
    }
    if (facet::ConfirmDialog("Turn off safe mode?", &safe_mode_confirm_open(),
                             "This brings back the HWID spoofer, the CS2 loaders (gamesense, luminary) and the "
                             "cleaner, with every account's stored HWID profile and launch method. Launching an "
                             "account injects and runs them again, and every cleaner trigger you armed fires again.",
                             {.confirmLabel = "Turn off safe mode", .destructive = true}) ==
        facet::DialogResult::Confirmed) {
        p.s.safe_mode = false;
        commit_now(state);
    }
}

}  // namespace

#ifdef SAM_UI_CAPTURE
void capture::open_safe_mode_confirm() {
    safe_mode_confirm_open() = true;
}
#endif

void draw_privacy_security(Page& p) {
    draw_on_screen(p);
    draw_unlock(p);
    draw_safe_mode(p);
}

}  // namespace sam::ui::screens::settings
