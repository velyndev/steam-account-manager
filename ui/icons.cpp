#include "ui/icons.hpp"

#include <windows.h>

#include <d3d11.h>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <iterator>
#include <vector>

#include "app/resource.h"

namespace sam::ui::icons {

namespace {

// The app's glyphs. Grid and stroke follow Facet's built-in set (docs/images-and-icons.md):
// 16 x 16 view box, stroke 1.5 in currentColor with round caps and joins, every coordinate
// between 1.75 and 14.25, fill only for dots. Original drawings, no brand marks.
#define SAM_GLYPH_OPEN                                                                            \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" " \
    "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.5\" stroke-linecap=\"round\" "       \
    "stroke-linejoin=\"round\">"
#define SAM_GLYPH_CLOSE "</svg>"

struct GlyphSource {
    GlyphInfo info;
    const char* svg;
};

const GlyphSource kGlyphs[] = {
    // Shield with a solid keyhole (a filled head and a tapered slot, so at 16 units it reads as
    // a lock, not as a map pin).
    {{"sam-authenticator", authenticator},
     SAM_GLYPH_OPEN
     "<path d=\"M8 1.75L13.25 3.5V7.75C13.25 10.9 11.1 13.2 8 14.25C4.9 13.2 2.75 10.9 2.75 7.75V3.5z\"/>"
     "<circle cx=\"8\" cy=\"6.6\" r=\"1.45\" fill=\"currentColor\" stroke=\"none\"/>"
     "<path d=\"M7.3 7.3h1.4l.45 3.2h-2.3z\" fill=\"currentColor\" stroke=\"none\"/>" SAM_GLYPH_CLOSE},
    // Phone with a check on its screen.
    {{"sam-confirmations", confirmations},
     SAM_GLYPH_OPEN "<rect x=\"4\" y=\"1.75\" width=\"8\" height=\"12.5\" rx=\"1.75\"/>"
     "<path d=\"M6.25 8.25l1.25 1.25 2.25-2.5\"/>" SAM_GLYPH_CLOSE},
    // Two opposed arrows.
    {{"sam-trade", trade},
     SAM_GLYPH_OPEN "<path d=\"M2.75 5.25h10M10.25 2.75l2.5 2.5-2.5 2.5\"/>"
     "<path d=\"M13.25 10.75h-10M5.75 8.25l-2.5 2.5 2.5 2.5\"/>" SAM_GLYPH_CLOSE},
    // Ring with four ticks and a center dot.
    {{"sam-crosshair", crosshair},
     SAM_GLYPH_OPEN "<circle cx=\"8\" cy=\"8\" r=\"5\"/>"
     "<path d=\"M8 1.75v3M8 11.25v3M1.75 8h3M11.25 8h3\"/>"
     "<circle cx=\"8\" cy=\"8\" r=\"1\" fill=\"currentColor\" stroke=\"none\"/>" SAM_GLYPH_CLOSE},
    // Key with two teeth.
    {{"sam-token", token},
     SAM_GLYPH_OPEN "<circle cx=\"5.25\" cy=\"10.75\" r=\"2.75\"/>"
     "<path d=\"M7.25 8.75l6-6M11.25 4.75l1.5 1.5M9.5 6.5l1.25 1.25\"/>" SAM_GLYPH_CLOSE},
    // Safe with a dial and two feet.
    {{"sam-vault", vault},
     SAM_GLYPH_OPEN "<rect x=\"2.25\" y=\"2.25\" width=\"11.5\" height=\"10\" rx=\"1.5\"/>"
     "<circle cx=\"8\" cy=\"7.25\" r=\"2.25\"/><path d=\"M4.75 12.25V14M11.25 12.25V14\"/>" SAM_GLYPH_CLOSE},
    // Circle with a slash.
    {{"sam-ban", ban},
     SAM_GLYPH_OPEN "<circle cx=\"8\" cy=\"8\" r=\"6\"/><path d=\"M3.75 12.25l8.5-8.5\"/>" SAM_GLYPH_CLOSE},
    // Two people, one in front of the other.
    {{"sam-group", group},
     SAM_GLYPH_OPEN "<circle cx=\"6\" cy=\"5.25\" r=\"2.5\"/><path d=\"M1.75 13.75a4.25 4.25 0 0 1 8.5 0\"/>"
     "<path d=\"M10.25 2.95a2 2 0 1 1 0 3.6M11.75 9.1a3.5 3.5 0 0 1 2.5 3.4\"/>" SAM_GLYPH_CLOSE},
    // A table: a frame with a header rule, a column rule and a row rule.
    {{"sam-table", table},
     SAM_GLYPH_OPEN "<rect x=\"2.25\" y=\"2.75\" width=\"11.5\" height=\"10.5\" rx=\"1.5\"/>"
     "<path d=\"M2.25 6.25h11.5M2.25 9.75h11.5M6.5 6.25v7\"/>" SAM_GLYPH_CLOSE},
    // Gift box with a bow.
    {{"sam-weekly-drop", weekly_drop},
     SAM_GLYPH_OPEN "<rect x=\"2.25\" y=\"5.25\" width=\"11.5\" height=\"3\" rx=\"0.75\"/>"
     "<path d=\"M3.25 8.25v5.5h9.5v-5.5M8 5.25v8.5\"/>"
     "<path d=\"M8 5.25C7 3 4.75 2.75 5 4.25 5.15 5 6.5 5.25 8 5.25 9.5 5.25 10.85 5 11 4.25 "
     "11.25 2.75 9 3 8 5.25z\"/>" SAM_GLYPH_CLOSE},
};

#undef SAM_GLYPH_OPEN
#undef SAM_GLYPH_CLOSE

GlyphInfo g_glyph_infos[std::size(kGlyphs)] = {};

// The app icon at the sizes its ICO carries, so it is drawn from a texture at least as large as
// the target (no mipmaps: a texture shrunk far below its size would alias).
constexpr int kAppIconSizes[] = {32, 64, 128, 256};
ID3D11ShaderResourceView* g_app_srv[std::size(kAppIconSizes)] = {};
ID3D11Texture2D* g_app_tex[std::size(kAppIconSizes)] = {};

bool load_app_icon_rgba(int size, std::vector<unsigned char>& rgba_out, UINT& w_out, UINT& h_out) {
    HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
    if (icon == nullptr) return false;

    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        DestroyIcon(icon);
        return false;
    }

    BITMAP bm{};
    GetObject(info.hbmColor, sizeof(bm), &bm);
    const UINT w = static_cast<UINT>(bm.bmWidth);
    const UINT h = static_cast<UINT>(bm.bmHeight);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(w);
    bi.bmiHeader.biHeight = -static_cast<LONG>(h);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    rgba_out.resize(static_cast<std::size_t>(w) * h * 4);
    HDC dc = GetDC(nullptr);
    const int scanned = GetDIBits(dc, info.hbmColor, 0, h, rgba_out.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    DestroyIcon(icon);
    if (scanned == 0) return false;

    bool any_alpha = false;
    for (std::size_t i = 0; i + 3 < rgba_out.size(); i += 4) {
        const unsigned char b = rgba_out[i];
        rgba_out[i] = rgba_out[i + 2];
        rgba_out[i + 2] = b;
        if (rgba_out[i + 3] != 0) any_alpha = true;
    }

    if (!any_alpha) {
        for (std::size_t i = 0; i + 3 < rgba_out.size(); i += 4) rgba_out[i + 3] = 255;
    }

    w_out = w;
    h_out = h;
    return true;
}

ID3D11ShaderResourceView* create_texture(ID3D11Device* device,
                                         const std::vector<unsigned char>& rgba, UINT w, UINT h,
                                         ID3D11Texture2D** out_tex) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba.data();
    init.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &tex))) {
        spdlog::warn("icons: CreateTexture2D failed");
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(device->CreateShaderResourceView(tex, nullptr, &srv))) {
        spdlog::warn("icons: CreateShaderResourceView failed");
        tex->Release();
        return nullptr;
    }
    *out_tex = tex;
    return srv;
}

}  // namespace

void register_glyphs() {
    for (std::size_t i = 0; i < std::size(kGlyphs); ++i) {
        const GlyphSource& g = kGlyphs[i];
        if (facet::FindIcon(g.info.name).IsValid()) continue;
        if (facet::RegisterIcon(g.info.name, g.svg) != g.info.icon) {
            spdlog::warn("icons: glyph {} did not register", g.info.name);
        }
    }
}

const GlyphInfo* glyphs(int* count) {
    for (std::size_t i = 0; i < std::size(kGlyphs); ++i) g_glyph_infos[i] = kGlyphs[i].info;
    if (count != nullptr) *count = static_cast<int>(std::size(kGlyphs));
    return g_glyph_infos;
}

void load(ID3D11Device* device) {
    register_glyphs();
    if (device == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < std::size(kAppIconSizes); ++i) {
        std::vector<unsigned char> rgba;
        UINT w = 0, h = 0;
        if (load_app_icon_rgba(kAppIconSizes[i], rgba, w, h)) {
            g_app_srv[i] = create_texture(device, rgba, w, h, &g_app_tex[i]);
        }
    }
}

void shutdown() {
    for (std::size_t i = 0; i < std::size(kAppIconSizes); ++i) {
        if (g_app_srv[i]) {
            g_app_srv[i]->Release();
            g_app_srv[i] = nullptr;
        }
        if (g_app_tex[i]) {
            g_app_tex[i]->Release();
            g_app_tex[i] = nullptr;
        }
    }
}

ImTextureID app_icon_for(float pixels) {
    ID3D11ShaderResourceView* best = nullptr;
    for (std::size_t i = 0; i < std::size(kAppIconSizes); ++i) {
        if (g_app_srv[i] == nullptr) continue;
        best = g_app_srv[i];
        if (static_cast<float>(kAppIconSizes[i]) >= pixels) break;
    }
    return reinterpret_cast<ImTextureID>(best);
}

}  // namespace sam::ui::icons
