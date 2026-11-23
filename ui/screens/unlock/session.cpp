#include "ui/screens/unlock/session.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "app/app_paths.hpp"
#include "app/vault_registry.hpp"
#include "core/account_store/store.hpp"
#include "core/log.hpp"
#include "platform/dpapi.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"

namespace sam::ui::screens::unlock {

namespace {

constexpr std::uint32_t kDefaultVaultColor = 0x4F8EF7FFu;

void write_active_cache(const std::string& pw) {
    try {
        std::span<const std::uint8_t> pw_bytes{reinterpret_cast<const std::uint8_t*>(pw.data()), pw.size()};
        auto wrapped = sam::platform::dpapi::protect(pw_bytes);
        sam::platform::atomic_write_file(app::master_pw_cache_path(), wrapped);
    } catch (const std::exception& ex) {
        SAM_LOG_WARN("auto-unlock cache write failed: {}", ex.what());
    }
}

void enter_session(app::AppState& state) {
    state.unlocked = true;
    state.needs_vault_pick = false;
    state.last_interaction = std::chrono::steady_clock::now();
    state.current_screen = app::Screen::Accounts;
    app::bind_vault_session(state);
    if (state.settings.refresh_on_launch) state.refresh_account_data();

    if (state.settings.info.show_external_funds) {
        state.refresh_all_spend(true);
    }
}

}  // namespace

UnlockResult unlock_with_password(app::AppState& state, const std::string& password) {
    UnlockResult result;
    try {
        state.vault = sam::core::store::load_vault(app::vault_path(), sam::crypto::make_secure(password));
        state.master_password = sam::crypto::make_secure(password);
        if (state.settings.remember_master_password) write_active_cache(password);
        enter_session(state);
        result.outcome = UnlockOutcome::Opened;
    } catch (const sam::core::store::WrongPassword&) {
        result.outcome = UnlockOutcome::WrongPassword;
    } catch (const sam::core::store::CorruptVault& ex) {
        result.outcome = UnlockOutcome::Corrupt;
        result.message = ex.what();
    } catch (const std::exception& ex) {
        result.outcome = UnlockOutcome::Failed;
        result.message = ex.what();
    }
    return result;
}

bool remembered_password_available(const app::AppState& state) {
    if (!state.settings.remember_master_password) return false;
    if (!sam::core::store::vault_exists(app::vault_path())) return false;
    std::error_code ec;
    return std::filesystem::exists(app::master_pw_cache_path(), ec);
}

bool unlock_with_remembered_password(app::AppState& state, std::string* reason) {
    if (reason != nullptr) reason->clear();
    if (!state.settings.remember_master_password) return false;
    if (!sam::core::store::vault_exists(app::vault_path())) return false;
    const auto cache_path = app::master_pw_cache_path();
    std::error_code ec;
    if (!std::filesystem::exists(cache_path, ec)) return false;
    try {
        auto wrapped = sam::platform::read_binary_file(cache_path);
        auto plain = sam::platform::dpapi::unprotect(wrapped);
        auto pw = sam::crypto::make_secure(std::string(plain.begin(), plain.end()));
        if (!plain.empty()) sam::crypto::zero_buffer(plain.data(), plain.size());
        state.vault = sam::core::store::load_vault(app::vault_path(), pw);
        state.master_password = pw;
        enter_session(state);
        return true;
    } catch (const std::exception& ex) {
        SAM_LOG_WARN("cached unlock failed ({}); removing stale cache", ex.what());
        std::filesystem::remove(cache_path, ec);
        if (reason != nullptr) {
            if (dynamic_cast<const sam::core::store::WrongPassword*>(&ex) != nullptr) {
                *reason = "The remembered master password no longer opens this vault. Enter the current one.";
            } else if (dynamic_cast<const sam::platform::dpapi::DpapiError*>(&ex) != nullptr) {
                *reason = "Windows could not decrypt the remembered master password for this user. Enter it "
                          "once to remember it again.";
            } else {
                *reason = std::string{"The remembered master password could not be used: "} + ex.what();
            }
        }
        return false;
    }
}

bool create_active_vault(app::AppState& state, const std::string& name, const std::string& password,
                         std::string& err) {
    try {
        sam::core::store::create_new_vault(app::vault_path(), sam::crypto::make_secure(password));
        state.vault = {};
        state.master_password = sam::crypto::make_secure(password);

        const auto id = sam::platform::active_vault_id();
        if (!state.vault_registry.find(id)) {
            app::add_vault_entry(state.vault_registry, id, name.empty() ? "My Vault" : name, kDefaultVaultColor);
        }
        if (state.settings.remember_master_password) write_active_cache(password);
        enter_session(state);
        err.clear();
        return true;
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
}

bool create_separate_vault(app::AppState& state, const std::string& name, std::uint32_t color,
                           const std::string& password, std::string& err) {
    try {
        const auto id = app::new_vault_id();
        std::error_code ec;
        std::filesystem::create_directories(app::vault_dir_for(id), ec);
        sam::platform::set_active_vault_id(id);
        sam::core::store::create_new_vault(app::vault_path(), sam::crypto::make_secure(password));
        state.vault = {};
        state.master_password = sam::crypto::make_secure(password);
        app::add_vault_entry(state.vault_registry, id, name.empty() ? "New Vault" : name, color);
        if (state.settings.remember_master_password) write_active_cache(password);
        enter_session(state);
        err.clear();
        return true;
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
}

}  // namespace sam::ui::screens::unlock
