#include "ui/fonts.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/text.h>
#include <facet/core/theme.h>

namespace sam::ui::fonts {

namespace {

constexpr char kFallbackPrefix[] = "sam-fallback ";

struct FallbackFace {
    const char* file;
    int face_index;
};

constexpr FallbackFace kFallbacks[] = {
    {"msyh.ttc", 0},
    {"malgun.ttf", 0},
    {"seguisym.ttf", 0},
    {"cambria.ttc", 1},
};

std::filesystem::path windows_font_path(const char* filename) {
    const char* win_dir = std::getenv("WINDIR");
    std::filesystem::path root = win_dir ? std::filesystem::path(win_dir) : std::filesystem::path("C:\\Windows");
    return root / "Fonts" / filename;
}

std::vector<std::uint8_t> read_font_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = static_cast<std::streamsize>(in.tellg());
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

std::vector<std::uint8_t>& cached_font_bytes(const char* filename) {
    static std::map<std::string, std::vector<std::uint8_t>> cache;
    auto it = cache.find(filename);
    if (it == cache.end()) {
        it = cache.emplace(filename, read_font_file(windows_font_path(filename))).first;
    }
    return it->second;
}

bool has_fallbacks(const ImFont* font) {
    for (const ImFontConfig* src : font->Sources) {
        if (src != nullptr && std::strncmp(src->Name, kFallbackPrefix, sizeof(kFallbackPrefix) - 1) == 0) {
            return true;
        }
    }
    return false;
}

void merge_into(ImFontAtlas* atlas, ImFont* font) {
    for (const FallbackFace& face : kFallbacks) {
        std::vector<std::uint8_t>& bytes = cached_font_bytes(face.file);
        if (bytes.empty()) continue;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.DstFont = font;
        cfg.FontDataOwnedByAtlas = false;
        cfg.FontNo = static_cast<ImU32>(face.face_index);
        std::snprintf(cfg.Name, sizeof(cfg.Name), "%s%s", kFallbackPrefix, face.file);
        atlas->AddFontFromMemoryTTF(bytes.data(), static_cast<int>(bytes.size()), 0.0F, &cfg);
    }
}

ImFont* or_default(ImFont* font) {
    if (font != nullptr || ImGui::GetCurrentContext() == nullptr) return font;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.FontDefault != nullptr) return io.FontDefault;
    return io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
}

}  // namespace

void attach_fallbacks() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas == nullptr || atlas->Locked) return;
    for (const facet::FontWeight weight :
         {facet::FontWeight::Regular, facet::FontWeight::Semibold, facet::FontWeight::Bold}) {
        for (const bool display : {false, true}) {
            ImFont* font = facet::WeightFont(weight, display);
            if (font != nullptr && !has_fallbacks(font)) merge_into(atlas, font);
        }
    }
}

ImFont* badge() { return or_default(facet::WeightFont(facet::FontWeight::Bold)); }

}  // namespace sam::ui::fonts
