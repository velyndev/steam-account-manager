#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/sda/confirmation.hpp"

namespace sam::ui::screens {
namespace confirmations_detail {

// What the workers know about one account's pending confirmations.
struct PendingList {
    std::vector<sda::Confirmation> items;
    bool refreshing = false;
    std::string last_error;
};

// What happened to one .maFile found by a folder scan.
enum class ScanOutcome {
    Linked,          // matched an account that needed it; its secrets were merged in
    Encrypted,       // the file is encrypted (SDA encryption); decrypt it first
    Ambiguous,       // its file name matched more than one account that needs data
    NoMatch,         // the name matched, the content belongs to another account
    MissingSecrets,  // the content matched but has no identity secret or device id
    Unreadable,      // the file could not be parsed
    NotNeeded,       // no account that is missing maFile data matches its name
};

struct ScanEntry {
    std::string file;        // file name, without the folder
    std::string account_id;  // the account it was matched to; empty for none
    ScanOutcome outcome = ScanOutcome::NotNeeded;
};

struct ScanResult {
    int targeted = 0;  // accounts that were missing maFile data when the scan started
    int linked = 0;
    int skipped_encrypted = 0;
    int skipped_no_match = 0;
    int skipped_ambiguous = 0;
    int parse_errors = 0;
    std::vector<ScanEntry> entries;  // every .maFile looked at, in the order found
};

// One confirmation picked for a decision, with the account it belongs to.
struct Pick {
    std::string account_id;
    sda::Confirmation conf;
};

// Shared persistent state: exactly one instance across the translation units. g_lists,
// g_last_scan and g_show_scan_modal are guarded by g_mtx (workers write them).
inline std::mutex g_mtx;
inline std::unordered_map<std::string, PendingList> g_lists;
inline std::atomic<bool> g_scan_in_progress{false};
inline bool g_show_scan_modal = false;
inline ScanResult g_last_scan;

// --- Defined in confirmations_screen.cpp -------------------------------------------------------

// Fetches one account's confirmations on a worker (with the session repair and auto-approve
// handling). Honors the per-account cooldown and skips an account already refreshing.
void submit_account_refresh(app::AppState& state, const core::Account& seed);

// The staggered Refresh all over every account with an authenticator that is not marked as a
// permanent failure and not on its cooldown.
void refresh_all(app::AppState& state);

// Seconds until `account_id` may be refreshed again (settings per_account_cooldown_seconds
// since its last finished refresh), 0 when it may refresh now.
std::int64_t refresh_cooldown_remaining(const app::AppState& state, const std::string& account_id);

// Approves (allow) or denies the picks. One pick goes through the single respond call with
// the nonce retry; several are grouped per account and sent with the bulk call, in batches of
// at most settings.confirmations.bulk_size_cap, accounts refresh_stagger_ms apart. Every pick
// is written to the audit log and leaves the pending list at once.
void respond(app::AppState& state, const std::vector<Pick>& picks, bool allow);

// --- Defined in confirmations_modals.cpp -------------------------------------------------------

// True when the account has an authenticator but no identity secret or device id, so its
// confirmations cannot be fetched.
bool needs_mafile_data(const core::Account& a);
// Opens a file dialog, loads the chosen maFile on a worker and merges it into the account
// when it matches; then refreshes the account. Failures land in the account's last_error.
void link_mafile_for_account(app::AppState& state, const core::Account& a);
// Opens a folder dialog and links every maFile that matches an account missing data. The
// result lands in g_last_scan and sets g_show_scan_modal.
void scan_folder_for_mafiles(app::AppState& state);

}  // namespace confirmations_detail
}  // namespace sam::ui::screens
