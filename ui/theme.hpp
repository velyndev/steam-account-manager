#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include "app/settings.hpp"

namespace sam::ui::theme {

// The look is owned by Facet (third_party/facet). This module is the bridge: it turns the
// appearance settings into Facet calls, keeps the stock Dear ImGui style in step with the Facet
// theme.

// Pushes the appearance settings (theme mode, accent, UI scale) into Facet and writes the
// resulting theme into the Dear ImGui style. Safe before facet::Init (the values take effect
// at Init) and between frames; a change after Init switches without an animation when
// `animate` is false. Turns on facet::SyncImGuiStyle so every later theme or scale change,
// including a System-mode switch that follows Windows, reaches the stock style.
void apply_appearance(const app::Settings& settings, bool animate = true);

// "#RRGGBB" (or "RRGGBB") to an opaque color; nullopt for an empty or malformed string.
std::optional<ImVec4> parse_accent(std::string_view hex);
// The opaque color as "#RRGGBB".
std::string format_accent(const ImVec4& color);

}  // namespace sam::ui::theme
