#include "ui/kit/row_line.hpp"

#include <algorithm>

#include <imgui_internal.h>

#include <facet/core/scale.h>
#include <facet/widgets/layout.h>

namespace sam::ui::kit::detail {

namespace {

// The table whose current row the cursor is in, when the current window is that table's
// inner window (a DataTableRow cell); null elsewhere.
ImGuiTable* current_table_row() {
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (table == nullptr || table->InnerWindow != ImGui::GetCurrentWindow()) return nullptr;
    if (table->CurrentColumn < 0) return nullptr;
    return table;
}

}  // namespace

float current_row_height() {
    if (ImGuiTable* table = current_table_row()) return table->RowMinHeight;
    return ImGui::GetCurrentWindow()->DC.CurrLineSize.y;
}

// Why every item is a run of its own: a Facet stack places each run (the items between two
// spacers) at the center of the stack's cross size from the run's size in the last frame, while
// the items inside one run follow Dear ImGui's line layout (tops aligned, text pushed down to the
// row's baseline). One item per run, the stack as tall as the row and started at the row's top
// edge, puts every item on the row's center, whatever its height. The text offset is zeroed so
// no text item is pushed down to a baseline on top of that.
RowLine::RowLine(const char* id, float content_px, float min_line_units, facet::Gap gap) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    saved_offset_ = w->DC.CurrLineTextBaseOffset;
    w->DC.CurrLineTextBaseOffset = 0.0F;
    float line = std::max(content_px, facet::Px(min_line_units));
    // The stack keeps its measurements under its id; table columns push no id of their own, so
    // the column (or, on a list row, the x where the cell starts) keeps the cells of one row apart.
    if (ImGuiTable* table = current_table_row()) {
        ImGui::PushID(table->CurrentColumn);
        line = std::max(line, table->RowMinHeight);
        w->DC.CursorPos.y = table->RowPosY1;
    } else {
        ImGui::PushID(static_cast<int>(w->DC.CursorPos.x - w->Pos.x));
        line = std::max(line, w->DC.CurrLineSize.y);
    }
    facet::BeginStack(id, {.orientation = facet::Orientation::Horizontal,
                           .gap = gap,
                           .align = facet::LayoutAlign::Center,
                           .height = line / facet::Scale()});
}

void RowLine::next() {
    ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0F;
    facet::StackSpacer();
}

RowLine::~RowLine() {
    facet::EndStack();
    ImGui::PopID();
    ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = saved_offset_;
}

}  // namespace sam::ui::kit::detail
