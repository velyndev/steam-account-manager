#pragma once

// Row centring for list rows and table cells (brief rule 6): every part of a row sits on the
// row's vertical center, without touching Dear ImGui's line internals in screen code.
//
//   facet::DataTableRow(i);
//   ImGui::TableNextColumn();
//   {
//       kit::RowCell cell;                       // centred on the table row
//       facet::Checkbox("##pick", &picked[i]);
//       cell.next();                             // between two items
//       kit::trust_dot(a.trust, 0.0F);
//       cell.next();
//       kit::account_row(state, a, {.layout = kit::AccountRowOptions::Layout::OneLine});
//   }
//   ImGui::TableNextColumn();
//   { kit::RowCell cell; facet::Button("Accept"); }
//
// The same works after facet::VirtualListRow(i), for the content that follows on the row's line.
// Two lines in one item: put a vertical facet stack (Gap::None) between two next() calls.
// kit::account_row, kit::row_markers and kit::trust_dot(trust, row_height) center themselves
// already; inside a RowCell each of them is one item.

#include "ui/kit/row_line.hpp"

namespace sam::ui::kit {

struct RowCellOptions {
    float min_height = 0.0F;  // units; a height the row has at least (0: the row's own height)
    // gap between the items: Item (spacing.sm) by default, Tight for marks that belong together
    facet::Gap gap = facet::Gap::Item;
};

// Lays the items submitted in its lifetime out in one horizontal row, each centered on the
// vertical center of the row the cursor is in: the table row inside a DataTableRow cell (the
// cell padding does not matter), the row item's line after VirtualListRow, or the tallest item
// elsewhere. Call next() between two items: each item is centered on its own (items between two
// next() calls share a top edge, as on any Dear ImGui line). No ImGui::SameLine needed. One item
// for the caller after it ends. Keeps the layout of the last frame under the current id stack,
// so PushID per row as usual; up to 16 items per cell.
class [[nodiscard]] RowCell {
public:
    explicit RowCell(const RowCellOptions& options = {})
        : line_("##row-cell", 0.0F, options.min_height, options.gap) {}
    RowCell(const RowCell&) = delete;
    RowCell& operator=(const RowCell&) = delete;

    // Starts the next item.
    void next() { line_.next(); }

private:
    detail::RowLine line_;
};

// The height of the row the cursor is in, in pixels (see RowCell); 0 on a fresh line.
inline float row_height() {
    return detail::current_row_height();
}

}  // namespace sam::ui::kit
