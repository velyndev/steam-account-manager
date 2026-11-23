#include <algorithm>
#include <filesystem>
#include <string>

#include <imgui.h>

#include <facet/core/image.h>
#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/media.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "app/vault_registry.hpp"
#include "platform/paths.hpp"
#include "ui/icons.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/standing.hpp"
#include "ui/shell/shell.hpp"
#include "ui/util.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::shell {

namespace {

constexpr float kVaultAvatarUnits = 32.0F;

void nav_item(app::AppState& app, app::Screen screen, int badge = 0) {
    facet::SidebarItemOptions options;
    options.selected = app.current_screen == screen;
    options.badge = badge;
    // In the rail the count sits on the icon's corner: a solid accent disc reads as a count there,
    // a grey one reads as a stray superscript. Beside a label the quiet grey count is enough.
    options.badgeTone = state().rail ? facet::Tone::Accent : facet::Tone::Neutral;
    if (facet::SidebarItem(kit::screen_title(screen), kit::screen_icon(screen), options)) kit::go_to(app, screen);
}

const app::VaultInfo* active_vault(const app::AppState& app) {
    return app.vault_registry.find(platform::active_vault_id());
}

// The vault's own mark: its custom picture when it has one, else Facet's initials avatar ringed
// in the vault's color (the accent pushed to that color), so the vault reads as itself and not
// as a generic menu. Returns true when activated (with `activatable`).
bool vault_avatar(const app::VaultInfo* vault, float diameter_units, bool activatable) {
    const std::string name = vault != nullptr ? vault->name : std::string("Vault");
    if (vault != nullptr && !vault->icon_file.empty()) {
        const std::filesystem::path file = app::vault_dir_for(vault->id) / vault->icon_file;
        if (auto* srv = widgets::texture_for_file(to_utf8(file.wstring()))) {
            const float d = facet::Px(diameter_units);
            facet::Image(srv, ImVec2(d, d), {.fit = facet::ImageFit::Cover, .radius = d * 0.5F, .showStates = false});
            const bool clicked = activatable && ImGui::IsItemClicked();
            facet::ItemTooltip("%s", name.c_str());
            return clicked;
        }
    }
    facet::PushAccent(kit::data_color(vault != nullptr ? vault->color_rgba : 0x4F8EF7FFu));
    const bool activated = facet::Avatar((name + "###vault-avatar").c_str(), facet::ImageHandle{},
                                         {.selected = true, .activatable = activatable, .diameter = diameter_units});
    facet::PopAccent();
    return activated;
}

// The rows of the switcher popover: every vault (the active one marked), then the entry that
// opens Settings on the vaults category.
void vault_menu(app::AppState& app) {
    State& st = state();
    const std::string active = platform::active_vault_id();
    facet::Text(facet::TextStyle::Footnote, "Switch vault");
    for (const app::VaultInfo& v : app.vault_registry.vaults) {
        ImGui::PushID(v.id.c_str());
        int selection = v.id == active ? 0 : -1;
        if (facet::MenuRadioItem(v.name.c_str(), &selection, 0, {.icon = icons::vault})) {
            st.vault_menu_open = false;
            if (v.id != active) kit::switch_vault(app, v.id);
        }
        ImGui::PopID();
    }
    facet::Separator();
    if (facet::MenuItem("Manage vaults", {.icon = facet::Icon("settings")})) {
        st.vault_menu_open = false;
        kit::open_settings(app, screens::SettingsCategory::Vaults);
    }
}

// Why an avatar and a name instead of a combo: the top of the navigation answers "which vault
// am I in" at a glance, in the vault's own color; switching is the less frequent action, so it
// sits behind the small chevron button (and the avatar) as a popover.
void vault_switcher(app::AppState& app, bool rail) {
    State& st = state();
    const app::VaultInfo* vault = active_vault(app);
    const bool several = app.vault_registry.vaults.size() > 1;
    if (rail) {
        const float d = facet::Px(kVaultAvatarUnits);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - d) * 0.5F));
        if (vault_avatar(vault, kVaultAvatarUnits, several)) st.vault_menu_open = !st.vault_menu_open;
    } else {
        const std::string name = vault != nullptr ? vault->name : std::string("Vault");
        const int count = static_cast<int>(app.vault_registry.vaults.size());
        char sub[48];
        if (count > 1) {
            std::snprintf(sub, sizeof(sub), "Vault, %d in total", count);
        } else {
            std::snprintf(sub, sizeof(sub), "Vault");
        }
        facet::BeginStack("##vault-row", {.orientation = facet::Orientation::Horizontal,
                                          .gap = facet::Gap::Item,
                                          .align = facet::LayoutAlign::Center,
                                          .width = facet::AvailableWidthUnits()});
        if (vault_avatar(vault, kVaultAvatarUnits, several)) st.vault_menu_open = !st.vault_menu_open;
        const float name_w = std::max(1.0F, facet::AvailableWidthUnits() -
                                                (several ? facet::T().metrics.controlHeight + facet::T().spacing.sm : 0.0F));
        facet::BeginStack("##vault-names", {.gap = facet::Gap::None, .width = name_w});
        facet::TruncatedText(name.c_str(), {.style = facet::TextStyle::Headline});
        facet::StyledText(sub, {.style = facet::TextStyle::Footnote, .secondary = true});
        facet::EndStack();
        if (several) {
            facet::StackSpacer();
            if (facet::IconButton("Switch vault", facet::Icon("chevrons-up-down"), {.selected = st.vault_menu_open})) {
                st.vault_menu_open = !st.vault_menu_open;
            }
        }
        facet::EndStack();
    }
    if (several && facet::BeginPopover("##vault-menu", &st.vault_menu_open,
                                       {.placement = facet::PopoverPlacement::Below, .arrow = false, .width = 220.0F})) {
        vault_menu(app);
        facet::EndPopover();
    }
}

}  // namespace

// Why this order: the vault frames everything below it, Accounts is the home screen, the two
// Steam Guard screens and the two trading screens are grouped by what the user is doing, and
// the bottom block holds what is visited rather than lived in (Add account, Settings) under
// what is happening right now (background jobs, an update), which appear only when they exist.
void draw_sidebar(app::AppState& app, float height) {
    const facet::Theme& t = facet::T();
    State& st = state();
    const bool rail = st.rail;
    const facet::SidebarMode mode = rail ? facet::SidebarMode::Rail : facet::SidebarMode::Expanded;
    if (!facet::BeginSidebar("##nav", {.mode = mode, .height = height / facet::Scale()})) return;

    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
    vault_switcher(app, rail);
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.md)));

    nav_item(app, app::Screen::Accounts);
    if (facet::BeginSidebarSection("Steam Guard")) {
        nav_item(app, app::Screen::Authenticator);
        nav_item(app, app::Screen::Confirmations, app.pending_confirmations_count.load(std::memory_order_relaxed));
        facet::EndSidebarSection();
    }
    if (facet::BeginSidebarSection("Trading")) {
        nav_item(app, app::Screen::TradeOffers, app.pending_trade_offers_count.load(std::memory_order_relaxed));
        if (kit::screen_available(app, app::Screen::Cs2)) nav_item(app, app::Screen::Cs2);
        facet::EndSidebarSection();
    }

    // The bottom block, held at the bottom of the column with its height from the last frame.
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float free = ImGui::GetContentRegionAvail().y - st.pinned_height;
    if (free > spacing) ImGui::Dummy(ImVec2(0.0F, free - spacing));
    const float top = ImGui::GetCursorPosY();

    const bool activity = draw_activity(app, rail);
    const bool update = draw_update_notice(app, rail);
    if (activity || update) ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
    if (rail) {
        const float control = facet::ControlHeight(facet::ControlSize::Inherit);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - control) * 0.5F));
        if (facet::IconButton("Add account", facet::Icon("plus"), {.variant = facet::ButtonVariant::Secondary})) {
            kit::go_to(app, app::Screen::AddAccount);
        }
    } else if (facet::Button("Add account", {.icon = facet::Icon("plus"), .width = facet::kFill})) {
        kit::go_to(app, app::Screen::AddAccount);
    }
    // A button and a list row are different kinds of control: without a gap the row's hover
    // fill touches the button's edge.
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
    nav_item(app, app::Screen::Settings);
    st.pinned_height = ImGui::GetCursorPosY() - top + facet::Px(t.spacing.xs);
    facet::EndSidebar();
}

}  // namespace sam::ui::shell
