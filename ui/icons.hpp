#pragma once

#include <imgui.h>

#include <facet/core/icons.h>

struct ID3D11Device;

namespace sam::ui::icons {

// Loads the app icon (IDI_APP_ICON, at 32, 64, 128 and 256 px) as textures and registers the
// app's own glyphs with Facet (register_glyphs). Call once after the device exists; shutdown
// releases the textures. The glyph registrations survive shutdown, as Facet's icon registry does.
void load(ID3D11Device* device);
void shutdown();

// The app icon texture for drawing at `pixels` (physical pixels, Px(units)): the smallest loaded
// size at least that large, else the largest; 0 when the icon failed to load. The icon is a
// flat mark on its own dark tile: draw it as it is, with no frame or background behind it.
ImTextureID app_icon_for(float pixels);

// Registers the glyphs below with facet::RegisterIcon. Safe to call more than once and before
// facet::Init. load() calls it; only the capture harness needs it on its own.
void register_glyphs();

// The app's own glyphs, drawn in the grid of Facet's built-in set (16 x 16 view box, 1.5 unit
// round stroke, single color). Each is a facet::Icon usable wherever Facet takes an icon; they
// draw as Facet's crossed placeholder until register_glyphs ran. Use Facet's built-in names
// ("user", "lock", "settings", "refresh", "clock", "eye-off" ...) where one fits; these exist
// only for what the built-in set lacks.
//
//   authenticator  shield with a keyhole: Steam Guard, the Authenticator screen
//   confirmations  phone with a check: mobile confirmations waiting for a decision
//   trade          two opposed arrows: trade offers, a swap between two accounts
//   crosshair      crosshair: CS2 and its Game Coordinator
//   token          key: sign-in tokens (NFA, cached, the CM token), revocation codes
//   vault          safe with a dial: a vault, the vault switcher
//   ban            circle with a slash: a ban of any kind
//   group          two people: an account group
//   weekly_drop    gift box: the CS2 weekly drop
//   table          a frame with rows and a column: a table view (the list/table switch), not a grid
inline constexpr facet::Icon authenticator{"sam-authenticator"};
inline constexpr facet::Icon confirmations{"sam-confirmations"};
inline constexpr facet::Icon trade{"sam-trade"};
inline constexpr facet::Icon crosshair{"sam-crosshair"};
inline constexpr facet::Icon token{"sam-token"};
inline constexpr facet::Icon vault{"sam-vault"};
inline constexpr facet::Icon ban{"sam-ban"};
inline constexpr facet::Icon group{"sam-group"};
inline constexpr facet::Icon weekly_drop{"sam-weekly-drop"};
inline constexpr facet::Icon table{"sam-table"};

// Every app glyph with its registered name, for the icon sheet capture and diagnostics.
struct GlyphInfo {
    const char* name;
    facet::Icon icon;
};
const GlyphInfo* glyphs(int* count);

}  // namespace sam::ui::icons
