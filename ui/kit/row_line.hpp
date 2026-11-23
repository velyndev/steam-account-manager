#pragma once

// Internal to the kit (screens use kit::RowCell in ui/kit/row.hpp): lays items on one line, each
// centered on the height of the row the cursor is in. Inside a DataTableRow cell that is the
// table row; after a VirtualListRow (on the row item's line) that line; elsewhere the tallest
// item. Each item is its own run of a Facet horizontal stack (next() starts the next one), because
// a stack centers runs, not the items inside a run.

#include <imgui.h>

#include <facet/widgets/layout.h>

namespace sam::ui::kit::detail {

// The height of the row the cursor is in, in pixels: the table row inside a DataTableRow cell,
// else the height of the line the caller is on (0 on a fresh line).
float current_row_height();

class [[nodiscard]] RowLine {
public:
    // `id` keys the Facet stack that does the centering; `content_px` is the tallest item
    // expected, in pixels; `min_line_units` a height the line has at least (0: none).
    RowLine(const char* id, float content_px, float min_line_units = 0.0F, facet::Gap gap = facet::Gap::Tight);
    ~RowLine();
    RowLine(const RowLine&) = delete;
    RowLine& operator=(const RowLine&) = delete;

    // Ends the current item's run and starts the next one: call it between two items.
    void next();

private:
    float saved_offset_ = 0.0F;
};

}  // namespace sam::ui::kit::detail
