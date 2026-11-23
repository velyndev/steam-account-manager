#pragma once

// What the locked screen does to open or create a vault: key derivation in load_vault on the
// calling thread, the DPAPI cache of the master password, the registry entry of a new vault.
// Each function also says what went wrong, so the card can show it in the right place.

#include <cstdint>
#include <string>

#include "app/app_state.hpp"

namespace sam::ui::screens::unlock {

enum class UnlockOutcome {
    Opened,         // the vault is open and the session entered
    WrongPassword,  // the master password does not open this vault
    Corrupt,        // the vault file is damaged; message holds the store's reason
    Failed,         // any other failure (file access, a newer vault version); message holds it
};

struct UnlockResult {
    UnlockOutcome outcome = UnlockOutcome::Failed;
    std::string message;  // empty for Opened and WrongPassword
};

// Loads the active vault with `password` (the key derivation runs here and blocks for a
// moment), writes the remembered-password cache when settings.remember_master_password is on,
// and enters the session.
UnlockResult unlock_with_password(app::AppState& state, const std::string& password);

// Whether the active vault can be opened from its remembered password: the setting is on, the
// vault file exists and its DPAPI cache file exists. Reads the file system.
bool remembered_password_available(const app::AppState& state);

// Opens the active vault from its DPAPI cache, exactly as screens::try_cached_unlock always did
// (that function calls this one): false without touching anything when no cache applies, and
// on a failure the stale cache is removed and false returned. With `reason`, a failure writes
// one sentence for the user saying why the remembered password could not be used; it stays
// empty when there simply was no cache.
bool unlock_with_remembered_password(app::AppState& state, std::string* reason);

// Creates the vault file of the active vault id (first run, or a registry entry whose file is
// missing), registers it under `name` when the registry has no entry for it yet, writes the
// remembered-password cache when the setting is on, and enters the session. false and `err` on
// failure.
bool create_active_vault(app::AppState& state, const std::string& name, const std::string& password,
                         std::string& err);

// Creates a vault in a new folder with a new id, makes it active, registers it under `name`
// ("New Vault" when empty) and `color`, writes the remembered-password cache when the setting
// is on, and enters the session. The body of screens::create_and_open_vault, unchanged.
bool create_separate_vault(app::AppState& state, const std::string& name, std::uint32_t color,
                           const std::string& password, std::string& err);

}  // namespace sam::ui::screens::unlock
