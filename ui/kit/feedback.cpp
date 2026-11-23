#include "ui/kit/feedback.hpp"

#include <chrono>
#include <utility>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>

#include "ui/kit/navigation.hpp"

namespace sam::ui::kit {

facet::DialogResult confirm_dialog(const char* title, bool* open, const char* message, const char* confirm_label) {
    return facet::ConfirmDialog(title, open, message, {.confirmLabel = confirm_label});
}

facet::DialogResult destructive_dialog(const char* title, bool* open, const char* message,
                                       const char* confirm_label) {
    return facet::ConfirmDialog(title, open, message, {.confirmLabel = confirm_label, .destructive = true});
}

bool error_dialog(const char* title, bool* open, const char* message) {
    const bool was_open = open != nullptr && *open;
    if (facet::BeginModal(title, open, {.closeButton = false})) {
        facet::InlineAlert("##error", nullptr, message, {.tone = facet::Tone::Destructive});
        ImGui::Spacing();
        facet::BeginStack("##error-actions", {.orientation = facet::Orientation::Horizontal,
                                              .width = facet::AvailableWidthUnits()});
        facet::StackSpacer();
        if (facet::Button("Close")) *open = false;
        facet::EndStack();
        facet::EndModal();
    }
    return was_open && open != nullptr && !*open;
}

void web_api_key_notice(app::AppState& state) {
    if (!state.settings.web_api_key.empty()) return;
    if (facet::InlineAlert("No Steam Web API key##web-api-key", nullptr,
                           "Level, games, bans and CS2 rank stay empty until you add a key in Settings.",
                           {.tone = facet::Tone::Warning, .actionLabel = "Open settings"}) ==
        facet::InlineAlertAction::Action) {
        open_settings(state, screens::SettingsCategory::NetworkData);
    }
}

void web_api_key_notice(app::AppState& state, const ApiKeyNoticeOptions& options) {
    if (!options.compact) {
        web_api_key_notice(state);
        return;
    }
    if (!state.settings.web_api_key.empty()) return;
    if (facet::Banner("##web-api-key-compact", nullptr,
                      "No Steam Web API key: level, games, bans and CS2 rank stay empty.",
                      {.tone = facet::Tone::Warning, .primaryAction = "Open settings"}) ==
        facet::BannerAction::Primary) {
        open_settings(state, screens::SettingsCategory::NetworkData);
    }
}

void show_toast(app::AppState& state, std::string message, const ToastOptions& options) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    widgets::ToastItem t;
    t.id = options.id;
    t.message = std::move(message);
    t.account_id = options.account_id;
    t.tone = options.tone;
    t.is_warning = options.tone == widgets::ToastTone::Warning;
    if (options.account_id.empty() && options.settings_category >= 0) {
        t.on_click_action = widgets::ToastClickAction::Settings;
        t.settings_category = options.settings_category;
    } else if (!options.account_id.empty()) {
        t.on_click_action = widgets::ToastClickAction::Account;
    }
    const int seconds =
        options.seconds == 0 ? state.settings.notifications.toast_duration_seconds : options.seconds;
    t.expires_at_unix = seconds < 0 ? 0 : now + seconds;
    state.toasts.push(std::move(t));
}

}  // namespace sam::ui::kit
