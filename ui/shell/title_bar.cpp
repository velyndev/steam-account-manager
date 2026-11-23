#include <algorithm>
#include <cmath>
#include <string>

#include <windows.h>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/draw.h>
#include <facet/core/image.h>
#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/overlays.h>

#include "ui/icons.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/shell/shell.hpp"

namespace sam::ui::shell {

namespace {

constexpr char kAppName[] = "Steam Account Manager";
constexpr char kSearchPlaceholder[] = "Search accounts, actions and settings";
constexpr float kAppIconUnits = 20.0F;
// The search pill: 360 units wide from a 1100 unit window, narrower down to 280, and the icon
// button below 900, where the title, a centred pill and the right cluster no longer fit.
constexpr float kPillUnits = 360.0F;
constexpr float kPillMinUnits = 280.0F;
constexpr float kPillFromUnits = 900.0F;

// The search pill: a read-only Facet search field that looks like the field it opens, with the
// shortcut's key caps at its trailing end. Activating it (click, Enter, Space) opens the palette.
void search_pill(float width_units) {
    State& st = state();
    static float keycap_w = 0.0F;  // the key caps' width of the last frame, to place them
    ImGui::PushID("search-pill");
    static std::string empty;  // the pill never holds text: typing happens in the palette
    facet::SearchField("##search", &empty,
                       {.placeholder = kSearchPlaceholder, .width = width_units, .readOnly = true});
    if (ImGui::IsItemActivated() || ImGui::IsItemClicked()) st.palette_open = true;
    facet::ItemTooltip("Search accounts, screens, actions and settings");
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    facet::ShortcutInfo info;
    if (facet::GetShortcut(shortcuts().palette, &info) && info.primary != ImGuiKey_None && keycap_w >= 0.0F) {
        const float small = facet::ControlHeight(facet::ControlSize::Small);
        ImGui::SetCursorScreenPos(ImVec2(max.x - facet::Px(facet::T().field.padding) - keycap_w,
                                         std::floor((min.y + max.y - small) * 0.5F)));
        facet::KeyCap(info.primary, {.size = facet::ControlSize::Small});
        keycap_w = ImGui::GetItemRectSize().x;
    }
    ImGui::PopID();
}

// A mode the user turned on, shown while it is on; activating it opens its setting.
void mode_indicator(app::AppState& app, const char* label, facet::Icon icon, const char* tip,
                    screens::SettingsCategory category) {
    if (facet::Button(label, {.variant = facet::ButtonVariant::Plain, .icon = icon, .size = facet::ControlSize::Small})) {
        kit::open_settings(app, category);
    }
    facet::ItemTooltip("%s", tip);
}

// The right cluster: mode indicators (only while on), the privacy latch and the lock button,
// and the search icon button when the bar is too narrow for the pill.
void right_items(app::AppState& app, bool search_button) {
    State& st = state();
    const float gap = facet::Px(facet::T().spacing.xs);
    bool first = true;
    auto next = [&] {
        if (!first) ImGui::SameLine(0.0F, gap);
        first = false;
    };
    if (search_button) {
        next();
        if (facet::IconButton(kSearchPlaceholder, facet::Icon("search"))) st.palette_open = true;
    }
    const float lift =
        (facet::ControlHeight(facet::ControlSize::Inherit) - facet::ControlHeight(facet::ControlSize::Small)) * 0.5F;
    if (app.settings.safe_mode) {
        next();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + lift);
        mode_indicator(app, "Safe mode", icons::authenticator,
                       "Safe mode is on: the HWID spoofer, the cleaner and the CS2 loaders are hidden and "
                       "never run. Click to open the setting.",
                       screens::SettingsCategory::SecurityPrivacy);
    }
    if (app.settings.streamproof) {
        next();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + lift);
        mode_indicator(app, "Streamproof", facet::Icon("monitor"),
                       "Streamproof is on: screen recordings and streams show a blank window instead of "
                       "this app. Click to open the setting.",
                       screens::SettingsCategory::SecurityPrivacy);
    }
    next();
    const bool privacy = app.settings.privacy_mode;
    if (facet::IconButton(privacy ? "Privacy mode is on: logins are hidden. Turn it off###privacy"
                                  : "Turn privacy mode on: hide logins###privacy",
                          facet::Icon("eye-off"), {.selected = privacy})) {
        toggle_privacy(app);
    }
    next();
    if (facet::IconButton("Lock vault", facet::Icon("lock"))) lock_vault(app);
}

void handle_window_buttons(HWND hwnd, bool maximized) {
    switch (facet::TitleBarButtonPressed()) {
        case facet::WindowButton::Minimize:
            if (hwnd != nullptr) PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case facet::WindowButton::Maximize:
            if (hwnd != nullptr) PostMessageW(hwnd, WM_SYSCOMMAND, maximized ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case facet::WindowButton::Close:
            if (hwnd != nullptr) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case facet::WindowButton::None:
            break;
    }
}

}  // namespace

// Why two title bars side by side: Facet places a bar's items at its trailing end and reports
// the whole item group as client area. The first bar ends where the centred pill ends, so the
// pill is its only item; the second bar holds the right cluster and the window buttons. The
// free area of both stays the drag region, and the app icon is drawn in front of the first,
// because TitleBarOptions takes a glyph and the icon is a color texture.
void draw_title_bar(app::AppState& app) {
    const facet::Theme& t = facet::T();
    const HWND hwnd = app.main_hwnd;
    const bool maximized = hwnd != nullptr && IsZoomed(hwnd) != FALSE;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float full_w = ImGui::GetContentRegionAvail().x;
    const float full_units = full_w / facet::Scale();
    const float bar_h = facet::Px(t.containers.titleBarHeight);

    float lead = 0.0F;
    const float icon_px = facet::Px(kAppIconUnits);
    if (auto* icon = reinterpret_cast<ID3D11ShaderResourceView*>(icons::app_icon_for(icon_px))) {
        lead = facet::Px(t.spacing.md) + icon_px;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + facet::Px(t.spacing.md), origin.y + std::floor((bar_h - icon_px) * 0.5F)));
        facet::Image(icon, ImVec2(icon_px, icon_px), {.showStates = false});
    }

    const bool pill = app.unlocked && full_units >= kPillFromUnits;
    if (pill) {
        const float pill_units = std::clamp(kPillMinUnits + (full_units - kPillFromUnits) * 0.4F, kPillMinUnits, kPillUnits);
        const float pad = facet::Px(t.spacing.md);
        const float left_end = origin.x + (full_w + facet::Px(pill_units)) * 0.5F + pad;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + lead, origin.y));
        facet::BeginTitleBar(kAppName, {.width = (left_end - origin.x - lead) / facet::Scale()});
        search_pill(pill_units);
        facet::EndTitleBar();
        ImGui::SetCursorScreenPos(ImVec2(left_end, origin.y));
        facet::BeginTitleBar("##window", {.windowButtons = true,
                                          .maximized = maximized,
                                          .width = (origin.x + full_w - left_end) / facet::Scale()});
        right_items(app, false);
        facet::EndTitleBar();
    } else {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + lead, origin.y));
        facet::BeginTitleBar(kAppName, {.windowButtons = true,
                                        .maximized = maximized,
                                        .width = (full_w - lead) / facet::Scale()});
        if (app.unlocked) right_items(app, true);
        facet::EndTitleBar();
    }
    handle_window_buttons(hwnd, maximized);

    const float bottom = origin.y + bar_h;
    facet::HairlineH(ImGui::GetWindowDrawList(), origin.x, origin.x + full_w, bottom - facet::HairlineThickness(),
                     facet::ToU32(t.colors.separator));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
}

}  // namespace sam::ui::shell
