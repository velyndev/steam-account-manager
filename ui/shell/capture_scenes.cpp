// Capture scenes of the shell (palette, icon rail, update dialog, toasts, locked state, icon
// sheet). Debug builds only; see app/ui_capture.hpp.

#ifdef SAM_UI_CAPTURE

#include <chrono>
#include <mutex>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "ui/icons.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/page.hpp"
#include "ui/main_window.hpp"

namespace {

using sam::app::AppState;

void setup_palette(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Accounts;
    sam::ui::capture_open_palette("fa");
}

void setup_rail(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Confirmations;
    sam::ui::capture_force_rail();
}

void setup_update(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Accounts;
    std::lock_guard lk(state.update_mutex);
    state.update_result = sam::core::update_check::Result{"v9.9.9-demo", true};
}

void setup_toast(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Accounts;
    state.settings.safe_mode = true;
    state.settings.privacy_mode = true;
    sam::ui::kit::show_toast(state, "Demo toast: weekly drop claimed on Falcon Demo",
                             {.tone = sam::ui::widgets::ToastTone::Success,
                              .id = "capture-success",
                              .account_id = sam::app::ui_capture::demo_account_id(0),
                              .seconds = -1});
    sam::ui::widgets::ToastItem warn;
    warn.id = "capture-warning";
    warn.message = "Demo toast from a worker: cooldown started on Otter Placeholder";
    warn.account_id = sam::app::ui_capture::demo_account_id(1);
    warn.is_warning = true;
    state.toasts.push(std::move(warn));
}

// Two jobs running and an update known: the sidebar's bottom block in its expanded form, and in
// the rail (shell-activity-rail).
void seed_activity(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Accounts;
    state.refresh_all_total.store(30);
    state.refresh_all_done.store(4);
    state.spend_bulk_running.store(true);
    state.spend_total.store(12);
    state.spend_done.store(3);
    std::lock_guard lk(state.update_mutex);
    state.update_result = sam::core::update_check::Result{"v9.9.9-demo", true};
    state.update_modal_dismissed_this_session = true;
}

void setup_activity(AppState& state) {
    seed_activity(state);
}

void setup_activity_rail(AppState& state) {
    seed_activity(state);
    sam::ui::capture_force_rail();
}

void setup_locked(AppState& state) {
    sam::app::ui_capture::select_demo_vault(state);
}

// The icon sheet: every app glyph at 16, 20 and 32 units beside two built-in glyphs for
// comparison, on the page scaffold.
void draw_icon_sheet(AppState&) {
    sam::ui::kit::PageScope page("icon-sheet");
    {
        sam::ui::kit::PageHeaderScope header("App glyphs", {.context = "16, 20 and 32 units; built-in lock and user for comparison"});
    }
    int count = 0;
    const sam::ui::icons::GlyphInfo* glyphs = sam::ui::icons::glyphs(&count);
    const float sizes[] = {16.0F, 20.0F, 32.0F};
    auto row = [&](const char* name, facet::Icon icon) {
        ImGui::PushID(name);
        const float row_h = facet::Px(40.0F);
        const float top = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(top + (row_h - facet::MeasureText(facet::TextStyle::Body, "Ag").y) * 0.5F);
        facet::StyledText(name, {.style = facet::TextStyle::Body});
        float x = facet::Px(200.0F);
        for (const float s : sizes) {
            ImGui::SameLine(x);
            ImGui::SetCursorPosY(top + (row_h - facet::Px(s)) * 0.5F);
            facet::IconGlyph(icon, facet::Px(s), 0);
            x += facet::Px(56.0F);
        }
        // The row's height as an item, so the cursor never moves past the last item.
        ImGui::SetCursorPosY(top);
        ImGui::Dummy(ImVec2(1.0F, row_h));
        ImGui::PopID();
    };
    for (int i = 0; i < count; ++i) row(glyphs[i].name, glyphs[i].icon);
    facet::Separator();
    row("lock (built-in)", facet::Icon("lock"));
    row("user (built-in)", facet::Icon("user"));
}

void setup_icons(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Settings;
    sam::ui::capture_override_content(&draw_icon_sheet);
}

}  // namespace

SAM_UI_CAPTURE_SCENE(shell_palette, "shell-palette", &setup_palette, 30)
SAM_UI_CAPTURE_SCENE(shell_rail, "shell-rail", &setup_rail, 30)
SAM_UI_CAPTURE_SCENE(shell_update, "shell-update", &setup_update, 30)
SAM_UI_CAPTURE_SCENE(shell_toast, "shell-toast", &setup_toast, 30)
SAM_UI_CAPTURE_SCENE(shell_locked, "shell-locked", &setup_locked, 30)
SAM_UI_CAPTURE_SCENE(shell_icons, "shell-icons", &setup_icons, 30)
SAM_UI_CAPTURE_SCENE(shell_activity, "shell-activity", &setup_activity, 30)
SAM_UI_CAPTURE_SCENE(shell_activity_rail, "shell-activity-rail", &setup_activity_rail, 30)

#endif  // SAM_UI_CAPTURE
