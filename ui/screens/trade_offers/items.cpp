#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/image.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/composites.h>
#include <facet/widgets/display.h>
#include <facet/widgets/overlays.h>

#include "ui/screens/trade_offers/trade_offers_ui.hpp"
#include "ui/screens/trade_offers_detail.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::screens::trade_offers_ui {

namespace {

using trade_offers_detail::economy_image_url;

// The image cache returns null both while a picture downloads and after the download failed.
// A picture that has not arrived this long after it was first asked for counts as failed; it
// still shows if it arrives later.
constexpr double kImageWaitSeconds = 12.0;

std::unordered_map<std::string, double>& first_asked() {
    static std::unordered_map<std::string, double> map;
    return map;
}

struct Picture {
    ID3D11ShaderResourceView* texture = nullptr;
    bool failed = false;
};

Picture picture_for(const core::trade::InventoryItem& item) {
    const std::string url = economy_image_url(item.icon_url);
    if (url.empty()) return {nullptr, true};
    Picture p;
    p.texture = widgets::texture_for(url);
    if (p.texture == nullptr) {
        const double now = ImGui::GetTime();
        auto [it, inserted] = first_asked().try_emplace(url, now);
        p.failed = now - it->second > kImageWaitSeconds;
    }
    return p;
}

const char* item_name(const core::trade::InventoryItem& item) {
    if (!item.market_hash_name.empty()) return item.market_hash_name.c_str();
    if (!item.name.empty()) return item.name.c_str();
    return "Unnamed item";
}

}  // namespace

ImU32 rarity_color(const std::string& hex) {
    if (hex.size() != 6) return 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(hex.c_str(), &end, 16);
    if (end != hex.c_str() + 6) return 0;
    // The rarity color is Steam's item data, not a theme color.
    return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 0xFF);
}

// Why a Facet tile with the texture drawn into it: Facet's inventory widgets take Facet image
// handles and the image cache holds app textures, so the tile draws the fill, the rarity bar,
// the count, selection and focus, and the downloaded picture is laid into its upper part.
bool item_tile(const char* id, const core::trade::InventoryItem& item, float size, bool selected, bool tab_stop) {
    const Picture pic = picture_for(item);
    facet::InventoryItem tile;
    tile.label = item_name(item);
    if (pic.texture == nullptr) tile.icon = facet::Icon(pic.failed ? "image-off" : "image");
    tile.count = item.amount > 1 ? static_cast<int>(item.amount) : 0;
    tile.tint = rarity_color(item.name_color);
    tile.locked = false;

    if (!tab_stop) ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    const bool pressed = facet::InventoryTile(id, tile, {.selected = selected, .dragSource = false, .slotSize = size});
    if (!tab_stop) ImGui::PopItemFlag();

    if (pic.texture != nullptr) {
        const facet::Theme& t = facet::T();
        const float inset = facet::Px(t.spacing.xs);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 p0(min.x + inset, min.y + inset);
        const ImVec2 p1(max.x - inset, max.y - inset - facet::Px(t.composites.inventoryTintHeight));
        facet::DrawImage(ImGui::GetWindowDrawList(), pic.texture, p0, p1,
                         {.fit = facet::ImageFit::Contain, .showStates = false});
    }
    return pressed;
}

bool item_strip(const char* id, const std::vector<core::trade::InventoryItem>& items, float width, float size) {
    if (items.empty()) {
        facet::StyledText("Nothing", {.style = facet::TextStyle::Footnote, .secondary = true});
        return false;
    }
    const float gap = facet::T().spacing.xs;
    const int fit = std::max(1, static_cast<int>((width + gap) / (size + gap)));
    const int total = static_cast<int>(items.size());
    const int shown = total <= fit ? total : std::max(1, fit - 1);
    bool pressed = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    for (int i = 0; i < shown; ++i) {
        if (i > 0) ImGui::SameLine(0.0F, facet::Px(gap));
        ImGui::PushID(i);
        pressed |= item_tile("##item", items[static_cast<std::size_t>(i)], size, false, false);
        ImGui::PopID();
    }
    if (shown < total) {
        ImGui::SameLine(0.0F, facet::Px(gap));
        char more[16];
        std::snprintf(more, sizeof(more), "+%d", total - shown);
        const float text_h = ImGui::GetTextLineHeight();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (facet::Px(size) - text_h) * 0.5F);
        facet::StyledText(more, {.style = facet::TextStyle::Footnote, .secondary = true});
        facet::ItemTooltip("%d more items", total - shown);
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return pressed;
}

void item_row(const char* id, const core::trade::InventoryItem& item) {
    const facet::Theme& t = facet::T();
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const float size = t.metrics.controlHeightLarge;
    const float top = ImGui::GetCursorPosY();
    item_tile("##tile", item, size, false, false);
    ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
    const float text_h = facet::MeasureText(facet::TextStyle::Body, "Ag").y +
                         facet::MeasureText(facet::TextStyle::Footnote, "Ag").y;
    ImGui::SetCursorPosY(top + std::max(0.0F, (facet::Px(size) - text_h) * 0.5F));
    ImGui::BeginGroup();
    facet::TruncatedText(item_name(item), {.style = facet::TextStyle::Body});
    if (!item.type.empty()) {
        facet::TruncatedText(item.type.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    ImGui::EndGroup();
    ImGui::EndGroup();
    ImGui::PopID();
}

}  // namespace sam::ui::screens::trade_offers_ui
