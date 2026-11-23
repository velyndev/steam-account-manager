#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/input.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "core/version.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/shell/shell.hpp"

namespace sam::ui::shell {

namespace {

enum class Kind { Screen, AddAccount, RefreshAll, Privacy, Lock, Shortcuts, Repository, Settings, Account };

struct Command {
    Kind kind;
    app::Screen screen = app::Screen::Accounts;
    screens::SettingsCategory category = screens::SettingsCategory::General;
    std::string account_id;
    std::string label;
    std::string detail;
    facet::Icon icon;
    const char* group = nullptr;
    bool disabled = false;
};

std::string chord_text(facet::ShortcutId id) {
    facet::ShortcutInfo info;
    if (!facet::GetShortcut(id, &info) || info.primary == ImGuiKey_None) return {};
    char buf[64];
    facet::ChordToText(info.primary, buf, sizeof(buf));
    return buf;
}

// Only real actions of the app: the screens, the global actions of the title bar and the
// sidebar, the Settings categories safe mode leaves visible, and every account of the vault.
std::vector<Command> build(const app::AppState& app) {
    const Shortcuts& s = shortcuts();
    std::vector<Command> c;
    auto screen = [&](app::Screen sc, facet::ShortcutId id) {
        if (!kit::screen_available(app, sc)) return;
        c.push_back({.kind = Kind::Screen,
                     .screen = sc,
                     .label = kit::screen_title(sc),
                     .detail = chord_text(id),
                     .icon = kit::screen_icon(sc),
                     .group = "Go to"});
    };
    screen(app::Screen::Accounts, s.accounts);
    screen(app::Screen::Authenticator, s.authenticator);
    screen(app::Screen::Confirmations, s.confirmations);
    screen(app::Screen::TradeOffers, s.trade_offers);
    screen(app::Screen::Cs2, s.cs2);
    screen(app::Screen::Settings, s.settings);

    c.push_back({.kind = Kind::AddAccount,
                 .label = "Add account",
                 .detail = chord_text(s.add_account),
                 .icon = facet::Icon("plus"),
                 .group = "Actions"});
    const bool can_refresh = can_refresh_all(app);
    c.push_back({.kind = Kind::RefreshAll,
                 .label = "Refresh all accounts",
                 .detail = can_refresh ? std::string() : std::string("Needs a Steam Web API key"),
                 .icon = facet::Icon("refresh"),
                 .group = "Actions",
                 .disabled = !can_refresh});
    c.push_back({.kind = Kind::Privacy,
                 .label = app.settings.privacy_mode ? "Turn privacy mode off" : "Turn privacy mode on",
                 .detail = chord_text(s.privacy),
                 .icon = facet::Icon("eye-off"),
                 .group = "Actions"});
    c.push_back({.kind = Kind::Lock,
                 .label = "Lock vault",
                 .detail = chord_text(s.lock),
                 .icon = facet::Icon("lock"),
                 .group = "Actions"});
    c.push_back({.kind = Kind::Shortcuts,
                 .label = "Keyboard shortcuts",
                 .detail = chord_text(s.shortcut_sheet),
                 .icon = facet::Icon("keyboard"),
                 .group = "Actions"});
    c.push_back({.kind = Kind::Repository,
                 .label = "Open the project page on GitHub",
                 .detail = std::string("Version ") + std::string(sam::kVersion),
                 .icon = facet::Icon("external-link"),
                 .group = "Actions"});

    for (const kit::SettingsCategoryInfo& cat : kit::settings_categories()) {
        if (kit::settings_category_hidden(app, cat.category)) continue;
        c.push_back({.kind = Kind::Settings,
                     .category = cat.category,
                     .label = std::string("Settings: ") + cat.title,
                     .icon = facet::Icon("settings"),
                     .group = "Settings"});
    }

    for (const core::Account& a : app.vault.accounts) {
        Command cmd{.kind = Kind::Account, .account_id = a.id, .group = "Accounts"};
        cmd.label = kit::display_name(app, a);
        if (!a.web.persona_name.empty() && !kit::login_hidden(app, a)) cmd.detail = a.login;
        cmd.icon = facet::Icon("user");
        c.push_back(std::move(cmd));
    }
    return c;
}

void run(app::AppState& app, const Command& cmd) {
    switch (cmd.kind) {
        case Kind::Screen: kit::go_to(app, cmd.screen); break;
        case Kind::AddAccount: kit::go_to(app, app::Screen::AddAccount); break;
        case Kind::RefreshAll: refresh_all(app); break;
        case Kind::Privacy: toggle_privacy(app); break;
        case Kind::Lock: lock_vault(app); break;
        case Kind::Shortcuts: state().sheet_open = true; break;
        case Kind::Repository: open_repository(); break;
        case Kind::Settings: kit::open_settings(app, cmd.category); break;
        case Kind::Account: kit::open_account(app, cmd.account_id); break;
    }
}

}  // namespace

void draw_command_palette(app::AppState& app) {
    State& st = state();
    if (!app.unlocked) {
        st.palette_open = false;
        st.sheet_open = false;
        return;
    }
    if (st.palette_open) {
        const std::vector<Command> commands = build(app);
        std::vector<facet::ChoiceItem> items;
        items.reserve(commands.size());
        for (const Command& cmd : commands) {
            items.push_back({.label = cmd.label.c_str(),
                             .icon = cmd.icon,
                             .detail = cmd.detail.empty() ? nullptr : cmd.detail.c_str(),
                             .group = cmd.group,
                             .disabled = cmd.disabled});
        }
        int picked = -1;
        const bool chosen = facet::CommandPalette("##command-palette", &st.palette_open, &picked, items,
                                                  {.placeholder = "Search accounts, screens and actions"});
#ifdef SAM_UI_CAPTURE
        if (st.pending_palette_frames >= 0 && --st.pending_palette_frames < 0) {
            ImGui::GetIO().AddInputCharactersUTF8(st.pending_palette_query.c_str());
        }
#endif
        if (chosen && picked >= 0 && picked < static_cast<int>(commands.size())) {
            run(app, commands[static_cast<std::size_t>(picked)]);
        }
    }
    facet::ShortcutSheet("Keyboard shortcuts", &st.sheet_open);
}

}  // namespace sam::ui::shell
