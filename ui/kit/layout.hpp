#pragma once

// Small layout helpers the screens share: a button that keeps its own width and a bounded column.

#include <facet/widgets/buttons.h>

namespace sam::ui::kit {

// The width in units a Facet button of `label` (its visible part, "##" and "###" honoured) needs
// at `size`, with room for a leading icon when `icon` is set. Measures with the current font
// and scale; writes nothing.
float fit_width(const char* label, facet::ControlSize size = facet::ControlSize::Inherit, bool icon = false);

// facet::Button that fits its label even where an item width is pushed (a settings row, a grid
// cell, a column), where a width of 0 would fill the column. options.width other than 0 is
// used as given. Returns true on activation, like facet::Button.
bool fit_button(const char* label, facet::ButtonOptions options = {});

// A column of at most `max_units` units: a child region as wide as min(available, max_units),
// centered in the available width when `centered` is set, else at the leading edge, as tall
// as its content (it never scrolls; the page around it does). Content widths (kFill, field
// defaults) follow the column. Pushes an id from `id`. Always pair with end_column, or use
// ColumnScope.
void begin_column(const char* id, float max_units, bool centered = true);
void end_column();

class [[nodiscard]] ColumnScope {
public:
    explicit ColumnScope(float max_units, bool centered = true, const char* id = "##column") {
        begin_column(id, max_units, centered);
    }
    ~ColumnScope() { end_column(); }
    ColumnScope(const ColumnScope&) = delete;
    ColumnScope& operator=(const ColumnScope&) = delete;
};

}  // namespace sam::ui::kit
