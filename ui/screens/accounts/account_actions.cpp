// The Accounts screen's actions: signing in, copying the code, refreshing, the selection model,
// the filter, the group bookkeeping and the per-frame pumps. Outcomes are shown through the
// kit's toasts and dialogs.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "app/cleaner_runner.hpp"
#include "app/gamesense_loader.hpp"
#include "app/luminary_loader.hpp"
#include "core/account_store/filter.hpp"
#include "core/hwid/hwid_gen.hpp"
#include "core/launch/cs2_autostart.hpp"
#include "core/launch/steam_launcher.hpp"
#include "core/sda/totp.hpp"
#include "core/strings.hpp"
#include "platform/clipboard.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::accounts {

ScreenState& screen() {
    static ScreenState s;
    return s;
}

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

ImGuiID account_key(const std::string& account_id) {
    return ImHashStr(account_id.c_str(), account_id.size(), 0x5A11ACC7u);
}

ImGuiID group_key(const std::string& group_key_text) {
    return ImHashStr(group_key_text.c_str(), group_key_text.size(), 0x6B0B4EADu);
}

std::vector<std::size_t> visible_accounts(const app::AppState& state, int sort) {
    core::AccountFilter filter;
    filter.query = state.search_query;
    filter.only_with_any_ban = state.settings.quick_filters.only_banned;
    filter.only_with_cooldown = state.settings.quick_filters.only_cooldown;
    filter.only_with_prime = state.settings.quick_filters.only_prime;
    filter.now_unix = now_unix();
    filter.sort = static_cast<core::SortKey>(std::clamp(sort, 0, 12));
    return core::apply_filter(state.vault.accounts, filter);
}

bool filters_active(const app::AppState& state) {
    const auto& q = state.settings.quick_filters;
    return !state.search_query.empty() || q.only_banned || q.only_cooldown || q.only_prime;
}

void clear_filters(app::AppState& state) {
    state.search_query.clear();
    auto& q = state.settings.quick_filters;
    if (q.only_banned || q.only_cooldown || q.only_prime) {
        q = {};
        state.save_settings();
    }
}

// --- Selection ------------------------------------------------------------------------------------

void load_selection(ImGuiSelectionBasicStorage& storage, const app::AppState& state) {
    storage.Clear();
    if (!state.selected_account_ids.empty()) {
        for (const std::string& id : state.selected_account_ids) storage.SetItemSelected(account_key(id), true);
    } else if (!state.selected_account_id.empty()) {
        storage.SetItemSelected(account_key(state.selected_account_id), true);
    }
}

bool store_selection(app::AppState& state, const std::vector<std::string>& ids) {
    if (ids.empty()) {
        if (state.selected_account_id.empty() && state.selected_account_ids.empty()) return false;
        state.selected_account_id.clear();
        state.selected_account_ids.clear();
        state.selection_mode = false;
        return false;
    }
    if (ids.size() == 1) {
        const bool changed = state.selected_account_id != ids.front() || !state.selected_account_ids.empty();
        state.selected_account_id = ids.front();
        state.selected_account_ids.clear();
        state.selection_mode = false;
        return changed;
    }
    std::unordered_set<std::string> set(ids.begin(), ids.end());
    if (set != state.selected_account_ids) state.selected_account_ids = std::move(set);
    state.selection_mode = true;
    if (state.selected_account_ids.count(state.selected_account_id) == 0) state.selected_account_id = ids.front();
    return false;
}

bool multi_selection(const app::AppState& state) {
    return state.selected_account_ids.size() > 1;
}

// --- Launch ---------------------------------------------------------------------------------------

namespace {

void show_launch_error(app::AppState& state, std::string message) {
    state.launch_error = message.empty() ? std::string("Steam could not be started for this account.") : std::move(message);
}

void do_launch(app::AppState& state, core::Account& a, bool use_token) {
    const bool safe = state.settings.safe_mode;
    const core::LoginMethod method = core::effective_login_method(a.login_method, safe);

    std::filesystem::path loader;
    if (method == core::LoginMethod::LaunchCs2Gamesense) {
        if (auto p = app::gamesense_loader_path()) loader = *p;
    }
    std::filesystem::path lum_loader;
    if (method == core::LoginMethod::LaunchCs2Luminary) {
        if (auto p = app::luminary_loader_path()) lum_loader = *p;
    }

    if (a.is_nfa) {
        state.capture_rotated_token_now(a.id, core::to_lower(a.login));
    }
    sam::launch::LaunchOptions opts;
    opts.cs2_launch_options = state.settings.cs2_video.launch_options;
    opts.disable_cloud_on_login = state.settings.disable_cloud_on_login;
    opts.disable_news_on_login = state.settings.disable_news_on_login;
    opts.login_invisible = state.settings.login_invisible;
    opts.disable_remote_play_on_login = state.settings.disable_remote_play_on_login;
    opts.disable_workshop_on_login = state.settings.disable_workshop_on_login;
    opts.remember_password = state.settings.remember_password_on_login;
    opts.gamesense_loader = loader;
    opts.luminary_loader = lum_loader;
    opts.hwid_component_mask = state.settings.hwid.component_mask;
    opts.use_token = use_token;
    opts.safe_mode = safe;

    if (!safe && state.settings.cleaner.enabled && state.settings.cleaner.run_before_launch &&
        !state.cleaner_busy.load()) {
        const app::Settings snapshot = state.settings;
        opts.after_steam_down = [snapshot] { app::cleaner_runner::run_blocking(snapshot, true, false); };
    }

    auto result = sam::launch::launch_account(a, opts);
    if (result.status != sam::launch::LaunchStatus::Ok) {
        show_launch_error(state, result.message);
        return;
    }
    if (result.hwid_outcome == sam::launch::InjectOutcome::Success) {
        state.last_hwid_account_id = a.id;
        kit::show_toast(state, "HWID spoofed for " + kit::login_text(state, a),
                        {.tone = widgets::ToastTone::Success, .id = "hwid-" + a.id, .account_id = a.id});
    } else if (result.hwid_outcome == sam::launch::InjectOutcome::Failed) {
        kit::show_toast(state, "HWID spoof failed for " + kit::login_text(state, a) + ": " + result.hwid_error,
                        {.tone = widgets::ToastTone::Destructive, .id = "hwid-fail-" + a.id, .account_id = a.id});
    }
    a.last_login_unix = now_seconds();
    state.vault_dirty = true;
    state.save_vault_if_dirty();

    if (a.is_nfa) {
        state.capture_rotated_token_async(a.id, a.steam_id_64, core::to_lower(a.login));
    } else if (use_token) {
        state.verify_cm_token_signin_async(a.id, a.steam_id_64, core::to_lower(a.login));
    }
    if (state.settings.cs2_video.mode != app::CS2ConfigMode::None) {
        state.apply_cs2_video_config(a);
    }
    if (method != core::LoginMethod::Normal && !result.first_login_deferred) {
        sam::launch::cs2_autostart::start_async(method, a.steam_id_64, std::move(loader), std::move(lum_loader));
    }
}

// Generates the profile this launch spoofs with before Steam starts, so an account never signs
// in with the identifiers another account already used: a first profile for an account that
// has none yet, and a new one on every launch when the setting asks for it.
void prepare_hwid_profile(app::AppState& state, core::Account& a) {
    if (!hwid_spoofed(state, a)) return;
    if (a.hwid.has_value() && !state.settings.hwid.new_profile_every_launch) return;
    a.hwid = core::hwid::generate_profile();
    state.vault_dirty = true;
    state.save_vault_if_dirty();
}

}  // namespace

bool hwid_spoofed(const app::AppState& state, const core::Account& a) {
    if (state.settings.safe_mode) return false;
    return state.settings.hwid.always_spoof ? !a.hwid_excluded : a.hwid.has_value();
}

void sign_in(app::AppState& state, core::Account& a) {
    screen().last_sign_in_id = a.id;
    state.flush_pending_save();
    prepare_hwid_profile(state, a);

    const bool use_token = !a.is_nfa && state.effective_sign_in_method(a.id) == app::SignInMethod::TokenInject;
    state.cm_relaunch_attempted.erase(a.id);

    if (use_token && !app::cm_token_valid(a)) {
        if (a.password.empty()) {
            show_launch_error(state,
                              "Token injection needs a stored password to mint the login token, but this "
                              "account has no password.");
            return;
        }
        state.pending_token_launch = {a.id, true, false, false, {}};
        const std::string aid = a.id;
        state.acquire_cm_token(aid, [&state](bool ok, std::string err) {
            state.pending_token_launch.minting = false;
            state.pending_token_launch.mint_done = true;
            state.pending_token_launch.mint_ok = ok;
            state.pending_token_launch.mint_error = std::move(err);
        });
        kit::show_toast(state, "Minting a login token for " + kit::login_text(state, a),
                        {.id = "token-mint-" + a.id, .account_id = a.id, .seconds = 15});
        return;
    }
    do_launch(state, a, use_token);
}

void copy_code(app::AppState& state, const core::Account& a) {
    if (!a.sda) return;
    const std::string code = sda::generate_code_now(a.sda->shared_secret);
    if (code.empty()) return;
    platform::clipboard::set_text_with_auto_clear(code, std::chrono::seconds(state.settings.clipboard_clear_seconds));
    kit::show_toast(state, "Steam Guard code copied", {.tone = widgets::ToastTone::Success, .id = "code-copied", .seconds = 3});
}

bool can_refresh(const app::AppState& state, const core::Account& a, std::string* reason) {
    if (state.refreshing_ids.count(a.id) != 0) {
        if (reason != nullptr) *reason = "Refreshing this account";
        return false;
    }
    const std::int64_t cooldown = state.refresh_cooldown_seconds(a.id);
    if (cooldown > 0) {
        if (reason != nullptr) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Refreshed recently. Try again in %llds.", static_cast<long long>(cooldown));
            *reason = buf;
        }
        return false;
    }
    if (reason != nullptr) *reason = "Refresh Steam data, spend and CS2 data of this account";
    return true;
}

void refresh_account(app::AppState& state, core::Account& a) {
    if (!can_refresh(state, a, nullptr)) return;
    state.refresh_single_account(a.id);
    if (!a.is_nfa) state.refresh_spend(a.id, true);
    state.queue_gc_validate(a.id);
}

// --- Dialog requests ------------------------------------------------------------------------------

void select_for_dialog(app::AppState& state, const std::string& account_id) {
    if (state.selected_account_ids.count(account_id) == 0) {
        state.selected_account_ids.clear();
        state.selection_mode = false;
    }
    state.selected_account_id = account_id;
    screen().context_account = account_id;
}

void request_remove(app::AppState& state, const std::string& account_id) {
    select_for_dialog(state, account_id);
    screen().remove_id = account_id;
    screen().remove_open = true;
}

void request_username(app::AppState& state, const core::Account& a) {
    select_for_dialog(state, a.id);
    UsernameDialog& d = screen().username;
    d = {};
    d.open = true;
    d.account_id = a.id;
    d.name = a.web.persona_name;
}

void request_edit(app::AppState& state, const core::Account& a) {
    select_for_dialog(state, a.id);
    state.account_edit_requested = true;
}

void request_notes(app::AppState& state, const core::Account& a) {
    select_for_dialog(state, a.id);
    state.notes_edit_requested = true;
}

void close_transient(ScreenState& s) {
    s.refresh_menu_open = false;
    s.more_menu_open = false;
    s.signin_menu_open = false;
    s.detail_more_open = false;
    s.remove_open = false;
    s.remove_id.clear();
    s.bulk_remove_open = false;
    s.quick_clean_open = false;
    s.delete_group_open = false;
    s.delete_group_id.clear();
    s.group = GroupDialog{};
    s.username = UsernameDialog{};
    s.export_state = widgets::ExportBundleState{};
}

namespace {

// Lock, vault change and capture scenes: everything transient goes, the inspector closes and the
// list scrolls to the selection again.
void reset_accounts_ui(app::AppState&) {
    set_capture_force(CaptureForce::None);
    ScreenState& s = screen();
    close_transient(s);
    s.inspector_open = true;
    s.detail_open = false;
    s.context_account.clear();
    s.scrolled_to.clear();
}

void leave_accounts(app::AppState&) {
    ScreenState& s = screen();
    close_transient(s);
    s.scrolled_to.clear();
}

}  // namespace

void request_export(std::vector<std::string> ids) {
    widgets::request_export_bundle(screen().export_state, std::move(ids));
}

void request_new_group() {
    GroupDialog& g = screen().group;
    g = {};
    g.open = true;
    g.create = true;
}

void request_edit_group(const core::Group& group) {
    GroupDialog& g = screen().group;
    g = {};
    g.open = true;
    g.create = false;
    g.group_id = group.id;
    g.name = group.name;
    const std::uint32_t c = group.color_rgba;
    g.color = ImVec4(static_cast<float>((c >> 24) & 0xFF) / 255.0F, static_cast<float>((c >> 16) & 0xFF) / 255.0F,
                     static_cast<float>((c >> 8) & 0xFF) / 255.0F, 1.0F);
}

void request_delete_group(const std::string& group_id) {
    screen().delete_group_id = group_id;
    screen().delete_group_open = true;
}

void move_to_group(app::AppState& state, const std::vector<std::string>& ids, const std::string& group_id) {
    bool changed = false;
    for (const std::string& id : ids) {
        if (core::Account* a = state.find_account(id); a != nullptr && a->group_id != group_id) {
            a->group_id = group_id;
            changed = true;
        }
    }
    if (changed) {
        state.vault_dirty = true;
        state.save_vault_if_dirty();
    }
}

bool group_collapsed(const app::AppState& state, const std::string& key) {
    const auto& v = state.settings.collapsed_groups;
    return std::find(v.begin(), v.end(), key) != v.end();
}

void set_group_collapsed(app::AppState& state, const std::string& key, bool collapsed) {
    auto& v = state.settings.collapsed_groups;
    const auto it = std::find(v.begin(), v.end(), key);
    if (collapsed && it == v.end()) {
        v.push_back(key);
        state.save_settings();
    } else if (!collapsed && it != v.end()) {
        v.erase(it);
        state.save_settings();
    }
}

// --- Pumps ----------------------------------------------------------------------------------------

namespace {

void pick_loader(app::AppState& state, std::optional<std::string>& request, const wchar_t* title,
                 bool (*install)(const std::filesystem::path&, std::string*), core::LoginMethod method,
                 const char* fallback_error) {
    if (!request.has_value()) return;
    const std::string acc_id = *request;
    request.reset();

    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = title;
    opts.filters = {
        {L"Executable (*.exe)", L"*.exe"},
        {L"All files (*.*)", L"*.*"},
    };
    const auto picked = platform::file_dialog::open_file(opts);
    if (!picked.ok) return;
    std::string err;
    if (install(picked.path, &err)) {
        if (!acc_id.empty()) {
            if (auto* acc = state.find_account(acc_id)) {
                acc->login_method = method;
                state.vault_dirty = true;
                state.save_vault_if_dirty();
            }
        }
    } else {
        show_launch_error(state, err.empty() ? std::string(fallback_error) : err);
    }
}

}  // namespace

void track_context(app::AppState& state) {
    ScreenState& s = screen();
    if (s.context_account == state.selected_account_id) return;
    // The selection moved on: menus and dialogs opened for the previous account close. The
    // export dialog of a multi-selection belongs to the selection and stays.
    const bool exporting_selection = s.export_state.open && s.export_state.account_ids.size() > 1;
    const widgets::ExportBundleState keep = s.export_state;
    const bool bulk = s.bulk_remove_open;
    close_transient(s);
    if (exporting_selection) s.export_state = keep;
    s.bulk_remove_open = bulk;
    s.context_account = state.selected_account_id;
}

void run_pumps(app::AppState& state) {
    if (state.settings.safe_mode) {
        state.gamesense_pick_request.reset();
        state.luminary_pick_request.reset();
    }
    pick_loader(state, state.gamesense_pick_request, L"Choose gamesense loader", &app::install_gamesense_loader,
                core::LoginMethod::LaunchCs2Gamesense, "Could not install the gamesense loader.");
    pick_loader(state, state.luminary_pick_request, L"Choose luminary loader", &app::install_luminary_loader,
                core::LoginMethod::LaunchCs2Luminary, "Could not install the luminary loader.");

    if (state.pending_token_launch.mint_done) {
        auto& ptl = state.pending_token_launch;
        const std::string aid = ptl.account_id;
        const bool ok = ptl.mint_ok;
        std::string err = std::move(ptl.mint_error);
        ptl = {};
        if (!ok) {
            show_launch_error(state, "Could not mint a login token: " + err);
        } else if (auto* acc = state.find_account(aid)) {
            do_launch(state, *acc, true);
        }
    }
}

}  // namespace sam::ui::screens::accounts

SAM_KIT_TRANSIENT_RESET(accounts_screen, &sam::ui::screens::accounts::reset_accounts_ui)
SAM_KIT_SCREEN_LEAVE(accounts_screen, sam::app::Screen::Accounts, &sam::ui::screens::accounts::leave_accounts)
