#include "ui/kit/layout.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>

namespace sam::ui::kit {

float fit_width(const char* label, facet::ControlSize size, bool icon) {
    if (label == nullptr) label = "";
    const char* end = label;
    while (*end != 0 && !(end[0] == '#' && end[1] == '#')) ++end;
    const facet::ControlSize resolved = facet::ResolveControlSize(size);
    const facet::TextStyle style = facet::ControlTextStyle(resolved);
    const float unit = facet::Px(1.0F);
    const float text = facet::MeasureText(style, label, end).x / unit;
    const auto& sp = facet::T().spacing;
    const float pad = resolved == facet::ControlSize::Small ? sp.sm : (resolved == facet::ControlSize::Large ? sp.lg : sp.md);
    const float icon_units = icon ? facet::MeasureText(style, "M").y / unit + sp.sm : 0.0F;
    return std::ceil(text + pad * 2.0F + icon_units + 1.0F);
}

bool fit_button(const char* label, facet::ButtonOptions options) {
    if (options.width == 0.0F) options.width = fit_width(label, options.size, options.icon.IsValid());
    return facet::Button(label, options);
}

void begin_column(const char* id, float max_units, bool centered) {
    const float avail = facet::AvailableWidthUnits();
    const float width = std::max(1.0F, std::min(avail, max_units));
    if (centered && avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + facet::Px((avail - width) * 0.5F));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild(id != nullptr ? id : "##column", ImVec2(facet::Px(width), 0.0F),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
}

void end_column() {
    ImGui::EndChild();
}

}  // namespace sam::ui::kit
