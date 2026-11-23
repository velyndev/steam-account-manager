#include "ui/shell/shell.hpp"

#include <imgui.h>

#include "platform/paths.hpp"
#include "ui/kit/navigation.hpp"

namespace sam::ui::shell {

namespace {

State g_state;

facet::ShortcutId add(const char* name, ImGuiKeyChord chord, const char* label, const char* group) {
    return facet::RegisterShortcut(name, chord, {.label = label, .group = group});
}

}  // namespace

const Shortcuts& shortcuts() {
    static const Shortcuts s = [] {
        Shortcuts r;
        r.palette = add("sam.palette", ImGuiMod_Ctrl | ImGuiKey_K, "Search accounts, screens and actions", "General");
        r.shortcut_sheet = add("sam.shortcuts", ImGuiMod_Ctrl | ImGuiKey_Slash, "Show keyboard shortcuts", "General");
        r.lock = add("sam.lock", ImGuiMod_Ctrl | ImGuiKey_L, "Lock the vault", "General");
        r.privacy = add("sam.privacy", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, "Turn privacy mode on or off",
                        "General");
        r.accounts = add("sam.go.accounts", ImGuiMod_Ctrl | ImGuiKey_1, "Go to Accounts", "Go to");
        r.authenticator = add("sam.go.authenticator", ImGuiMod_Ctrl | ImGuiKey_2, "Go to Authenticator", "Go to");
        r.confirmations = add("sam.go.confirmations", ImGuiMod_Ctrl | ImGuiKey_3, "Go to Confirmations", "Go to");
        r.trade_offers = add("sam.go.trade-offers", ImGuiMod_Ctrl | ImGuiKey_4, "Go to Trade offers", "Go to");
        r.cs2 = add("sam.go.cs2", ImGuiMod_Ctrl | ImGuiKey_5, "Go to CS2", "Go to");
        r.add_account = add("sam.go.add-account", ImGuiMod_Ctrl | ImGuiKey_N, "Add an account", "Go to");
        r.settings = add("sam.go.settings", ImGuiMod_Ctrl | ImGuiKey_Comma, "Open Settings", "Go to");
        return r;
    }();
    return s;
}

State& state() {
    return g_state;
}

void reset_state() {
    g_state = State{};
}

// Why every shortcut is asked for each frame: Facet grants a chord's route from the frame
// after it was asked for, so a shortcut skipped in one frame would miss its key in the next.
void handle_shortcuts(app::AppState& app) {
    const Shortcuts& s = shortcuts();
    const bool on = app.unlocked;
    facet::SetShortcutEnabled(s.palette, on);
    facet::SetShortcutEnabled(s.shortcut_sheet, on);
    facet::SetShortcutEnabled(s.lock, on);
    facet::SetShortcutEnabled(s.privacy, on);
    facet::SetShortcutEnabled(s.accounts, on);
    facet::SetShortcutEnabled(s.authenticator, on);
    facet::SetShortcutEnabled(s.confirmations, on);
    facet::SetShortcutEnabled(s.trade_offers, on);
    facet::SetShortcutEnabled(s.cs2, on && kit::screen_available(app, app::Screen::Cs2));
    facet::SetShortcutEnabled(s.add_account, on);
    facet::SetShortcutEnabled(s.settings, on);

    State& st = state();
    if (facet::ShortcutPressed(s.palette)) st.palette_open = true;
    if (facet::ShortcutPressed(s.shortcut_sheet)) st.sheet_open = true;
    if (facet::ShortcutPressed(s.privacy)) toggle_privacy(app);
    if (facet::ShortcutPressed(s.accounts)) kit::go_to(app, app::Screen::Accounts);
    if (facet::ShortcutPressed(s.authenticator)) kit::go_to(app, app::Screen::Authenticator);
    if (facet::ShortcutPressed(s.confirmations)) kit::go_to(app, app::Screen::Confirmations);
    if (facet::ShortcutPressed(s.trade_offers)) kit::go_to(app, app::Screen::TradeOffers);
    if (facet::ShortcutPressed(s.cs2)) kit::go_to(app, app::Screen::Cs2);
    if (facet::ShortcutPressed(s.add_account)) kit::go_to(app, app::Screen::AddAccount);
    if (facet::ShortcutPressed(s.settings)) kit::go_to(app, app::Screen::Settings);
    if (facet::ShortcutPressed(s.lock)) lock_vault(app);
}

void lock_vault(app::AppState& app) {
    if (!app.unlocked) return;
    // A pending vault write goes out under the master password before lock_vault forgets it.
    app.flush_pending_save();
    app.lock_vault();
    State& st = state();
    st.palette_open = false;
    st.sheet_open = false;
    st.vault_menu_open = false;
}

// The same as the Settings screen's privacy checkbox (settings.privacy_mode, then
// clear_session_secrets), plus a save, because the title bar has no Save button of its own.
void toggle_privacy(app::AppState& app) {
    app.settings.privacy_mode = !app.settings.privacy_mode;
    app.clear_session_secrets();
    app.save_settings();
}

bool can_refresh_all(const app::AppState& app) {
    return !app.settings.web_api_key.empty();
}

void refresh_all(app::AppState& app) {
    if (can_refresh_all(app)) app.refresh_account_data(false, true);
}

std::string active_vault_name(const app::AppState& app) {
    if (const app::VaultInfo* v = app.vault_registry.find(platform::active_vault_id())) return v->name;
    return {};
}

}  // namespace sam::ui::shell
