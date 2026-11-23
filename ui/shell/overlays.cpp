#include <cstdio>
#include <mutex>
#include <string>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>

#include "core/version.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/shell/shell.hpp"
#include "ui/util.hpp"

namespace sam::ui::shell {

// The "Update available" dialog. It opens by itself once per session while a newer release is
// known and not skipped, and again from the sidebar's update notice. Escape, the close button
// and the scrim count as "Remind me later".
void draw_update_dialog(app::AppState& app) {
    State& st = state();
    std::string latest;
    {
        std::lock_guard lk(app.update_mutex);
        if (app.update_result) latest = app.update_result->latest_tag;
    }
    if (!app.unlocked || latest.empty()) {
        st.update_open = false;
        return;
    }
    if (!st.update_offered && !app.update_modal_dismissed_this_session &&
        latest != app.settings.version_check_skip_until) {
        st.update_offered = true;
        st.update_open = true;
    }
    const bool was_open = st.update_open;
    bool decided = false;
    if (facet::BeginModal("Update available", &st.update_open)) {
        char text[256];
        std::snprintf(text, sizeof(text), "Steam Account Manager %s is available. You are on version %s.",
                      latest.c_str(), std::string(sam::kVersion).c_str());
        facet::StyledText(text, {.wrap = true});
        ImGui::Spacing();
        facet::BeginStack("##update-actions", {.orientation = facet::Orientation::Horizontal,
                                               .width = facet::AvailableWidthUnits()});
        if (facet::Button("Remind me later", {.variant = facet::ButtonVariant::Plain})) decided = true;
        facet::StackSpacer();
        if (facet::Button("Skip this version")) {
            app.settings.version_check_skip_until = latest;
            app.save_settings();
            decided = true;
        }
        if (facet::Button("Open releases page", {.variant = facet::ButtonVariant::Primary})) {
            open_url(std::string(sam::kRepoUrl) + "/releases");
            decided = true;
        }
        facet::EndStack();
        facet::EndModal();
    }
    if (decided) st.update_open = false;
    if (was_open && !st.update_open) app.update_modal_dismissed_this_session = true;
}

void render_toasts(app::AppState& app) {
    app.toasts.render([&app](const widgets::ToastItem& t) {
        if (t.on_click_action == widgets::ToastClickAction::Settings) {
            app.current_screen = app::Screen::Settings;
            app.pending_settings_category = t.settings_category;
            return;
        }
        if (t.account_id.empty()) return;
        kit::open_account(app, t.account_id);
    });
}

}  // namespace sam::ui::shell
