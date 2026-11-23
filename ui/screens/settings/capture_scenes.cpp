// Screenshot scenes of the Settings screen (SAM_UI_CAPTURE builds only): one per category, plus
// the search, the unsaved changes bar, safe mode, the cleaner preview, the per-account proxy
// editor and the dialogs.

#ifdef SAM_UI_CAPTURE

#include <string>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "core/cleaner/plan.hpp"
#include "core/crypto/secure_string.hpp"
#include <facet/core/theme.h>

#include "ui/screens/settings/capture_hooks.hpp"
#include "ui/screens/settings/page.hpp"
#include "ui/screens/settings_screen.hpp"

namespace {

using sam::app::AppState;
using sam::ui::screens::SettingsCategory;
namespace capture = sam::ui::screens::settings::capture;

void open_category(AppState& state, SettingsCategory category) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Settings;
    state.pending_settings_category = static_cast<int>(category);
}

#define SAM_SETTINGS_SCENE(ident, id, category)                     \
    void setup_##ident(AppState& state) { open_category(state, category); } \
    SAM_UI_CAPTURE_SCENE(settings_##ident, id, &setup_##ident, 30)

SAM_SETTINGS_SCENE(general, "settings-general", SettingsCategory::General)
// The theme control shows the theme the capture renders (the harness picks light or dark), so
// the picture never shows "Dark" selected on a light page.
void match_rendered_theme(AppState& state) {
    const auto mode = facet::GetThemeMode() == facet::ThemeMode::Light ? sam::app::ThemeMode::Light
                                                                        : sam::app::ThemeMode::Dark;
    state.settings.theme_mode = mode;
    sam::ui::screens::settings::saved().theme_mode = mode;
}
void setup_appearance(AppState& state) {
    open_category(state, SettingsCategory::Appearance);
    capture::after_baseline(&match_rendered_theme);
}
SAM_UI_CAPTURE_SCENE(settings_appearance, "settings-appearance", &setup_appearance, 30)
SAM_SETTINGS_SCENE(accounts, "settings-accounts", SettingsCategory::AccountsRefresh)
SAM_SETTINGS_SCENE(steam_guard, "settings-steam-guard", SettingsCategory::SteamGuard)
SAM_SETTINGS_SCENE(sign_in, "settings-sign-in", SettingsCategory::SignInLaunch)
SAM_SETTINGS_SCENE(cs2, "settings-cs2", SettingsCategory::Cs2)
SAM_SETTINGS_SCENE(trading, "settings-trading", SettingsCategory::Trading)
SAM_SETTINGS_SCENE(notifications, "settings-notifications", SettingsCategory::Notifications)
SAM_SETTINGS_SCENE(network, "settings-network", SettingsCategory::Network)
SAM_SETTINGS_SCENE(privacy, "settings-privacy", SettingsCategory::PrivacySecurity)
SAM_SETTINGS_SCENE(vaults, "settings-vaults", SettingsCategory::VaultsStorage)
SAM_SETTINGS_SCENE(cleaner, "settings-cleaner", SettingsCategory::Cleaner)
SAM_SETTINGS_SCENE(hwid, "settings-hwid", SettingsCategory::HwidSpoofer)
SAM_SETTINGS_SCENE(loaders, "settings-loaders", SettingsCategory::Loaders)
SAM_SETTINGS_SCENE(about, "settings-about", SettingsCategory::About)

void setup_vault_menu(AppState& state) {
    open_category(state, SettingsCategory::VaultsStorage);
    capture::after_baseline([](AppState&) { capture::open_vault_menu("demo-vault-work"); });
}
SAM_UI_CAPTURE_SCENE(settings_vault_menu, "settings-vault-menu", &setup_vault_menu, 40)

// "refresh" matches rows in Accounts and refresh, Steam Guard, CS2 and Trading.
// No pending category: opening one clears the query.
void setup_search(AppState& state) {
    open_category(state, SettingsCategory::General);
    state.pending_settings_category = -1;
    capture::set_query("refresh");
}
SAM_UI_CAPTURE_SCENE(settings_search, "settings-search", &setup_search, 40)

void setup_search_empty(AppState& state) {
    open_category(state, SettingsCategory::General);
    state.pending_settings_category = -1;
    capture::set_query("zebra");
}
SAM_UI_CAPTURE_SCENE(settings_search_empty, "settings-search-empty", &setup_search_empty, 30)

void make_pending(AppState& state) {
    state.settings.auto_lock_minutes = 30;
    state.settings.hide_notes = true;
    state.settings.sda.show_next_code = false;
}
void setup_unsaved(AppState& state) {
    open_category(state, SettingsCategory::General);
    capture::after_baseline(&make_pending);
}
SAM_UI_CAPTURE_SCENE(settings_unsaved, "settings-unsaved", &setup_unsaved, 40)

void setup_safe_mode(AppState& state) {
    open_category(state, SettingsCategory::PrivacySecurity);
    state.settings.safe_mode = true;
}
SAM_UI_CAPTURE_SCENE(settings_safe_mode, "settings-safe-mode", &setup_safe_mode, 30)

void setup_safe_mode_confirm(AppState& state) {
    setup_safe_mode(state);
    capture::after_baseline([](AppState&) { capture::open_safe_mode_confirm(); });
}
SAM_UI_CAPTURE_SCENE(settings_safe_mode_confirm, "settings-safe-mode-confirm", &setup_safe_mode_confirm, 40)

// A fictional plan: the paths are made up for the picture, nothing is read from disk.
void setup_cleaner_preview(AppState& state) {
    open_category(state, SettingsCategory::Cleaner);
    state.settings.cleaner.enabled = true;
    state.settings.cleaner.profile = sam::app::CleanerProfile::AccountReset;
    sam::cleaner::Plan plan;
    const auto add = [&plan](sam::cleaner::OpKind kind, const wchar_t* target, const wchar_t* value,
                             std::uint64_t bytes) {
        sam::cleaner::PlanStep step;
        step.target_id = "demo";
        step.op.kind = kind;
        step.op.target = target;
        step.op.value_name = value;
        step.op.size_bytes = bytes;
        plan.steps.push_back(step);
        plan.total_bytes += bytes;
        plan.total_file_count += bytes > 0 ? 3 : 0;
    };
    add(sam::cleaner::OpKind::RemoveTree, L"C:\\Demo\\Steam\\appcache\\httpcache", L"", 48ull << 20);
    add(sam::cleaner::OpKind::RemoveTree, L"C:\\Demo\\Steam\\logs", L"", 3ull << 20);
    add(sam::cleaner::OpKind::RemoveTree, L"C:\\Demo\\Steam\\dumps", L"", 12ull << 20);
    add(sam::cleaner::OpKind::RemoveFile, L"C:\\Demo\\Steam\\ssfn0000000000000000000", L"", 2048);
    add(sam::cleaner::OpKind::VdfRemoveChild, L"C:\\Demo\\Steam\\config\\loginusers.vdf", L"users/76561190000000001", 0);
    add(sam::cleaner::OpKind::VdfRemoveChild, L"C:\\Demo\\Steam\\config\\config.vdf", L"ConnectCache/demo", 0);
    add(sam::cleaner::OpKind::RemoveRegistryValue, L"HKCU\\Software\\Valve\\Steam", L"AutoLoginUser", 0);
    state.cleaner_preview = plan;
    sam::cleaner::CleanResult last;
    last.succeeded = 41;
    last.failed = 2;
    last.failure_messages = {L"C:\\Demo\\Steam\\logs\\content_log.txt: the file is in use by another process",
                             L"C:\\Demo\\Steam\\dumps\\crash_demo.dmp: access is denied"};
    state.cleaner_last = last;
    capture::scroll_to_end();
}
SAM_UI_CAPTURE_SCENE(settings_cleaner_preview, "settings-cleaner-preview", &setup_cleaner_preview, 30)

void setup_proxies(AppState& state) {
    open_category(state, SettingsCategory::Network);
    state.settings.proxy_mode = sam::app::ProxyMode::PerAccount;
    const char* const kProxies[] = {"socks5://demo:demo@proxy-a.example:1080", "http://proxy-b.example:8080",
                                    "socks5://proxy-c.example:1080"};
    for (int i = 0; i < 3; ++i) {
        if (auto* a = state.find_account(sam::app::ui_capture::demo_account_id(i * 2))) {
            a->proxy = sam::crypto::make_secure(kProxies[i]);
        }
    }
    capture::after_baseline([](AppState&) { capture::edit_proxy_of(sam::app::ui_capture::demo_account_id(2)); });
}
SAM_UI_CAPTURE_SCENE(settings_proxies, "settings-proxies", &setup_proxies, 30)

void setup_create_vault(AppState& state) {
    open_category(state, SettingsCategory::VaultsStorage);
    capture::after_baseline([](AppState&) { capture::open_create_vault(); });
}
SAM_UI_CAPTURE_SCENE(settings_create_vault, "settings-create-vault", &setup_create_vault, 40)

void setup_delete_vault(AppState& state) {
    open_category(state, SettingsCategory::VaultsStorage);
    capture::after_baseline([](AppState&) { capture::open_delete_vault("demo-vault-work"); });
}
SAM_UI_CAPTURE_SCENE(settings_delete_vault, "settings-delete-vault", &setup_delete_vault, 40)

}  // namespace

#endif
