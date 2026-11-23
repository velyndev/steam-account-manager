#pragma once

// Internal interface of the Add account screen: the methods, the flows that add an account,
// and the import helpers they share. Only the files of the screen include it.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"

namespace sam::ui::screens {
namespace add_account_detail {

// --- Methods and screen navigation ------------------------------------------------------------

// The ways to add an account, in the order the chooser lists them: most used first.
enum class Method { FullLogin, MaFile, ThisPc, NfaToken, InfoDat, Bundle };
inline constexpr int kMethodCount = 6;

// Shows the flow of `method` (add_account_screen.cpp). The flow keeps its own state.
void open_method(Method method);
// Back to the method chooser; the flows keep their state until they are reset.
void open_chooser();
// Every flow back to its first step and the chooser shown.
void reset_screen();
// Capture scenes: the state seeded for `state` is kept on its first frame instead of being
// reset for a new AppState.
void mark_seeded(const app::AppState& state);

// --- Flows -------------------------------------------------------------------------------------
// Each draws the whole flow of one method into the page body and keeps its state in its own
// file. reset_* puts the flow back to its first step and wipes any secret it held.

void draw_full_login(app::AppState& state);
void reset_full_login();
// Starts the full login for an account whose saved session expired (pending_relogin_login):
// the username prefilled, the stored password too when the vault has one. Returns false while
// another sign-in is under way; the request then waits until it is back at its first step.
bool begin_relogin(app::AppState& state, const std::string& login);
// True while the sign-in flow was started for an expired session (the page says "Sign in again").
bool relogin_active();

void draw_mafile_import(app::AppState& state);
void reset_mafile_import();
// Adds dropped or picked maFile paths to the maFile flow and rescans its preview.
void queue_mafiles(app::AppState& state, std::vector<std::string> paths);

void draw_info_dat_import(app::AppState& state);
void reset_info_dat_import();
void queue_info_dat(app::AppState& state, std::vector<std::string> paths);

void draw_nfa_token(app::AppState& state);
void reset_nfa_token();

void draw_this_pc(app::AppState& state);
void reset_this_pc();

void draw_bundle_import(app::AppState& state);
void reset_bundle_import();
// Starts the bundle flow on path (a file picked or dropped elsewhere): its unlock step.
void bundle_open_file(const std::string& path);

// --- Helpers -----------------------------------------------------------------------------------

std::string generate_ulid();
std::string trim_path(std::string s);
std::vector<std::filesystem::path> scan_dir_for_extension(
    const std::filesystem::path& dir, std::string_view ext_lower);

// A pasted NFA token read without touching the vault, for live validation.
struct JwtCheck {
    bool ok = false;
    std::string error;             // why it is not a token; empty when ok
    std::string login;             // from "login----token"; empty for a bare token
    std::uint64_t steam_id = 0;    // from the token, else looked up locally from the login
    std::int64_t expires = 0;      // unix seconds
    bool expired = false;
    bool client_audience = false;  // aud includes "client"
    bool steam_issuer = true;      // iss is Steam
};
JwtCheck check_jwt_token(const std::string& raw);

// Result of importing a single NFA refresh token ("SteamID----JWT" or a bare JWT).
struct JwtImportResult {
    bool ok = false;
    bool merged = false;           // existing account updated rather than created
    bool client_audience = false;  // token's aud includes "client" (client-signable)
    bool steam_issuer = true;      // token's iss is Steam (matches Python check_token)
    bool expired = false;
    std::int64_t expires = 0;
    std::uint64_t steam_id = 0;
    std::string error;
    std::string account_id;
    std::string login;
};

// Parses + validates a "SteamID----JWT" (or bare JWT) line, then creates or updates
// the matching NFA account in the vault in place. Caller persists the vault on ok.
JwtImportResult import_jwt_token(app::AppState& state, const std::string& raw,
                                 bool assign_nfa_group = true);

}  // namespace add_account_detail
}  // namespace sam::ui::screens
