#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/draw.h>
#include <facet/core/icons.h>
#include <facet/core/image.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/composites.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>

#include "core/strings.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/util.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::screens::cs2 {

namespace {

constexpr std::string_view kStatTrak = "StatTrak\xE2\x84\xA2 ";
constexpr std::string_view kSouvenir = "Souvenir ";
constexpr const char* kWears[] = {"Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"};

struct RarityColor {
    std::uint32_t rgb;
    Rarity rarity;
};
// The grade colors of the item data (rarity.color), weapon skin names; stickers, agents and
// patches share the same colors per tier.
constexpr RarityColor kRarityColors[] = {
    {0xB0C3D9, Rarity::Consumer},   {0x5E98D9, Rarity::Industrial}, {0x4B69FF, Rarity::MilSpec},
    {0x8847FF, Rarity::Restricted}, {0xD32CE6, Rarity::Classified}, {0xEB4B4B, Rarity::Covert},
    {0xE4AE39, Rarity::Contraband},
};

ImGuiID key_from_index(ImGuiSelectionBasicStorage* self, int index) {
    const auto* items = static_cast<const std::span<const cs2_gc::DisplayItem* const>*>(self->UserData);
    if (items == nullptr || index < 0 || index >= static_cast<int>(items->size())) return 0;
    return item_key((*items)[static_cast<std::size_t>(index)]->id);
}

ImU32 data_rgb(std::uint32_t rgb) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

// The content of one tile inside the rectangle ItemGridTile laid out: the picture, the rarity
// bar, two caption lines and the queue marker.
void draw_tile(const cs2_gc::DisplayItem& item, ImVec2 min, ImVec2 max, bool selected, TileMark mark) {
    const facet::Theme& t = facet::T();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = facet::Px(t.spacing.sm);
    const float radius = facet::Px(t.radii.control);

    if (!selected) {
        const ImU32 rest = facet::ToU32(mark == TileMark::Failed ? t.colors.destructive : t.colors.controlBorder);
        facet::StrokeRect(dl, min, max, rest, radius, facet::HairlineThickness());
    } else if (mark == TileMark::Failed) {
        facet::StrokeRect(dl, min, max, facet::ToU32(t.colors.destructive), radius, facet::HairlineThickness());
    }

    facet::PushTextStyle(facet::TextStyle::Caption);
    const float line = ImGui::GetTextLineHeight();
    facet::PopTextStyle();
    const float caption_h = line * 2.0F + facet::Px(t.spacing.xs);

    // Picture, centered at its aspect ratio (Steam item art is 4:3 or square).
    const ImVec2 pic_min(min.x + pad, min.y + pad);
    const ImVec2 pic_max(max.x - pad, max.y - pad - caption_h);
    if (pic_max.y > pic_min.y + 4.0F) {
        ID3D11ShaderResourceView* tex = item.icon_url.empty() ? nullptr : widgets::texture_for(item.icon_url);
        if (tex != nullptr) {
            const float w = pic_max.x - pic_min.x;
            const float h = pic_max.y - pic_min.y;
            const float side = std::min(w, h * (4.0F / 3.0F));
            const float ih = side * 0.75F;
            const ImVec2 c((pic_min.x + pic_max.x) * 0.5F, (pic_min.y + pic_max.y) * 0.5F);
            facet::DrawImage(dl, tex, ImVec2(c.x - side * 0.5F, c.y - ih * 0.5F), ImVec2(c.x + side * 0.5F, c.y + ih * 0.5F));
        } else {
            const float size = std::min(pic_max.x - pic_min.x, pic_max.y - pic_min.y) * 0.45F;
            const ImVec2 c((pic_min.x + pic_max.x) * 0.5F, (pic_min.y + pic_max.y) * 0.5F);
            facet::DrawIcon(dl, facet::Icon("image"), ImVec2(c.x - size * 0.5F, c.y - size * 0.5F), size,
                            facet::ToU32(t.colors.labelTertiary));
        }
    }

    // Rarity bar: the grade color is item data.
    if (item.border_rgb != 0) {
        const float bar = facet::Px(3.0F);
        const float y = max.y - pad - caption_h - bar - facet::Px(t.spacing.xs) * 0.5F;
        facet::FillRect(dl, ImVec2(min.x + pad, y), ImVec2(max.x - pad, y + bar), data_rgb(item.border_rgb),
                        bar * 0.5F);
        // A hairline around the bar keeps light grades (Consumer grey) visible on a light surface.
        facet::StrokeRect(dl, ImVec2(min.x + pad, y), ImVec2(max.x - pad, y + bar), facet::ToU32(t.colors.separator),
                          bar * 0.5F, facet::HairlineThickness());
    }

    const ItemFacts f = item_facts(item);
    const float text_w = max.x - min.x - pad * 2.0F;
    facet::PushTextStyle(facet::TextStyle::Caption);
    const std::string first = truncate_to_width(f.stattrak ? std::string(kStatTrak) + f.weapon : f.weapon, text_w);
    std::string second_src = f.skin.empty() ? f.wear : (f.wear.empty() ? f.skin : f.skin + ", " + f.wear);
    if (second_src.empty() && !f.name_tag.empty()) second_src = "\"" + f.name_tag + "\"";
    const std::string second = truncate_to_width(second_src, text_w);
    const float y0 = max.y - pad - caption_h + facet::Px(t.spacing.xs);
    dl->AddText(ImVec2(min.x + pad, y0), facet::ToU32(t.colors.label), first.c_str());
    if (!second.empty())
        dl->AddText(ImVec2(min.x + pad, y0 + line), facet::ToU32(t.colors.labelSecondary), second.c_str());
    facet::PopTextStyle();

    if (mark != TileMark::None) {
        const float size = facet::Px(14.0F);
        const ImVec2 at(max.x - pad - size, min.y + pad);
        if (mark == TileMark::Failed)
            facet::DrawIcon(dl, facet::Icon("error"), at, size, facet::ToU32(t.colors.destructive));
        else
            facet::DrawIcon(dl, facet::Icon("clock"), at, size, facet::ToU32(t.colors.labelSecondary));
    }
}

}  // namespace

Rarity rarity_of(std::uint32_t rgb) {
    for (const RarityColor& c : kRarityColors)
        if (c.rgb == rgb) return c.rarity;
    return Rarity::Unknown;
}

const char* rarity_name(Rarity rarity) {
    switch (rarity) {
        case Rarity::Consumer: return "Consumer";
        case Rarity::Industrial: return "Industrial";
        case Rarity::MilSpec: return "Mil-Spec";
        case Rarity::Restricted: return "Restricted";
        case Rarity::Classified: return "Classified";
        case Rarity::Covert: return "Covert";
        case Rarity::Contraband: return "Contraband";
        case Rarity::Unknown: break;
    }
    return "Unknown rarity";
}

ItemFacts item_facts(const cs2_gc::DisplayItem& item) {
    ItemFacts f;
    f.rarity = rarity_of(item.border_rgb);
    std::string name = item.name;

    // ItemSchema::decorate: [StatTrak™ ]base[ (wear)][  "custom name"]
    if (const auto q = name.find("  \""); q != std::string::npos && name.size() > q + 3 && name.back() == '"') {
        f.name_tag = name.substr(q + 3, name.size() - q - 4);
        name.resize(q);
    }
    if (name.starts_with(kStatTrak)) {
        f.stattrak = true;
        name.erase(0, kStatTrak.size());
    }
    if (name.starts_with(kSouvenir)) f.souvenir = true;
    for (const char* wear : kWears) {
        const std::string suffix = std::string(" (") + wear + ")";
        if (name.ends_with(suffix)) {
            f.wear = wear;
            name.resize(name.size() - suffix.size());
            break;
        }
    }
    f.title = name;
    if (const auto bar = name.find(" | "); bar != std::string::npos) {
        f.weapon = name.substr(0, bar);
        f.skin = name.substr(bar + 3);
    } else {
        f.weapon = name;
    }
    if (f.weapon.empty()) f.weapon = "Unnamed item";
    return f;
}

ImGuiID item_key(std::uint64_t id) {
    const auto folded = static_cast<ImGuiID>(id ^ (id >> 32));
    return folded == 0 ? 1U : folded;
}

int item_tiles(const char* id, std::span<const cs2_gc::DisplayItem* const> items, const ItemTilesOptions& options) {
    ImGuiSelectionBasicStorage* sel = options.selection;
    if (sel != nullptr) {
        sel->UserData = &items;
        sel->AdapterIndexToStorageId = &key_from_index;
    }
    int activated = -1;
    const int count = static_cast<int>(items.size());
    if (facet::BeginItemGrid(id, count, sel,
                             {.minTileWidth = options.tile_width,
                              .tileAspect = 1.0F,
                              .state = options.data,
                              .emptyText = options.empty_text,
                              .errorText = options.error_text,
                              .height = options.height})) {
        facet::RowRange tiles;
        while (facet::NextVisibleRows(&tiles)) {
            for (int i = tiles.first; i < tiles.last; ++i) {
                const cs2_gc::DisplayItem& item = *items[static_cast<std::size_t>(i)];
                if (facet::ItemGridTile(i)) activated = i;
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const std::string* error = nullptr;
                const TileMark mark = options.mark != nullptr && options.state != nullptr
                                          ? options.mark(*options.state, item.id, &error)
                                          : TileMark::None;
                const bool selected = sel != nullptr && sel->Contains(item_key(item.id));
                if (ImGui::IsItemHovered()) {
                    if (mark == TileMark::Failed && error != nullptr)
                        facet::ItemTooltip("%s\nNot moved. %s", item.name.c_str(), sentence(*error).c_str());
                    else if (mark == TileMark::Queued)
                        facet::ItemTooltip("%s\nWaiting to move", item.name.c_str());
                    else
                        facet::ItemTooltip("%s", item.name.c_str());
                }
                draw_tile(item, min, max, selected, mark);
            }
        }
        facet::EndItemGrid();
    }
    if (sel != nullptr) sel->UserData = nullptr;
    return activated;
}

std::vector<const cs2_gc::DisplayItem*> selected_items(std::span<const cs2_gc::DisplayItem* const> items,
                                                       const ImGuiSelectionBasicStorage& selection) {
    std::vector<const cs2_gc::DisplayItem*> out;
    if (selection.Size == 0) return out;
    for (const cs2_gc::DisplayItem* it : items)
        if (selection.Contains(item_key(it->id))) out.push_back(it);
    return out;
}

int selected_count(std::span<const cs2_gc::DisplayItem* const> items, const ImGuiSelectionBasicStorage& selection) {
    if (selection.Size == 0) return 0;
    int n = 0;
    for (const cs2_gc::DisplayItem* it : items)
        if (selection.Contains(item_key(it->id))) ++n;
    return n;
}

std::vector<const cs2_gc::DisplayItem*> filter_items(const std::vector<cs2_gc::DisplayItem>& items,
                                                     const ItemFilter& filter) {
    std::vector<const cs2_gc::DisplayItem*> out;
    out.reserve(items.size());
    const std::string q = filter.query != nullptr ? core::to_lower(*filter.query) : std::string();
    for (const cs2_gc::DisplayItem& it : items) {
        if (!q.empty() && core::to_lower(it.name).find(q) == std::string::npos) continue;
        if (filter.rarity != 0 && static_cast<int>(rarity_of(it.border_rgb)) != filter.rarity) continue;
        if (filter.stattrak_only && !item_facts(it).stattrak) continue;
        out.push_back(&it);
    }
    if (filter.sort == 1 || filter.sort == 2) {
        const bool high_first = filter.sort == 1;
        std::stable_sort(out.begin(), out.end(), [high_first](const cs2_gc::DisplayItem* a, const cs2_gc::DisplayItem* b) {
            const int ra = static_cast<int>(rarity_of(a->border_rgb));
            const int rb = static_cast<int>(rarity_of(b->border_rgb));
            return high_first ? ra > rb : ra < rb;
        });
    }
    return out;
}

// Search first (the thing people type into), then the two narrowing choices; the row wraps
// under the search in a narrow width through the Flow layout.
void filter_bar(const char* id, std::string* query, int* rarity, bool* stattrak_only, int* sort, int result_count) {
    ImGui::PushID(id);
    static const char* const kRarities[] = {"All rarities", "Consumer", "Industrial", "Mil-Spec", "Restricted",
                                            "Classified",   "Covert",   "Contraband"};
    static const char* const kSorts[] = {"Sort by name", "Rarity, highest first", "Rarity, lowest first"};
    // kRarities index 0 is "all"; Rarity::Unknown (0) is not offered as a filter of its own.
    facet::FilterChip chips[] = {{.label = "StatTrak", .active = *stattrak_only}};
    facet::BeginFlow("##filters", {.gap = facet::Gap::Item});
    if (facet::FlowNext()) {
        (void)facet::SearchFilterBar("##search", query, chips,
                                     {.placeholder = "Search items", .resultCount = result_count, .width = 320.0F});
        *stattrak_only = chips[0].active;
    }
    if (facet::FlowNext()) (void)facet::Dropdown("##rarity", rarity, kRarities, {.width = 150.0F});
    if (facet::FlowNext()) (void)facet::Dropdown("##sort", sort, kSorts, {.width = 180.0F});
    facet::EndFlow();
    ImGui::PopID();
}

void note(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    facet::StyledText(buf, {.style = facet::TextStyle::Footnote, .secondary = true});
}

void note_wrapped(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    facet::StyledText(buf, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
}

std::string sentence(const std::string& text) {
    std::string out = text;
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    if (!out.empty() && out.back() != '.' && out.back() != '!' && out.back() != '?') out += '.';
    return out;
}

std::string item_count_text(int count) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), count == 1 ? "%d item" : "%d items", count);
    return buf;
}

}  // namespace sam::ui::screens::cs2
