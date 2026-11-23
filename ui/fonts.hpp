#pragma once

struct ImFont;

namespace sam::ui::fonts {

// Facet owns the UI fonts: facet::Init loads Segoe UI Variable (or the static Segoe UI files)
// into the font atlas, one ImFont per weight. This module adds the app's fallback faces to
// those fonts and names the face drawn over rank badges.

// Merges the fallback faces into every font Facet loaded, so glyphs Segoe UI lacks still
// draw: Microsoft YaHei (msyh.ttc, CJK), Malgun Gothic (malgun.ttf, Korean), Segoe UI Symbol
// (seguisym.ttf) and Cambria Math (cambria.ttc face 1). Account persona names use those
// scripts. A missing file is skipped. Call after every successful facet::Init (Facet removes
// its fonts, and the merged faces with them, at facet::Shutdown) and before the next
// ImGui::NewFrame. Calling it again for the same fonts does nothing.
void attach_fallbacks();

// Facet's bold font (facet::WeightFont), for the rating drawn over a Premier rank badge. The
// font carries no size of its own: pass one to AddText or PushFont.
ImFont* badge();

}  // namespace sam::ui::fonts
