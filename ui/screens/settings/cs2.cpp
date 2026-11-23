// Settings > CS2: the Game Coordinator feature and everything that depends on it.

#include <array>

#include <facet/widgets/layout.h>
#include <facet/widgets/selection.h>

#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

// Why one dependent group: every option here needs the live Game Coordinator connection, so
// turning the feature off disables them together.
void draw_cs2(Page& p) {
    auto& g = p.s.cs2_gc;
    const auto& d = p.d.cs2_gc;
    section(p, "Game Coordinator");
    toggle_row(p, "CS2 Game Coordinator", &g.enabled, d.enabled,
               "Shows the CS2 screen and allows a live Game Coordinator connection for inventory, missions and "
               "weekly drops. Off closes any open connection.",
               "cs2 gc game coordinator inventory tab connection");
    if (begin_dependent(p, "##gc", g.enabled)) {
        bool* shown[] = {&g.show_weekly_drop, &g.show_weekly_mission, &g.show_inventory, &g.show_storage_units};
        const bool defaults[] = {d.show_weekly_drop, d.show_weekly_mission, d.show_inventory, d.show_storage_units};
        bool modified = false;
        for (int i = 0; i < 4; ++i) modified = modified || *shown[i] != defaults[i];
        if (begin_row(p, "Show on the CS2 screen",
                      {.description = "The parts of the CS2 screen you use.",
                       .keywords = "weekly drop mission inventory storage units casket card show",
                       .modified = modified,
                       .reset = true})) {
            static const facet::ChoiceItem kParts[] = {{"Weekly drop"}, {"Weekly mission"}, {"Inventory"}, {"Storage units"}};
            std::array<bool, 4> checked{};
            for (int i = 0; i < 4; ++i) checked[static_cast<std::size_t>(i)] = *shown[i];
            if (facet::CheckboxGroup("##value", checked, kParts)) {
                for (int i = 0; i < 4; ++i) *shown[i] = checked[static_cast<std::size_t>(i)];
            }
            if (end_row()) {
                for (int i = 0; i < 4; ++i) *shown[i] = defaults[i];
            }
        }
        toggle_row(p, "Mark the weekly drop claimed automatically", &g.auto_mark_claimed, d.auto_mark_claimed,
                   "Keeps the weekly drop marker in step with the Game Coordinator while connected. Off leaves it "
                   "to Mark claimed.",
                   "weekly drop claimed marker automatic");
        toggle_row(p, "Game Coordinator refresh at launch", &g.auto_pull_on_startup, d.auto_pull_on_startup,
                   "Runs Refresh all and the Game Coordinator refresh at startup, signing in where needed. Skips "
                   "anything still cached. Funds are never fetched.",
                   "auto pull startup refresh gc");
        number_row(p, "Keep Game Coordinator data for", &g.cache_hours, d.cache_hours, 1, 24, "h",
                   "Automatic refreshes skip accounts refreshed within this time.", "gc cache hours ttl lifetime");
        end_dependent(p);
    }
}

}  // namespace sam::ui::screens::settings
