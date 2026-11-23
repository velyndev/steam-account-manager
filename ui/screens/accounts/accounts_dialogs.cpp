// The dialogs of the Accounts screen. Each is a Facet layer opened by its flag in
// ScreenState (or, for the sign-in error, by a non-empty state.launch_error) and drawn every
// frame from one place, so it keeps its place in the id stack.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/pickers.h>

#include "app/cleaner_runner.hpp"
#include "app/job_pump.hpp"
#include "core/profile/edit.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/screens/add_account_screen.hpp"
#include "ui/util.hpp"
#include "ui/widgets/bundle_dialogs.hpp"

namespace sam::ui::screens::accounts {

namespace {

void save(app::AppState& state) {
    state.vault_dirty = true;
    state.save_vault_if_dirty();
}

// The trailing button row of a dialog.
void begin_actions(const char* id) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    facet::BeginStack(id, {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
}

void end_actions() {
    facet::EndStack();
}

std::string make_group_id() {
    static std::atomic<std::uint64_t> counter{0};
    const auto n = counter.fetch_add(1);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "g%016llx%08llx", static_cast<unsigned long long>(now_seconds()),
                  static_cast<unsigned long long>(n));
    return buf;
}

std::uint32_t pack_rgba(const ImVec4& c) {
    auto to_byte = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.0F, 1.0F) * 255.0F + 0.5F); };
    return (to_byte(c.x) << 24) | (to_byte(c.y) << 16) | (to_byte(c.z) << 8) | 0xFFu;
}

// --- Remove ---------------------------------------------------------------------------------------

// Why a dialog of its own and not kit::destructive_dialog: the message suggests exporting first,
// so the dialog offers that as a button next to Cancel.
void remove_dialog(app::AppState& state) {
    ScreenState& s = screen();
    if (!facet::BeginModal("Remove account?", &s.remove_open, {.closeButton = false})) return;
    std::string message = "This account";
    if (const core::Account* a = state.find_account(s.remove_id)) message = kit::display_name(state, *a);
    message += " is removed from this vault together with its saved password, sign-in tokens and Steam Guard "
               "secrets. This cannot be undone, so export it first if you may need it again.";
    facet::StyledText(message.c_str(), {.wrap = true});
    begin_actions("##remove-actions");
    if (facet::Button("Export first...", {.icon = facet::Icon("upload")})) {
        request_export({s.remove_id});
        s.remove_open = false;
    }
    if (facet::Button("Cancel")) s.remove_open = false;
    if (facet::Button("Remove account", {.variant = facet::ButtonVariant::Destructive, .icon = facet::Icon("trash")})) {
        state.remove_accounts({s.remove_id});
        if (state.selected_account_id == s.remove_id) state.selected_account_id.clear();
        save(state);
        s.remove_id.clear();
        s.remove_open = false;
    }
    end_actions();
    facet::EndModal();
}

void bulk_remove_dialog(app::AppState& state) {
    ScreenState& s = screen();
    const std::size_t n = state.selected_account_ids.size();
    std::string names;
    std::size_t listed = 0;
    for (const core::Account& a : state.vault.accounts) {
        if (state.selected_account_ids.count(a.id) == 0) continue;
        if (listed == 5) break;
        if (listed > 0) names += ", ";
        names += kit::display_name(state, a);
        ++listed;
    }
    char head[96];
    std::snprintf(head, sizeof(head), "%zu accounts are removed from this vault together with their secrets: ", n);
    std::string message = head + names;
    if (n > listed) message += " and " + std::to_string(n - listed) + " more";
    message += ". This cannot be undone.";
    char confirm[48];
    std::snprintf(confirm, sizeof(confirm), "Remove %zu accounts", n);
    char title[48];
    std::snprintf(title, sizeof(title), "Remove %zu accounts?###bulk-remove", n);
    if (kit::destructive_dialog(title, &s.bulk_remove_open, message.c_str(), confirm) == facet::DialogResult::Confirmed) {
        state.remove_accounts(state.selected_account_ids);
        state.selected_account_ids.clear();
        state.selection_mode = false;
        save(state);
    }
}

// --- Sign-in error --------------------------------------------------------------------------------

// What went wrong in plain words and the one thing that fixes it, chosen from the raw text of
// the launcher (which stays behind "Details" for support).
struct SignInProblem {
    const char* text;
    const char* action;  // the Primary button; null for "Try again"
    SettingsCategory category = SettingsCategory::SignInLaunch;
};

bool contains(const std::string& haystack, const char* needle) {
    std::string h = haystack;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(needle) != std::string::npos;
}

SignInProblem classify(const std::string& raw) {
    if (contains(raw, "path") || contains(raw, "not be found") || contains(raw, "not found") ||
        contains(raw, "steam.exe")) {
        return {"The app could not find Steam on this PC. Check the Steam folder in the sign-in settings.",
                "Open sign-in settings"};
    }
    if (contains(raw, "password")) {
        return {"This way of signing in needs the account's saved password, and there is none. Add the password, "
                "or choose another way to sign in from the arrow next to Sign in to Steam.",
                nullptr};
    }
    if (contains(raw, "token")) {
        return {"Steam did not accept the saved sign-in for this account. Try again; if it keeps failing, sign in "
                "with the password or import a fresh token.",
                nullptr};
    }
    if (contains(raw, "loader")) {
        return {"The CS2 loader could not be set up. Choose its file again in the loader settings.",
                "Open loader settings", SettingsCategory::Loaders};
    }
    return {"Steam could not be started for this account. Try again; if it keeps failing, copy the details for "
            "support.",
            nullptr};
}

void launch_failed_dialog(app::AppState& state) {
    ScreenState& s = screen();
    bool open = !state.launch_error.empty();
    if (facet::BeginModal("Couldn't sign in", &open, {.closeButton = false})) {
        const SignInProblem problem = classify(state.launch_error);
        facet::StyledText(problem.text, {.wrap = true});
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
        if (facet::BeginDisclosure("Details", nullptr)) {
            facet::StyledText(state.launch_error.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true,
                                                           .wrap = true});
            facet::CopyButton("Copy details", state.launch_error.c_str(),
                              {.variant = facet::ButtonVariant::Plain, .iconOnly = false});
            facet::EndDisclosure();
        }
        begin_actions("##launch-actions");
        if (facet::Button("Close")) open = false;
        if (problem.action != nullptr) {
            if (facet::Button(problem.action, {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("settings")})) {
                kit::open_settings(state, problem.category);
                open = false;
            }
        } else {
            core::Account* a = state.find_account(s.last_sign_in_id);
            ImGui::BeginDisabled(a == nullptr);
            if (facet::Button("Try again", {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("refresh")})) {
                open = false;
                state.launch_error.clear();
                if (a != nullptr) sign_in(state, *a);
            }
            ImGui::EndDisabled();
        }
        end_actions();
        facet::EndModal();
    }
    if (!open) state.launch_error.clear();
}

// --- Quick clean ----------------------------------------------------------------------------------

void quick_clean_dialog(app::AppState& state) {
    ScreenState& s = screen();
    if (state.settings.safe_mode) s.quick_clean_open = false;
    if (kit::confirm_dialog("Clean Steam caches?", &s.quick_clean_open,
                            "Closes Steam, then deletes its caches, logs and crash dumps. No account is signed out "
                            "and no saved login is touched.",
                            "Clean Steam caches") == facet::DialogResult::Confirmed) {
        app::cleaner_runner::run_async(state, app::cleaner_runner::Trigger::QuickClean);
    }
}

// --- Groups ---------------------------------------------------------------------------------------

void group_dialog(app::AppState& state) {
    GroupDialog& g = screen().group;
    core::Group* existing = nullptr;
    if (!g.create) {
        for (core::Group& candidate : state.vault.groups) {
            if (candidate.id == g.group_id) existing = &candidate;
        }
        if (existing == nullptr) g.open = false;
    }
    if (!facet::BeginModal(g.create ? "New group###group-dialog" : "Edit group###group-dialog", &g.open)) return;
    const bool name_ok = !g.name.empty();
    facet::BeginSettingsRow("Name");
    facet::TextField("##value", &g.name, {.placeholder = "Main accounts", .maxLength = 63});
    facet::EndSettingsRow();
    // Preset group colors (user data, the dot next to a group) plus a custom color.
    static const ImVec4 kPresets[] = {
        {0.31F, 0.56F, 0.97F, 1.0F}, {0.24F, 0.72F, 0.47F, 1.0F}, {0.95F, 0.62F, 0.20F, 1.0F},
        {0.86F, 0.33F, 0.36F, 1.0F}, {0.64F, 0.42F, 0.90F, 1.0F}, {0.20F, 0.72F, 0.78F, 1.0F},
        {0.91F, 0.44F, 0.70F, 1.0F}, {0.55F, 0.58F, 0.64F, 1.0F},
    };
    static const char* const kPresetNames[] = {"Blue", "Green", "Orange", "Red", "Purple", "Teal", "Pink", "Gray"};
    facet::BeginSettingsRow("Color");
    facet::SwatchPalette("##value", &g.color, kPresets, nullptr, {.names = kPresetNames});
    facet::ColorWell("Custom##custom", &g.color, {.helpText = "Shown as the dot next to the group in the list."});
    facet::EndSettingsRow();
    begin_actions("##group-actions");
    if (facet::Button("Cancel")) g.open = false;
    ImGui::BeginDisabled(!name_ok);
    if (facet::Button(g.create ? "Create group" : "Save group", {.variant = facet::ButtonVariant::Primary})) {
        if (g.create) {
            core::Group group;
            group.id = make_group_id();
            group.name = g.name;
            group.color_rgba = pack_rgba(g.color);
            state.vault.groups.push_back(std::move(group));
        } else if (existing != nullptr) {
            existing->name = g.name;
            existing->color_rgba = pack_rgba(g.color);
        }
        save(state);
        g.open = false;
    }
    ImGui::EndDisabled();
    end_actions();
    facet::EndModal();
}

void delete_group_dialog(app::AppState& state) {
    ScreenState& s = screen();
    std::string name = "This group";
    for (const core::Group& g : state.vault.groups) {
        if (g.id == s.delete_group_id) name = "\"" + g.name + "\"";
    }
    const std::string message = name + " is deleted. Its accounts stay in the vault and move to Ungrouped.";
    if (kit::destructive_dialog("Delete group?", &s.delete_group_open, message.c_str(), "Delete group") ==
        facet::DialogResult::Confirmed) {
        for (core::Account& a : state.vault.accounts) {
            if (a.group_id == s.delete_group_id) a.group_id.clear();
        }
        std::erase_if(state.vault.groups, [&](const core::Group& g) { return g.id == s.delete_group_id; });
        set_group_collapsed(state, s.delete_group_id, false);
        save(state);
    }
}

// --- Change Steam name ----------------------------------------------------------------------------

void username_dialog(app::AppState& state) {
    UsernameDialog& d = screen().username;
    if (!facet::BeginModal("Change Steam name", &d.open)) return;
    const core::Account* acc = state.find_account(d.account_id);
    if (acc == nullptr) {
        facet::InlineAlert("##gone", nullptr, "This account no longer exists.", {.tone = facet::Tone::Warning});
        begin_actions("##username-gone");
        if (facet::Button("Close")) d.open = false;
        end_actions();
        facet::EndModal();
        return;
    }
    facet::StyledText(("The public display name of " + kit::login_text(state, *acc) + " on Steam.").c_str(),
                      {.secondary = true, .wrap = true});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    ImGui::BeginDisabled(d.busy);
    facet::TextField("##persona", &d.name, {.placeholder = "New display name", .maxLength = 32});
    ImGui::EndDisabled();

    const std::int64_t cooldown = state.persona_change_cooldown_seconds(acc->id);
    if (cooldown > 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Steam limits name changes. Try again in %lldm %02llds.",
                      static_cast<long long>(cooldown / 60), static_cast<long long>(cooldown % 60));
        facet::InlineAlert("##rate", nullptr, msg, {.tone = facet::Tone::Warning, .icon = facet::Icon("clock")});
    }
    if (!d.status.empty()) {
        facet::InlineAlert("##status", nullptr, d.status.c_str(),
                           {.tone = d.status_error ? facet::Tone::Destructive : facet::Tone::Success});
    }

    begin_actions("##username-actions");
    if (facet::Button(d.status.empty() || d.status_error ? "Cancel" : "Close")) d.open = false;
    ImGui::BeginDisabled(d.busy || d.name.empty() || cooldown > 0);
    if (facet::Button("Change name", {.variant = facet::ButtonVariant::Primary, .loading = d.busy})) {
        d.busy = true;
        d.status.clear();
        const std::string new_name = d.name;
        core::Account snapshot = *acc;
        app::job_pump::submit([&state, snapshot, new_name]() mutable {
            auto res = sam::profile::change_persona_name(snapshot, new_name);
            if (!res.ok && res.needs_relogin) {
                std::string err;
                if (state.auto_relogin(snapshot.id, snapshot, &err)) {
                    res = sam::profile::change_persona_name(snapshot, new_name);
                }
            }
            state.post_ui_callback([&state, snapshot = std::move(snapshot), new_name = std::move(new_name), res]() mutable {
                const std::string shown = res.applied_name.empty() ? new_name : res.applied_name;
                if (auto* a = state.find_account(snapshot.id)) {
                    a->refresh_token = snapshot.refresh_token;
                    a->access_token = snapshot.access_token;
                    a->access_token_expires = snapshot.access_token_expires;
                    a->steam_login_secure = snapshot.steam_login_secure;
                    a->session_id = snapshot.session_id;
                    if (res.ok) {
                        a->web.persona_name = shown;
                        state.last_persona_change_unix[snapshot.id] = sam::ui::now_seconds();
                    }
                    state.vault_dirty = true;
                    state.save_vault_if_dirty();
                }
                UsernameDialog& dlg = screen().username;
                if (dlg.account_id != snapshot.id) return;
                dlg.busy = false;
                if (res.ok) {
                    dlg.status = "Display name changed to \"" + shown + "\".";
                    dlg.status_error = false;
                } else {
                    dlg.status = res.error.empty() ? std::string("Steam did not accept the new name.") : res.error;
                    dlg.status_error = true;
                }
            });
        });
    }
    ImGui::EndDisabled();
    end_actions();
    facet::EndModal();
}

}  // namespace

void draw_dialogs(app::AppState& state) {
    remove_dialog(state);
    bulk_remove_dialog(state);
    launch_failed_dialog(state);
    quick_clean_dialog(state);
    group_dialog(state);
    delete_group_dialog(state);
    username_dialog(state);
    draw_edit_notes_modal(state);
    draw_edit_account_modal(state);
    widgets::draw_export_bundle_modal(state, screen().export_state);
}

}  // namespace sam::ui::screens::accounts
