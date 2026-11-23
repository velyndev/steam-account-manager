#include "ui/kit/page.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>

namespace sam::ui::kit {

namespace {

struct PageFrame {
    bool scroll = false;
    bool area_open = false;
};

constexpr int kMaxPageDepth = 4;
PageFrame g_pages[kMaxPageDepth];
int g_page_depth = 0;

bool g_header_compact = false;

ImVec2 page_padding() {
    const facet::Theme& t = facet::T();
    const bool compact = facet::AvailableWidthClass() == facet::WidthClass::Compact;
    const float side = compact ? t.spacing.lg : t.spacing.xxl;
    const float top = compact ? t.spacing.lg : t.spacing.xl;
    return facet::Px(ImVec2(side, top));
}

constexpr ImGuiWindowFlags kBodyFlags =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

}  // namespace

// Why a scroll area around a padded child: the area gives Facet's overlay bars and smooth
// wheel, the child gives the padding on all four sides without touching the area's insets.
void begin_page(const char* id, const PageOptions& options) {
    ImGui::PushID(id != nullptr ? id : "page");
    PageFrame frame;
    frame.scroll = options.scroll;
    const ImVec2 pad = page_padding();
    if (options.scroll) {
        frame.area_open = facet::BeginScrollArea("##page-scroll");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
        ImGui::BeginChild("##page-body", ImVec2(0.0F, 0.0F),
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_NavFlattened,
                          kBodyFlags);
        ImGui::PopStyleVar();
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
        ImGui::BeginChild("##page-body", ImVec2(0.0F, 0.0F),
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, kBodyFlags);
        ImGui::PopStyleVar();
    }
    if (g_page_depth < kMaxPageDepth) g_pages[g_page_depth] = frame;
    ++g_page_depth;
}

void end_page() {
    if (g_page_depth <= 0) return;
    --g_page_depth;
    const PageFrame frame = g_page_depth < kMaxPageDepth ? g_pages[g_page_depth] : PageFrame{};
    ImGui::EndChild();
    if (frame.scroll && frame.area_open) facet::EndScrollArea();
    ImGui::PopID();
}

// Why the actions are a top-aligned row placed on the title's center: every action is a control
// of one height, so aligning their tops gives them one baseline whatever their icons are and
// whatever a popover anchored to one of them does to Dear ImGui's last item, and the row sits
// on the middle of the title block. The spacer keeps the actions at the trailing edge.
namespace {
ImVec2 g_header_origin;
float g_header_title_bottom = 0.0F;
}  // namespace

void begin_page_header(const char* title, const PageHeaderOptions& options) {
    g_header_compact = facet::AvailableWidthClass() == facet::WidthClass::Compact;
    g_header_origin = ImGui::GetCursorScreenPos();
    const float avail_units = facet::AvailableWidthUnits();
    // Title and context as one tight block: the context line belongs to the title.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0F));
    facet::Text(facet::TextStyle::Title1, "%s", title != nullptr ? title : "");
    if (options.context != nullptr && options.context[0] != '\0') {
        facet::StyledText(options.context, {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    ImGui::PopStyleVar();
    g_header_title_bottom = ImGui::GetItemRectMax().y;
    if (g_header_compact) {
        facet::BeginStack("##page-actions", {.orientation = facet::Orientation::Horizontal,
                                             .gap = facet::Gap::Item,
                                             .align = facet::LayoutAlign::Start});
        return;
    }
    const float title_h = g_header_title_bottom - g_header_origin.y;
    const float control = facet::ControlHeight(facet::ControlSize::Inherit);
    ImGui::SetCursorScreenPos(
        ImVec2(g_header_origin.x, g_header_origin.y + std::max(0.0F, std::floor((title_h - control) * 0.5F))));
    facet::BeginStack("##page-header", {.orientation = facet::Orientation::Horizontal,
                                        .gap = facet::Gap::Item,
                                        .align = facet::LayoutAlign::Start,
                                        .width = avail_units});
    facet::StackSpacer();
}

void end_page_header() {
    facet::EndStack();
    const float bottom = std::max(g_header_title_bottom, ImGui::GetItemRectMax().y);
    ImGui::SetCursorScreenPos(ImVec2(g_header_origin.x, bottom));
    const float gap = facet::Px(facet::T().spacing.lg);
    ImGui::Dummy(ImVec2(0.0F, gap));
}

}  // namespace sam::ui::kit
