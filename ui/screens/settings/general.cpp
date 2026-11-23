// Settings > General: what happens at Windows logon, elevation, updates, clipboard and auto-lock.

#include <windows.h>

#include <imgui.h>

#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/vault_registry.hpp"
#include "core/log.hpp"
#include "platform/paths.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/settings/categories.hpp"
#include "ui/screens/settings_sections.hpp"

namespace sam::ui::screens::settings {

namespace {

struct GeneralState {
    bool restart_open = false;  // "Restart as administrator?" after turning elevation on
    bool drop_open = false;     // "Close to drop administrator?" after turning it off
    bool task_ok = true;        // the last sync of the logon task succeeded
};

GeneralState& general_state() {
    static GeneralState s;
    return s;
}

}  // namespace

void reset_general_transient() {
    general_state().restart_open = false;
    general_state().drop_open = false;
}

namespace {

// The logon action changes the Task Scheduler task at once. Background refresh also needs the
// DPAPI cache and refresh on launch, so it turns both on, or does not happen at all.
void set_logon_action(Page& p, app::LogonAction action) {
    app::AppState& state = p.state;
    const app::LogonAction prev = p.s.logon_action;
    p.s.logon_action = action;
    if (action == app::LogonAction::BackgroundRefresh) {
        p.s.refresh_on_launch = true;
        saved().refresh_on_launch = true;
        if (settings_detail::write_master_pw_cache(state)) p.s.remember_master_password = true;
        if (!p.s.remember_master_password) {
            SAM_LOG_ERROR("startup: background refresh needs the master-password cache");
            p.s.logon_action = prev;
            kit::show_toast(state,
                            "Background refresh needs the saved master password, and it could not be "
                            "written. See the log for the error.",
                            {.tone = widgets::ToastTone::Destructive});
        } else if (state.vault_registry.vaults.size() > 1) {
            app::set_auto_open(state.vault_registry, platform::active_vault_id());
        }
    }
    general_state().task_ok = state.sync_logon_task();
    commit_now(state);
}

// Why one row with the dropdown and its consequences under it: the action, the admin
// requirement and the DPAPI warning are one decision, so they stay together.
void draw_startup(Page& p) {
    app::AppState& state = p.state;
    GeneralState& g = general_state();
    section(p, "Startup", "What happens when you sign in to Windows.");

    if (begin_row(p, "At Windows logon",
                  {.description = "Background refresh runs hidden, notifies about new bans and cooldowns, "
                                  "then exits. Needs administrator. Applies to every vault.",
                   .keywords = "logon login boot autostart task scheduler background refresh startup windows",
                   .modified = p.s.logon_action != p.d.logon_action,
                   .immediate = true})) {
        static const char* const kActions[] = {"Do nothing", "Refresh accounts in the background", "Open the app"};
        int index = static_cast<int>(p.s.logon_action);
        ImGui::BeginDisabled(!state.is_elevated);
        if (facet::Dropdown("##value", &index, kActions, {.width = cap(300.0F)})) {
            set_logon_action(p, static_cast<app::LogonAction>(index));
        }
        ImGui::EndDisabled();
        if (!state.is_elevated && !p.s.run_as_admin) {
            facet::StyledText("Needs administrator: turn on Run as administrator below.",
                              {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        } else if (state.is_elevated && !g.task_ok) {
            facet::InlineAlert("##task-failed", nullptr,
                               "Windows Task Scheduler refused the logon task. The log has the error code.",
                               {.tone = facet::Tone::Destructive});
        }
        if (p.s.logon_action == app::LogonAction::BackgroundRefresh) {
            facet::InlineAlert("##dpapi", nullptr,
                               "The master password is saved with Windows DPAPI so the background run can "
                               "open the vault unattended. Anyone signed in as you on this PC can then open "
                               "the vault without the password.",
                               {.tone = facet::Tone::Warning});
        }
        end_row();
    }

    if (begin_dependent(p, "##logon-open", p.s.logon_action == app::LogonAction::OpenApp)) {
        ImGui::BeginDisabled(!state.is_elevated);
        if (begin_row(p, "Start minimized",
                      {.description = "Only at logon: the window opens minimized to the taskbar. Manual "
                                      "launches always open normally. Applies to every vault.",
                       .keywords = "minimized taskbar logon tray hidden",
                       .modified = p.s.start_minimized != p.d.start_minimized,
                       .immediate = true})) {
            if (facet::Toggle("##value", &p.s.start_minimized)) {
                g.task_ok = state.sync_logon_task();
                commit_now(state);
            }
            end_row();
        }
        ImGui::EndDisabled();
        end_dependent(p);
    }
}

void draw_administrator(Page& p) {
    app::AppState& state = p.state;
    GeneralState& g = general_state();
    section(p, "Administrator");

    if (begin_row(p, "Run as administrator",
                  {.description = "Shows a UAC prompt at every launch. Only the logon task and an elevated "
                                  "Steam need it. Takes effect at the next launch. Applies to every vault.",
                   .keywords = "admin elevation elevated uac runas privileges",
                   .modified = p.s.run_as_admin != p.d.run_as_admin,
                   .immediate = true})) {
        if (facet::Toggle("##value", &p.s.run_as_admin)) {
            commit_now(state);
            if (p.s.run_as_admin && !state.is_elevated) g.restart_open = true;
            if (!p.s.run_as_admin && state.is_elevated) g.drop_open = true;
        }
        if (p.s.run_as_admin && !state.is_elevated) {
            // The notice without Facet's built-in action, which fills the notice's width; the button
            // under it fits its label at every width.
            facet::InlineAlert("##restart-admin", nullptr, "Restart as administrator to apply.",
                               {.tone = facet::Tone::Accent});
            if (fit_button("Restart as administrator")) restart_elevated(state);
        }
        end_row();
    }

    if (facet::ConfirmDialog("Restart as administrator?", &g.restart_open,
                             "A running app cannot gain administrator rights, so this takes effect at the "
                             "next launch. Restart now?",
                             {.confirmLabel = "Restart now", .cancelLabel = "Later"}) ==
        facet::DialogResult::Confirmed) {
        restart_elevated(state);
    }
    if (facet::ConfirmDialog("Close to drop administrator?", &g.drop_open,
                             "This session stays elevated: an elevated app cannot restart itself as a normal "
                             "one. Close the app and open it again, and it starts without the UAC prompt.",
                             {.confirmLabel = "Close the app", .cancelLabel = "Keep it open"}) ==
        facet::DialogResult::Confirmed) {
        PostMessageW(state.main_hwnd, WM_CLOSE, 0, 0);
    }
}

void draw_clipboard_lock(Page& p) {
    section(p, "Clipboard and auto-lock");
    number_row(p, "Clear copied secrets after", &p.s.clipboard_clear_seconds, p.d.clipboard_clear_seconds, 10,
               120, "s", "Copied passwords and Steam Guard codes are wiped from the clipboard after this long.",
               "clipboard clear copy password code wipe seconds");
    number_row(p, "Lock the vault after", &p.s.auto_lock_minutes, p.d.auto_lock_minutes, 0, 240, "min",
               "Locks the vault after this long without input. 0 never locks it.",
               "auto lock idle timeout minutes inactivity");
}

}  // namespace

// Why this order: logon behaviour is the decision people come here for; elevation follows
// because the logon task needs it; the clipboard and auto-lock are set once and forgotten.
void draw_general(Page& p) {
    draw_startup(p);
    draw_administrator(p);
    draw_clipboard_lock(p);
}

}  // namespace sam::ui::screens::settings
