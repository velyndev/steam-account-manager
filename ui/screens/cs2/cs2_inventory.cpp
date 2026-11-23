// The Inventory tab: the storable items as a grid with search, rarity filter and sort, and a
// detail card for the selection with the one action an item has here, storing it in a unit.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/draw.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/media.h>

#include "ui/kit/actions.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::screens::cs2 {

namespace {

constexpr int kUnitCapacity = 1000;  // items one CS2 storage unit holds

float avail_units() {
    return ImGui::GetContentRegionAvail().x / facet::Px(1.0F);
}

// The "Store in" row: which unit, and the button that queues the moves. Shared by the single
// item card and the multi-selection card.
void store_controls(Cs2ScreenState& s, std::span<const cs2_gc::DisplayItem* const> items) {
    const auto& units = s.snapshot.units;
    if (units.empty()) {
        note("This inventory has no storage unit to store items in.");
        return;
    }
    std::vector<std::string> details;
    std::vector<facet::ChoiceItem> choices;
    details.reserve(units.size());
    for (const cs2_gc::DisplayItem& u : units)
        details.push_back(u.item_count >= 0 ? item_count_text(u.item_count) : std::string());
    for (std::size_t i = 0; i < units.size(); ++i)
        choices.push_back({.label = units[i].name.c_str(), .detail = details[i].empty() ? nullptr : details[i].c_str()});
    if (s.store_target < 0 || s.store_target >= static_cast<int>(units.size())) s.store_target = 0;
    (void)facet::Dropdown("Storage unit", &s.store_target, choices, {.width = facet::kFill});

    const cs2_gc::DisplayItem& target = units[static_cast<std::size_t>(s.store_target)];
    const int room = target.item_count >= 0 ? kUnitCapacity - target.item_count : kUnitCapacity;
    const int n = static_cast<int>(items.size());
    char label[64];
    std::snprintf(label, sizeof(label), n == 1 ? "Store in this unit###store" : "Store %d items###store", n);
    const bool running = batch_running(s);
    ImGui::BeginDisabled(running || n == 0 || n > room || s.phase != Cs2Phase::Connected);
    if (facet::Button(label, {.variant = facet::ButtonVariant::Primary, .width = facet::kFill})) {
        enqueue_moves(s, target.id, true, items);
        s.inv_selection.Clear();
    }
    ImGui::EndDisabled();
    if (n > room)
        note("Only %d more item%s fit in %s.", room, room == 1 ? "" : "s",
                    target.name.c_str());
    else if (running)
        note("Wait for the current move to finish.");
}

// The card of one item: picture, the rarity as its grade color, the facts read from the name,
// and the store action.
void item_card(Cs2ScreenState& s, const cs2_gc::DisplayItem& item) {
    const ItemFacts f = item_facts(item);
    facet::BeginCard((f.title + "###item-card").c_str());
    const float w = std::max(80.0F, avail_units());
    ID3D11ShaderResourceView* tex = item.icon_url.empty() ? nullptr : widgets::texture_for(item.icon_url);
    if (tex != nullptr) {
        facet::ImageView((item.name + "##picture").c_str(), tex,
                         {.corners = facet::Radius::Element, .width = w, .height = w * 0.75F});
    } else if (!item.icon_url.empty()) {
        facet::Skeleton({.shape = facet::SkeletonShape::Rect, .width = w, .height = w * 0.75F});
    }
    if (item.border_rgb != 0) {
        // The grade color is item data: a bar the width of the picture, as on the tiles.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bar = facet::Px(3.0F);
        ImGui::Dummy(ImVec2(facet::Px(w), bar));
        facet::FillRect(ImGui::GetWindowDrawList(), p, ImVec2(p.x + facet::Px(w), p.y + bar),
                        IM_COL32((item.border_rgb >> 16) & 0xFF, (item.border_rgb >> 8) & 0xFF, item.border_rgb & 0xFF, 255),
                        bar * 0.5F);
    }

    facet::BeginKeyValueList("##facts", {.labelWidth = 96.0F});
    (void)facet::KeyValue("Rarity", rarity_name(f.rarity));
    if (!f.wear.empty()) (void)facet::KeyValue("Exterior", f.wear.c_str());
    if (f.stattrak) (void)facet::KeyValue("StatTrak", "Yes, counts kills");
    if (f.souvenir) (void)facet::KeyValue("Souvenir", "Yes");
    if (!f.name_tag.empty()) (void)facet::KeyValue("Name tag", f.name_tag.c_str());
    (void)facet::KeyValue("Location", "Inventory");
    const std::string asset = std::to_string(item.id);
    facet::EndKeyValueList();
    facet::BeginKeyValueList("##ids", {.labelWidth = 96.0F, .copyable = true});
    (void)facet::KeyValue("Asset ID", asset.c_str());
    facet::EndKeyValueList();

    if (const auto it = s.failed_items.find(item.id); it != s.failed_items.end())
        (void)facet::InlineAlert("Not moved", nullptr, sentence(it->second).c_str(), {.tone = facet::Tone::Destructive});

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    const cs2_gc::DisplayItem* one[] = {&item};
    store_controls(s, one);
    facet::EndCard();
}

void detail_pane(Cs2ScreenState& s, std::span<const cs2_gc::DisplayItem* const> visible, bool loading) {
    if (loading) {
        facet::BeginCard("##loading-card");
        facet::Skeleton({.shape = facet::SkeletonShape::Rect, .height = 120.0F});
        facet::Skeleton({.lines = 3});
        facet::EndCard();
        return;
    }
    const std::vector<const cs2_gc::DisplayItem*> picked = selected_items(visible, s.inv_selection);
    if (picked.empty()) {
        if (visible.empty()) return;  // the grid already says why there is nothing
        facet::EmptyState("No item selected",
                          {.message = "Click an item to see its details. Ctrl and Shift select several to store "
                                      "them together.",
                           .icon = facet::Icon("grid"),
                           .compact = false});
        return;
    }
    if (picked.size() == 1) {
        item_card(s, *picked.front());
        return;
    }
    char title[48];
    std::snprintf(title, sizeof(title), "%zu items selected###multi-card", picked.size());
    facet::BeginCard(title);
    int counts[kRarityCount] = {};
    for (const cs2_gc::DisplayItem* it : picked) ++counts[static_cast<int>(rarity_of(it->border_rgb))];
    facet::BeginKeyValueList("##by-rarity", {.labelWidth = 96.0F});
    for (int r = kRarityCount - 1; r >= 0; --r) {
        if (counts[r] == 0) continue;
        (void)facet::KeyValue(rarity_name(static_cast<Rarity>(r)), item_count_text(counts[r]).c_str());
    }
    facet::EndKeyValueList();
    if (facet::Button("Clear selection", {.variant = facet::ButtonVariant::Plain})) s.inv_selection.Clear();
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    store_controls(s, picked);
    facet::EndCard();
}

// A grid of placeholder tiles while the inventory loads, the shape the real grid will take.
void skeleton_grid(int count) {
    facet::BeginGrid("##inv-skeleton", {.minCellWidth = 104.0F, .gap = facet::Gap::Item});
    for (int i = 0; i < count; ++i) {
        facet::GridNext();
        ImGui::PushID(i);
        facet::Skeleton({.shape = facet::SkeletonShape::Rect, .corners = facet::Radius::Element, .height = 120.0F});
        ImGui::PopID();
    }
    facet::EndGrid();
}

}  // namespace

// Grid and detail side by side (SplitView stacks them in a narrow window): the grid is what
// people scan, the card answers "what is this one" without leaving the grid. Loading shows a
// skeleton grid across the full width, an empty inventory one empty state with Refresh; the
// filters appear only when there is something to filter.
void draw_inventory(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    (void)acct;
    (void)state;
    const bool loading = s.phase == Cs2Phase::Connecting || !s.has_snapshot;
    if (loading) {
        if (facet::BeginScrollArea("##inv-loading")) {
            skeleton_grid(18);
            facet::EndScrollArea();
        }
        return;
    }
    if (s.snapshot.items.empty()) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xl)));
        if (kit::empty_state("No items to show",
                             {.message = "CS2 lists no items for this account that can be moved or stored. Default "
                                         "weapons and items that can never be traded are not shown here.",
                              .icon = facet::Icon("grid"),
                              .action_label = "Refresh",
                              .action_icon = facet::Icon("refresh")}))
            refresh(s);
        return;
    }

    const ItemFilter filter{.query = &s.inv_query, .rarity = s.inv_rarity, .stattrak_only = s.inv_stattrak_only,
                            .sort = s.inv_sort};
    const std::vector<const cs2_gc::DisplayItem*> items = filter_items(s.snapshot.items, filter);
    filter_bar("inv", &s.inv_query, &s.inv_rarity, &s.inv_stattrak_only, &s.inv_sort, static_cast<int>(items.size()));
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));

    static float ratio = 0.66F;
    facet::BeginSplitView("##inv-split", &ratio, {.minFirst = 280.0F, .minSecond = 250.0F});
    if (items.empty()) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xl)));
        if (kit::empty_state("No item matches",
                             {.message = "Nothing in the inventory fits the search and filters.",
                              .icon = facet::Icon("search"),
                              .action_label = "Clear filters"})) {
            s.inv_query.clear();
            s.inv_rarity = 0;
            s.inv_stattrak_only = false;
        }
    } else {
        (void)item_tiles("##inv-grid", items, {.selection = &s.inv_selection, .mark = &op_mark, .state = &s});
    }
    facet::SplitViewNext();
    if (facet::BeginScrollArea("##inv-detail")) {
        detail_pane(s, items, false);
        facet::EndScrollArea();
    }
    facet::EndSplitView();
}

}  // namespace sam::ui::screens::cs2