#include "ui/kit/actions.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/anim.h>
#include <facet/core/color.h>
#include <facet/core/draw.h>
#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/display.h>

#include "ui/kit/layout.hpp"

namespace sam::ui::kit {

namespace {

constexpr float kCardIconUnits = 24.0F;

struct GroupHeight {
    int frame = -1;
    float current = 0.0F;  // tallest card of this frame so far
    float last = 0.0F;     // tallest card of the last frame
};

std::unordered_map<std::string, GroupHeight>& groups() {
    static std::unordered_map<std::string, GroupHeight> map;
    return map;
}

float group_height(const char* group, float own) {
    if (group == nullptr) return own;
    GroupHeight& g = groups()[group];
    const int frame = ImGui::GetFrameCount();
    if (g.frame != frame) {
        g.last = g.frame == frame - 1 ? g.current : 0.0F;
        g.current = 0.0F;
        g.frame = frame;
    }
    g.current = std::max(g.current, own);
    return std::max(own, g.last);
}

const char* label_end(const char* label) {
    const char* end = label;
    while (*end != 0 && !(end[0] == '#' && end[1] == '#')) ++end;
    return end;
}

}  // namespace

// Why it is drawn here: Facet has no activatable card without a selection mark. The look takes
// OptionCard's tokens (surface, fill washes, controlBorder edge, card radius, focus ring), so it
// sits beside Facet's cards without a difference.
bool action_card(const char* label, const ActionCardOptions& options) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || label == nullptr) return false;
    const facet::Theme& t = facet::T();
    const facet::Colors& c = t.colors;
    const ImGuiID id = window->GetID(label);
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;

    const float width =
        options.width > 0.0F ? facet::Px(options.width) : ImGui::GetContentRegionAvail().x;
    const float pad = std::round(facet::Px(t.spacing.md));
    const float icon = options.icon.IsValid() ? std::round(facet::Px(kCardIconUnits)) : 0.0F;
    const float icon_gap = icon > 0.0F ? std::round(facet::Px(t.spacing.md)) : 0.0F;
    const float text_w = std::max(1.0F, width - pad * 2.0F - icon - icon_gap);
    const char* end = label_end(label);
    const ImVec2 title = facet::MeasureText(facet::TextStyle::Headline, label, end, text_w);
    ImVec2 desc(0.0F, 0.0F);
    if (options.description != nullptr && options.description[0] != '\0') {
        desc = facet::MeasureText(facet::TextStyle::Footnote, options.description, nullptr, text_w);
    }
    const float line_gap = desc.y > 0.0F ? std::round(facet::Px(t.spacing.xs)) : 0.0F;
    const float own = pad * 2.0F + std::max(icon, title.y + line_gap + desc.y);
    const float height = group_height(options.equal_height_group, own);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::ItemSize(rect.GetSize());
    if (!ImGui::ItemAdd(rect, id)) return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(rect, id, &hovered, &held) && !disabled;
    if (hovered && !disabled) facet::SetHandCursor();
    if (hovered && disabled) facet::SetNotAllowedCursor();

    const float hover = facet::Smooth(id, 0, hovered && !disabled ? 1.0F : 0.0F, 18.0F);
    const float press = facet::Smooth(id, 1, held && !disabled ? 1.0F : 0.0F, 24.0F);
    const float radius = facet::Px(t.radii.card);
    ImDrawList* dl = window->DrawList;
    facet::FillRect(dl, rect.Min, rect.Max, facet::ToU32(disabled ? c.fill[3] : c.bgSecondary), radius);
    const float feedback = std::max(hover, press);
    if (feedback > 0.0F) {
        const ImVec4 tint = facet::Mix(c.fill[2], c.fill[1], press);
        facet::FillRect(dl, rect.Min, rect.Max, facet::ToU32(facet::WithAlpha(tint, tint.w * feedback)), radius);
    }
    const ImVec4 edge = facet::Mix(c.controlBorder, c.controlBorderHover, disabled ? 0.0F : hover);
    facet::StrokeRect(dl, rect.Min, rect.Max, facet::ToU32(edge), radius, facet::HairlineThickness());
    const bool focused = ImGui::GetFocusID() == id && GImGui->NavCursorVisible;
    if (focused) {
        const float ring = facet::Px(t.metrics.focusRingWidth);
        const ImVec2 inset(ring * 0.5F, ring * 0.5F);
        facet::StrokeRect(dl, ImVec2(rect.Min.x + inset.x, rect.Min.y + inset.y),
                          ImVec2(rect.Max.x - inset.x, rect.Max.y - inset.y), facet::ToU32(c.focusRing),
                          std::max(0.0F, radius - ring * 0.5F), ring);
    }

    const ImU32 text_col = facet::ToU32(disabled ? c.labelTertiary : c.label);
    const ImU32 sub_col = facet::ToU32(disabled ? c.labelTertiary : c.labelSecondary);
    float x = rect.Min.x + pad;
    const float content_h = std::max(icon, title.y + line_gap + desc.y);
    const float top = rect.Min.y + std::round((height - content_h) * 0.5F);
    if (icon > 0.0F) {
        facet::DrawIcon(dl, options.icon, ImVec2(x, top + std::round((content_h - icon) * 0.5F)), icon,
                        disabled ? text_col : facet::ToU32(c.label));
        x += icon + icon_gap;
    }
    const float text_top = top + std::round((content_h - (title.y + line_gap + desc.y)) * 0.5F);
    facet::PushTextStyle(facet::TextStyle::Headline);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, text_top), text_col, label, end, text_w);
    facet::PopTextStyle();
    if (desc.y > 0.0F) {
        facet::PushTextStyle(facet::TextStyle::Footnote);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, text_top + title.y + line_gap), sub_col,
                    options.description, nullptr, text_w);
        facet::PopTextStyle();
    }
    return pressed;
}

bool empty_state(const char* label, const EmptyStateOptions& options) {
    const facet::Theme& t = facet::T();
    const bool action = options.action_label != nullptr && options.action_label[0] != '\0';
    float state_h = options.height;
    if (action && state_h > 0.0F) {
        state_h = std::max(0.0F, state_h - t.metrics.controlHeight - t.spacing.lg);
    }
    ImGui::BeginGroup();
    facet::EmptyState(label, {.message = options.message,
                              .icon = options.icon,
                              .tone = options.tone,
                              .width = options.width,
                              .height = state_h});
    bool pressed = false;
    if (action) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
        const float button_w = fit_width(options.action_label, facet::ControlSize::Inherit, options.action_icon.IsValid());
        const float avail = options.width > 0.0F ? options.width : facet::AvailableWidthUnits();
        const float offset = std::max(0.0F, (avail - button_w) * 0.5F);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + facet::Px(offset));
        pressed = facet::Button(options.action_label, {.variant = facet::ButtonVariant::Primary,
                                                       .icon = options.action_icon,
                                                       .width = button_w});
    }
    ImGui::EndGroup();
    return pressed;
}

bool quiet_destructive_button(const char* label, facet::Icon icon, facet::ControlSize size) {
    return fit_button(label, {.variant = facet::ButtonVariant::Secondary, .icon = icon, .size = size});
}

}  // namespace sam::ui::kit
