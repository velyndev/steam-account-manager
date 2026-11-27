// Menus of the Accounts screen: the account and group context menus and the "Sign in to Steam"
// split button with its method menu, all drawn as Facet menu rows.
//
// No facet::Separator inside a menu: it takes the available width, and in a popup that sizes
// itself to its content that width grows with every frame (the cause of the 745 px wide context
// menu at scale 1.5). Menus are grouped with submenus instead.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "app/app_paths.hpp"
#include "app/gamesense_loader.hpp"
#include "app/luminary_loader.hpp"
#include "core/cs2/friend_code.hpp"
#include "core/hwid/hwid_gen.hpp"
#include "core/sda/totp.hpp"
#include "platform/clipboard.hpp"
#include "ui/icons.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::accounts {

namespace {

void save(app::AppState& state) {
    state.vault_dirty = true;
    state.save_vault_if_dirty();
}

// A wrapped line of secondary text inside a menu, at a fixed width so the menu keeps its size.
void menu_note(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true,
                             .width = facet::T().overlay.menuMinWidth + 40.0F});
}

}  // namespace

const char* login_method_badge(core::LoginMethod method) {
    switch (method) {
        case core::LoginMethod::LaunchCs2: return "launches CS2 after sign-in";
        case core::LoginMethod::LaunchCs2Gamesense: return "launches CS2 with gamesense after sign-in";
        case core::LoginMethod::LaunchCs2Luminary: return "launches CS2 with luminary after sign-in";
        case core::LoginMethod::Normal: break;
    }
    return nullptr;
}

const char* login_method_tooltip(core::LoginMethod method) {
    switch (method) {
        case core::LoginMethod::LaunchCs2: return "Signs in, then launches CS2.";
        case core::LoginMethod::LaunchCs2Gamesense: return "Signs in, launches CS2, then injects gamesense.";
        case core::LoginMethod::LaunchCs2Luminary: return "Signs in, launches CS2, then runs the luminary loader.";
        case core::LoginMethod::Normal: break;
    }
    return "Signs in to Steam only. The arrow next to it sets what happens after.";
}

// --- Account context menu -------------------------------------------------------------------------

namespace {

void copy_menu(app::AppState& state, core::Account& a) {
    const auto secs = std::chrono::seconds(state.settings.clipboard_clear_seconds);
    const bool has_sid = a.steam_id_64 != 0;
    if (!a.login.empty() && facet::MenuItem("Login")) platform::clipboard::set_text(a.login);
    if (!a.password.empty() && facet::MenuItem("Password")) {
        platform::clipboard::set_text_with_auto_clear(std::string_view(a.password.c_str(), a.password.size()), secs);
    }
    if (a.sda.has_value() && !a.sda->shared_secret.empty() && facet::MenuItem("Steam Guard code")) {
        const std::string code = sda::generate_code_now(a.sda->shared_secret);
        if (!code.empty()) platform::clipboard::set_text_with_auto_clear(code, secs);
    }
    if (has_sid) {
        char sid[24];
        std::snprintf(sid, sizeof(sid), "%llu", static_cast<unsigned long long>(a.steam_id_64));
        if (facet::MenuItem("SteamID64")) platform::clipboard::set_text(sid);
        if (facet::MenuItem("Profile link")) {
            platform::clipboard::set_text(std::string("https://steamcommunity.com/profiles/") + sid);
        }
        if (facet::MenuItem("CS2 friend code")) {
            const std::string code = cs2::friend_code(a.steam_id_64);
            if (!code.empty()) platform::clipboard::set_text(code);
        }
    }
    const bool has_trade_link = a.trade_url.has_value() && !a.trade_url->empty();
    const bool can_fetch_trade = has_sid && (!a.refresh_token.empty() || !a.password.empty());
    if ((has_trade_link || can_fetch_trade) && facet::MenuItem("Trade link")) state.copy_trade_link(a.id);
}

void hwid_menu(app::AppState& state, core::Account& a) {
    const bool enabled = a.hwid.has_value();
    const bool always = state.settings.hwid.always_spoof;
    if (always && !a.hwid_excluded && facet::MenuItem("Exclude from spoofing")) {
        a.hwid_excluded = true;
        save(state);
    }
    if (always && a.hwid_excluded && facet::MenuItem("Include in spoofing")) {
        a.hwid_excluded = false;
        save(state);
    }
    if (!always && !enabled && facet::MenuItem("Enable")) {
        a.hwid = core::hwid::generate_profile();
        save(state);
    }
    // Also offered before an account's first launch, so a newly added account can get its
    // profile up front. Hidden while every launch generates one anyway.
    if (hwid_spoofed(state, a) && !state.settings.hwid.new_profile_every_launch &&
        facet::MenuItem(enabled ? "Generate a new profile" : "Generate a profile")) {
        a.hwid = core::hwid::generate_profile();
        save(state);
    }
    if (!always && enabled && facet::MenuItem("Disable")) {
        a.hwid = std::nullopt;
        save(state);
    }
}

}  // namespace

void account_menu_items(app::AppState& state, core::Account& a) {
    const bool has_sid = a.steam_id_64 != 0;
    if (facet::BeginMenu("Copy", {.icon = facet::Icon("copy")})) {
        copy_menu(state, a);
        facet::EndMenu();
    }
    const bool can_browser = has_sid && !a.is_nfa && (!a.refresh_token.empty() || !a.password.empty());
    if (can_browser && facet::MenuItem("Open in browser, signed in", {.icon = facet::Icon("external-link")})) {
        state.open_account_in_browser(a);
    }
    if (a.sda.has_value() && facet::MenuItem("Open in Authenticator", {.icon = icons::authenticator})) {
        state.selected_account_id = a.id;
        kit::go_to(state, app::Screen::Authenticator);
    }
    if (facet::MenuItem("Edit account", {.icon = facet::Icon("edit")})) request_edit(state, a);
    if (facet::MenuItem("Edit notes", {.icon = facet::Icon("file")})) request_notes(state, a);
    if (has_sid && facet::MenuItem("Change Steam name", {.icon = facet::Icon("user")})) request_username(state, a);
    if (facet::BeginMenu("Move to group", {.icon = icons::group})) {
        int current = -1;
        for (std::size_t i = 0; i < state.vault.groups.size(); ++i) {
            if (state.vault.groups[i].id == a.group_id) current = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < state.vault.groups.size(); ++i) {
            const core::Group& g = state.vault.groups[i];
            ImGui::PushID(g.id.c_str());
            if (facet::MenuRadioItem(g.name.c_str(), &current, static_cast<int>(i))) move_to_group(state, {a.id}, g.id);
            ImGui::PopID();
        }
        if (facet::MenuRadioItem("No group", &current, -1)) move_to_group(state, {a.id}, std::string());
        if (facet::MenuItem("New group...")) request_new_group();
        facet::EndMenu();
    }
    if (has_sid) {
        const char* cfg_label = nullptr;
        bool cfg_ready = false;
        switch (state.settings.cs2_video.mode) {
            case app::CS2ConfigMode::VideoTxt:
                cfg_label = "Add video config";
                cfg_ready = std::filesystem::exists(app::cs2_video_template_path());
                break;
            case app::CS2ConfigMode::Folder730:
                cfg_label = "Apply 730 folder";
                cfg_ready = std::filesystem::is_directory(app::cs2_730_template_dir());
                break;
            case app::CS2ConfigMode::UserdataFolder:
                cfg_label = "Apply game folders";
                cfg_ready = std::filesystem::is_directory(app::userdata_template_dir());
                break;
            case app::CS2ConfigMode::None: break;
        }
        if (cfg_label != nullptr && cfg_ready && facet::MenuItem(cfg_label, {.icon = facet::Icon("monitor")})) {
            state.apply_cs2_video_config(a);
        }
    }
    if (a.is_nfa && has_sid && facet::MenuItem("Check token and refresh CS2 data", {.icon = icons::token})) {
        state.queue_gc_validate(a.id);
    }
    if (facet::BeginMenu("Weekly drop", {.icon = icons::weekly_drop})) {
        const std::int64_t now = now_seconds();
        const bool claimed = a.cs2.weekly_drop_reset_unix > now;
        if (!claimed && facet::MenuItem("Mark as claimed")) {
            a.cs2.weekly_drop_reset_unix = next_weekly_reset(now);
            save(state);
        }
        if (claimed && facet::MenuItem("Mark as not claimed")) {
            a.cs2.weekly_drop_reset_unix = 0;
            save(state);
        }
        facet::EndMenu();
    }
    if (!state.settings.safe_mode && facet::BeginMenu("HWID spoofer", {.icon = facet::Icon("sliders")})) {
        hwid_menu(state, a);
        facet::EndMenu();
    }
    if (facet::MenuItem("Export...", {.icon = facet::Icon("upload")})) request_export({a.id});
    if (facet::MenuItem("Remove account...", {.icon = facet::Icon("trash"), .destructive = true})) request_remove(state, a.id);
}

// --- Group context menu ---------------------------------------------------------------------------

void group_menu_items(app::AppState& state, const core::Group& group, const std::vector<std::size_t>& members) {
    if (facet::MenuItem("Rename and recolor...", {.icon = facet::Icon("edit")})) request_edit_group(group);
    if (!state.settings.safe_mode && state.settings.hwid.always_spoof &&
        facet::BeginMenu("HWID spoofer", {.icon = facet::Icon("sliders")})) {
        if (facet::MenuItem("Exclude all from spoofing")) {
            for (std::size_t idx : members) {
                state.vault.accounts[idx].hwid_excluded = true;
                state.vault.accounts[idx].hwid = std::nullopt;
            }
            save(state);
        }
        if (facet::MenuItem("Include all in spoofing")) {
            for (std::size_t idx : members) state.vault.accounts[idx].hwid_excluded = false;
            save(state);
        }
        facet::EndMenu();
    }
    if (facet::MenuItem("Delete group...", {.icon = facet::Icon("trash"), .destructive = true})) {
        request_delete_group(group.id);
    }
}

// --- Sign-in split button --------------------------------------------------------------------------

namespace {

// What happens after Steam is up (persisted per account), then, for password accounts, the
// session-only way of signing in as a submenu, named by what it does.
void sign_in_menu(app::AppState& state, core::Account& a) {
    const core::LoginMethod eff = core::effective_login_method(a.login_method, state.settings.safe_mode);
    facet::StyledText("After signing in", {.style = facet::TextStyle::Footnote, .secondary = true});
    int method = static_cast<int>(eff);
    auto set_method = [&](core::LoginMethod m) {
        a.login_method = m;
        save(state);
    };
    if (facet::MenuRadioItem("Stay in Steam", &method, static_cast<int>(core::LoginMethod::Normal))) {
        set_method(core::LoginMethod::Normal);
    }
    if (facet::MenuRadioItem("Launch CS2", &method, static_cast<int>(core::LoginMethod::LaunchCs2))) {
        set_method(core::LoginMethod::LaunchCs2);
    }
    if (!state.settings.safe_mode) {
        int stored = static_cast<int>(a.login_method);
        if (facet::MenuRadioItem("Launch CS2 with gamesense", &stored,
                                 static_cast<int>(core::LoginMethod::LaunchCs2Gamesense))) {
            if (app::gamesense_loader_path()) {
                set_method(core::LoginMethod::LaunchCs2Gamesense);
            } else {
                state.gamesense_pick_request = a.id;
            }
        }
        if (facet::MenuRadioItem("Launch CS2 with luminary", &stored,
                                 static_cast<int>(core::LoginMethod::LaunchCs2Luminary))) {
            if (app::luminary_loader_path()) {
                set_method(core::LoginMethod::LaunchCs2Luminary);
            } else {
                state.luminary_pick_request = a.id;
            }
        }
    }
    if (a.is_nfa) return;

    if (facet::BeginMenu("How to sign in this session")) {
        const app::SignInMethod def = state.settings.sign_in_method;
        int current = static_cast<int>(state.effective_sign_in_method(a.id));
        const bool has_password = !a.password.empty();
        struct Entry {
            const char* label;
            const char* label_default;
            app::SignInMethod method;
            bool capable;
            const char* blocked;
        };
        const Entry entries[] = {
            {"Type the password into Steam", "Type the password into Steam (default)", app::SignInMethod::Driver,
             has_password, "Needs a saved password."},
            {"Sign in without the login window", "Sign in without the login window (default)",
             app::SignInMethod::TokenInject, has_password || app::cm_token_valid(a),
             "Needs a saved password or a sign-in token."},
        };
        for (const Entry& e : entries) {
            const bool is_default = e.method == def;
            ImGui::BeginDisabled(!e.capable);
            if (facet::MenuRadioItem(is_default ? e.label_default : e.label, &current, static_cast<int>(e.method))) {
                if (is_default) {
                    state.sign_in_override.erase(a.id);
                } else {
                    state.sign_in_override[a.id] = e.method;
                }
            }
            ImGui::EndDisabled();
            if (!e.capable) facet::ItemTooltip("%s", e.blocked);
        }
        menu_note("Until the app restarts. The default is in Settings, Sign-in and launch.");
        facet::EndMenu();
    }
}

}  // namespace

// Why a Primary button plus a chevron icon button: Facet has no split button, and the two
// joined controls keep the sign-in action one click while the method menu stays one step away.
void sign_in_button(app::AppState& state, core::Account& a) {
    ScreenState& s = screen();
    const bool cleaning = state.cleaner_busy.load();
    const bool minting = state.pending_token_launch.minting && state.pending_token_launch.account_id == a.id;
    const core::LoginMethod eff = core::effective_login_method(a.login_method, state.settings.safe_mode);

    ImGui::BeginGroup();
    ImGui::BeginDisabled(cleaning);
    if (facet::Button(minting ? "Preparing sign-in###sign-in" : "Sign in to Steam###sign-in",
                      {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("play"), .loading = minting})) {
        sign_in(state, a);
    }
    ImGui::EndDisabled();
    if (cleaning) {
        facet::ItemTooltip("The cleaner is running. A sign-in now would be undone by it.");
    } else {
        facet::ItemTooltip("%s", login_method_tooltip(eff));
    }
    ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.xs) * 0.5F);
    if (facet::IconButton("What happens after signing in", facet::Icon("chevron-down"),
                          {.variant = facet::ButtonVariant::Primary, .selected = s.signin_menu_open})) {
        s.signin_menu_open = !s.signin_menu_open;
    }
#ifdef SAM_UI_CAPTURE
    if (capture_force() == CaptureForce::SignInMenu) s.signin_menu_open = true;
#endif
    // The menu opens upward when the room under the button is shorter than the menu (a short
    // window, a large UI scale); the popover alone keeps the side it was given.
    const float rows = (state.settings.safe_mode ? 3.0F : 5.0F) + (a.is_nfa ? 0.0F : 1.0F);
    const float needed = facet::Px(facet::T().list.rowHeight) * rows + facet::Px(48.0F);
    const float below = ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y -
                        ImGui::GetItemRectMax().y;
    const bool up = below < needed && ImGui::GetItemRectMin().y - ImGui::GetMainViewport()->WorkPos.y > below;
    if (facet::BeginPopover("##sign-in-menu", &s.signin_menu_open,
                            {.placement = up ? facet::PopoverPlacement::Above : facet::PopoverPlacement::Below,
                             .arrow = false})) {
        ImGui::PushID(a.id.c_str());
        sign_in_menu(state, a);
        ImGui::PopID();
        facet::EndPopover();
    }
    ImGui::EndGroup();
}

}  // namespace sam::ui::screens::accounts
