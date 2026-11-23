// The Storage units tab and the move queue: the units on the left, the open unit on the right
// with its contents or the inventory to add from, and the queue of one-at-a-time moves.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/selection.h>

#include "core/strings.hpp"
#include "ui/kit/format.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"

namespace sam::ui::screens::cs2 {

namespace {

constexpr int kUnitCapacity = 1000;  // items one CS2 storage unit holds

std::vector<const cs2_gc::DisplayItem*> search(const std::vector<cs2_gc::DisplayItem>& items, const std::string& q) {
    return filter_items(items, {.query = &q});
}

// The unit as an object: its name and, in one row of fixed height, either how full it is or
// the progress of the moves running on it, so nothing under the card moves when a batch
// starts. Failures of the last batch stay marked on their tiles; the caption says so once.
void unit_card(app::AppState& state, Cs2ScreenState& s, const cs2_gc::DisplayItem& unit) {
    (void)state;
    facet::BeginCard((unit.name + "###unit-card").c_str(), {.icon = facet::Icon("layers")});
    const bool running = batch_running(s);
    const int failed = ops_failed(s);
    if (running) {
        const int total = static_cast<int>(s.ops.size());
        const int finished = ops_finished(s);
        const std::string label = "Moving items: " + kit::format_count(finished) + " of " + kit::format_count(total) +
                                  " done###fill";
        facet::ProgressBar(label.c_str(), total > 0 ? static_cast<float>(finished) / static_cast<float>(total) : 0.0F);
    } else if (unit.item_count >= 0) {
        const std::string label = kit::format_count(unit.item_count) + " of " + kit::format_count(kUnitCapacity) +
                                  " items, " + kit::format_count(std::max(0, kUnitCapacity - unit.item_count)) +
                                  " free###fill";
        const float frac = static_cast<float>(unit.item_count) / static_cast<float>(kUnitCapacity);
        facet::ProgressBar(label.c_str(), frac, {.tone = frac >= 0.95F ? facet::Tone::Warning : facet::Tone::Accent});
    } else {
        facet::ProgressBar("Item count unknown###fill", 0.0F);
    }
    // The caption line is always there.
    const Cs2MoveOp* current = nullptr;
    for (const Cs2MoveOp& op : s.ops)
        if (op.status == Cs2MoveOp::Status::Running) current = &op;
    if (running && current != nullptr) {
        note("Moving %s. CS2 moves one item at a time.", current->name.c_str());
    } else if (!running && failed > 0) {
        note("%d item%s could not be moved. Hover a marked tile to see why.", failed, failed == 1 ? "" : "s");
        ImGui::SameLine();
        if (facet::Button("Dismiss", {.variant = facet::ButtonVariant::Plain, .size = facet::ControlSize::Small})) {
            s.ops.clear();
            s.failed_items.clear();
        }
    } else if (running) {
        note("Waiting for CS2.");
    } else if (!s.unit_loading && unit.item_count > static_cast<int>(s.unit_contents.size())) {
        note("Showing %s of %s items; CS2 sent only part of the unit.",
             kit::format_count(static_cast<std::int64_t>(s.unit_contents.size())).c_str(),
             kit::format_count(unit.item_count).c_str());
    } else {
        note("%s", unit.item_count >= kUnitCapacity * 95 / 100 ? "Nearly full." : "Moves go one item at a time.");
    }
    if (running) {
        const bool queued = std::any_of(s.ops.begin(), s.ops.end(),
                                        [](const Cs2MoveOp& op) { return op.status == Cs2MoveOp::Status::Queued; });
        if (queued) {
            ImGui::SameLine();
            if (facet::Button("Stop after this item", {.variant = facet::ButtonVariant::Plain, .size = facet::ControlSize::Small}))
                cancel_queued(s);
        }
    }
    facet::EndCard();
}

// The open unit: what is in it (move out) or what can go in (store), one segmented switch
// between them, because both act on the same unit and share the action row.
void unit_detail(app::AppState& state, Cs2ScreenState& s) {
    const cs2_gc::DisplayItem* unit = find_unit(s, s.open_unit);
    if (unit == nullptr) {
        facet::EmptyState("Choose a storage unit",
                          {.message = "Its contents open here. Select items to move them out, or switch to the "
                                      "inventory to store items in it.",
                           .icon = facet::Icon("layers")});
        return;
    }
    unit_card(state, s, *unit);
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    const std::vector<const cs2_gc::DisplayItem*> contents = search(s.unit_contents, s.unit_query);
    const std::vector<const cs2_gc::DisplayItem*> inventory = search(s.snapshot.items, s.unit_query);
    char in_label[48];
    char add_label[48];
    // One count per unit: the one CS2 reports for the unit, which the list and the card show too.
    const int in_unit = unit->item_count >= 0 ? unit->item_count : static_cast<int>(s.unit_contents.size());
    std::snprintf(in_label, sizeof(in_label), "In this unit (%s)", kit::format_count(in_unit).c_str());
    std::snprintf(add_label, sizeof(add_label), "Add from inventory (%s)",
                  kit::format_count(static_cast<std::int64_t>(s.snapshot.items.size())).c_str());
    const facet::ChoiceItem views[] = {{.label = in_label}, {.label = add_label}};

    facet::BeginFlow("##unit-tools");
    if (facet::FlowNext()) (void)facet::SegmentedControl("##view", &s.unit_view, views);
    if (facet::FlowNext()) (void)facet::SearchField("##unit-search", &s.unit_query, {.placeholder = "Search items", .width = 220.0F});
    facet::EndFlow();

    const bool out_view = s.unit_view == 0;
    const std::vector<const cs2_gc::DisplayItem*>& shown = out_view ? contents : inventory;
    ImGuiSelectionBasicStorage& sel = out_view ? s.unit_selection : s.add_selection;
    const std::vector<const cs2_gc::DisplayItem*> picked = selected_items(shown, sel);
    const int n = static_cast<int>(picked.size());
    const int room = unit->item_count >= 0 ? kUnitCapacity - unit->item_count : kUnitCapacity;
    const bool running = batch_running(s);

    // The action row sits above the grid so it stays in view however long the grid is: the
    // primary action first, then what is selected.
    char label[48];
    if (out_view)
        std::snprintf(label, sizeof(label), n > 0 ? "Move %d to inventory###move" : "Move to inventory###move", n);
    else
        std::snprintf(label, sizeof(label), n > 0 ? "Store %d in this unit###move" : "Store in this unit###move", n);
    ImGui::BeginDisabled(n == 0 || running || s.phase != Cs2Phase::Connected || (!out_view && n > room) ||
                         (out_view && s.unit_loading));
    if (facet::Button(label, {.variant = facet::ButtonVariant::Primary})) {
        enqueue_moves(s, unit->id, !out_view, picked);
        sel.Clear();
        pump(state, s);
    }
    ImGui::EndDisabled();
    if (n > 0) {
        ImGui::SameLine();
        if (facet::Button("Clear selection", {.variant = facet::ButtonVariant::Plain})) sel.Clear();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (n > 0)
        note("%d selected", n);
    else
        note("%s", out_view ? "Select items to move them to the inventory" : "Select items to store them in this unit");
    if (!out_view && n > room)
        note("Only %d more item%s fit in this unit.", room, room == 1 ? "" : "s");
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));

    const bool searching = !s.unit_query.empty();
    // The grid takes the rest of the pane, but at least two rows: in a short window the pane
    // scrolls instead of squeezing the grid to a sliver.
    const float grid_h = std::max(ImGui::GetContentRegionAvail().y / facet::Px(1.0F), 290.0F);
    if (out_view) {
        (void)item_tiles("##unit-grid", contents,
                         {.selection = &s.unit_selection,
                          .mark = &op_mark,
                          .state = &s,
                          .data = s.unit_loading ? facet::DataState::Loading
                                                 : (s.unit_error.empty() ? facet::DataState::Ready : facet::DataState::Error),
                          .empty_text = searching ? "No item in this unit matches the search." : "This storage unit is empty.",
                          .error_text = s.unit_error.empty() ? nullptr : s.unit_error.c_str(),
                          .height = grid_h});
    } else {
        (void)item_tiles("##add-grid", inventory,
                         {.selection = &s.add_selection,
                          .mark = &op_mark,
                          .state = &s,
                          .empty_text = searching ? "No inventory item matches the search."
                                                  : "No inventory item can be stored.",
                          .height = grid_h});
    }
}

}  // namespace

// List and detail: the units are few and named, so a list on the left picks one, and the
// right side is the one unit being worked on. In a narrow window only one pane shows.
void draw_storage(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    (void)acct;
    const bool loading = s.phase == Cs2Phase::Connecting || !s.has_snapshot;
    const auto& units = s.snapshot.units;
    if (!loading && units.empty()) {
        facet::EmptyState("No storage units",
                          {.message = "This inventory has no storage unit. Storage units are bought in CS2 and show "
                                      "up here after the next refresh.",
                           .icon = facet::Icon("layers")});
        return;
    }

    facet::BeginListDetail("##units", &s.units_detail_open, {.listWidth = 240.0F});
    if (facet::BeginListPane()) {
        (void)facet::SectionHeader("Storage units");
        std::vector<std::string> details;
        std::vector<facet::ChoiceItem> rows;
        details.reserve(units.size());
        for (const cs2_gc::DisplayItem& u : units) {
            if (u.item_count < 0)
                details.emplace_back();
            else if (u.item_count >= kUnitCapacity * 95 / 100)
                details.push_back(kit::format_count(u.item_count) + " items");
            else
                details.push_back(kit::format_count(u.item_count) + (u.item_count == 1 ? " item" : " items"));
        }
        for (std::size_t i = 0; i < units.size(); ++i) {
            const bool full = units[i].item_count >= kUnitCapacity * 95 / 100;
            rows.push_back({.label = units[i].name.c_str(),
                            .icon = full ? facet::Icon("warning") : facet::Icon{},
                            .detail = details[i].empty() ? nullptr : details[i].c_str()});
        }
        int index = -1;
        for (std::size_t i = 0; i < units.size(); ++i)
            if (units[i].id == s.open_unit) index = static_cast<int>(i);
        // As tall as the units (up to the pane), so the list ends where its rows end.
        const float row_h = ImGui::GetFrameHeightWithSpacing();
        const int fit = std::max(3, static_cast<int>(ImGui::GetContentRegionAvail().y / row_h) - 1);
        const int visible = std::clamp(static_cast<int>(units.size()), 3, fit);
        const bool locked = batch_running(s) || s.pending == Cs2Pending::LoadUnit;
        if (facet::ListBox("##unit-list", &index, rows,
                           {.visibleRows = visible,
                            .state = loading ? facet::DataState::Loading : facet::DataState::Ready,
                            .readOnly = locked}) &&
            index >= 0) {
            open_unit(s, units[static_cast<std::size_t>(index)].id);
            s.unit_view = 0;
            s.units_detail_open = true;
        }
        if (locked && batch_running(s))
            note("Units can be switched when the move finishes.");
        facet::EndListPane();
    }
    if (facet::BeginDetailPane()) {
        if (loading) {
            facet::Skeleton({.lines = 2});
            facet::Skeleton({.shape = facet::SkeletonShape::Rect, .height = 160.0F});
        } else if (facet::BeginScrollArea("##unit-scroll")) {
            unit_detail(state, s);
            facet::EndScrollArea();
        }
        facet::EndDetailPane();
    }
    facet::EndListDetail();
}

}  // namespace sam::ui::screens::cs2
