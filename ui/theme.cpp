#include "ui/theme.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>

namespace sam::ui::theme {

namespace {

facet::ThemeMode to_facet(app::ThemeMode mode) {
    switch (mode) {
        case app::ThemeMode::System: return facet::ThemeMode::System;
        case app::ThemeMode::Light:  return facet::ThemeMode::Light;
        case app::ThemeMode::Dark:   return facet::ThemeMode::Dark;
    }
    return facet::ThemeMode::Dark;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

void apply_appearance(const app::Settings& settings, bool animate) {
    if (const auto accent = parse_accent(settings.ui_accent)) {
        facet::SetAccent(*accent);
    } else {
        facet::ClearAccent();
    }
    facet::SetThemeMode(to_facet(settings.theme_mode), animate);
    facet::SetUiScale(std::clamp(settings.ui_scale, app::kUiScaleMin, app::kUiScaleMax));
    facet::SyncImGuiStyle(true);
    if (ImGui::GetCurrentContext() != nullptr) {
        facet::ApplyImGuiStyle();
    }
}

std::optional<ImVec4> parse_accent(std::string_view hex) {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    if (hex.size() != 6) return std::nullopt;
    std::uint32_t rgb = 0;
    for (const char c : hex) {
        const int d = hex_digit(c);
        if (d < 0) return std::nullopt;
        rgb = (rgb << 4) | static_cast<std::uint32_t>(d);
    }
    return facet::Rgb(rgb);
}

std::string format_accent(const ImVec4& color) {
    const auto channel = [](float v) {
        return static_cast<unsigned>(std::clamp(v, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", channel(color.x), channel(color.y),
                  channel(color.z));
    return buf;
}

}  // namespace sam::ui::theme
