// Settings > Notifications: ban and cooldown detection, where its notices appear, which events
// count, and how long they stay; then the confirmation notice of Steam Guard.

#include <array>

#include <facet/widgets/layout.h>
#include <facet/widgets/selection.h>

#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

// The eight event switches as one checkbox group: they are one question ("which changes?").
void draw_events(Page& p) {
    auto& n = p.s.notifications;
    const auto& d = p.d.notifications;
    std::array<bool*, 8> values = {&n.on_new_vac_ban,    &n.on_new_game_ban, &n.on_new_community_ban,
                                   &n.on_new_trade_ban,  &n.on_new_vac_live, &n.on_ban_removed,
                                   &n.on_cooldown_started, &n.on_cooldown_ended};
    const std::array<bool, 8> defaults = {d.on_new_vac_ban,   d.on_new_game_ban, d.on_new_community_ban,
                                          d.on_new_trade_ban, d.on_new_vac_live, d.on_ban_removed,
                                          d.on_cooldown_started, d.on_cooldown_ended};
    static const facet::ChoiceItem kEvents[] = {{"VAC ban"},      {"Game ban"},         {"Community ban"},
                                                {"Trade ban"},    {"VAC live"},         {"Ban removed"},
                                                {"Cooldown started"}, {"Cooldown ended"}};
    bool modified = false;
    for (std::size_t i = 0; i < values.size(); ++i) modified = modified || *values[i] != defaults[i];
    if (!begin_row(p, "Notify about",
                   {.description = "The changes a refresh reports.",
                    .keywords = "events vac game community trade ban removed cooldown started ended live",
                    .modified = modified,
                    .reset = true})) {
        return;
    }
    std::array<bool, 8> checked{};
    for (std::size_t i = 0; i < values.size(); ++i) checked[i] = *values[i];
    if (facet::CheckboxGroup("##value", checked, kEvents)) {
        for (std::size_t i = 0; i < values.size(); ++i) *values[i] = checked[i];
    }
    if (end_row()) {
        for (std::size_t i = 0; i < values.size(); ++i) *values[i] = defaults[i];
    }
}

// Where a notice appears: three places, one question.
void draw_surfaces(Page& p) {
    auto& n = p.s.notifications;
    const auto& d = p.d.notifications;
    bool* values[] = {&n.surface_in_card, &n.surface_toast, &n.surface_windows_notification};
    const bool defaults[] = {d.surface_in_card, d.surface_toast, d.surface_windows_notification};
    bool modified = false;
    for (int i = 0; i < 3; ++i) modified = modified || *values[i] != defaults[i];
    if (!begin_row(p, "Show notices as",
                   {.description = "A marker stays on the account until you have seen it. Windows notifications are "
                                   "mainly for the background refresh at logon and are skipped while this window has "
                                   "the focus.",
                    .keywords = "badge card row marker unread toast popup windows notification tray balloon",
                    .modified = modified,
                    .reset = true})) {
        return;
    }
    static const facet::ChoiceItem kWhere[] = {{"Marker on the account"}, {"Toast in the app"}, {"Windows notification"}};
    std::array<bool, 3> checked{};
    for (int i = 0; i < 3; ++i) checked[static_cast<std::size_t>(i)] = *values[i];
    if (facet::CheckboxGroup("##value", checked, kWhere)) {
        for (int i = 0; i < 3; ++i) *values[i] = checked[static_cast<std::size_t>(i)];
    }
    if (end_row()) {
        for (int i = 0; i < 3; ++i) *values[i] = defaults[i];
    }
}

}  // namespace

// Why the master switch carries a dependent group: every row below it is about the notices it
// produces; the confirmation notice is separate because it comes from Steam Guard, not from it.
void draw_notifications(Page& p) {
    auto& n = p.s.notifications;
    const auto& d = p.d.notifications;
    section(p, "Bans and cooldowns");
    toggle_row(p, "Detect bans and cooldown changes", &n.enabled, d.enabled,
               "Each refresh compares with the last one. The first refresh after adding an account only records "
               "where it stands.",
               "notifications detect bans cooldown changes events");
    if (begin_dependent(p, "##notifications", n.enabled)) {
        draw_surfaces(p);
        draw_events(p);
        number_row(p, "Toast duration", &n.toast_duration_seconds, d.toast_duration_seconds, 2, 30, "s",
                   "How long a toast stays on screen.", "toast duration seconds time");
        number_row(p, "Summarize from", &n.coalesce_threshold, d.coalesce_threshold, 2, 50, "accounts",
                   "When this many accounts change in one refresh, one summary toast replaces the single ones. "
                   "Markers still appear.",
                   "coalesce summary threshold group toasts");
        number_row(p, "Keep notification history for", &n.retention_days, d.retention_days, 1, 365, "days",
                   "Older events are removed from the history.", "history retention days events");
        end_dependent(p);
    }

    section(p, "Steam Guard");
    toggle_row(p, "Toast for new confirmations", &p.s.confirmations.toast_on_new_confirmations,
               p.d.confirmations.toast_on_new_confirmations,
               "Shows a toast when a refresh finds confirmations that were not there before.",
               "confirmations toast new arrive");
}

}  // namespace sam::ui::screens::settings
