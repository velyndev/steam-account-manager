#pragma once

// The account bundle: export accounts to an encrypted .samvault file (a Facet modal the Accounts
// screen opens with request_export_bundle and draws every frame), and import one (the steps the
// Add account screen runs on its page).

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/account_store/store.hpp"
#include "ui/screens/add_account/bulk.hpp"

namespace sam::ui::widgets {

constexpr std::size_t kBundlePassphraseBufLen = 128;

// Owned by the calling screen as a static so it survives across frames while
// the modal is up.
struct ExportBundleState {
    bool open = false;
    // Empty = export every account in the vault.
    std::vector<std::string> account_ids;
    std::string status;  // the export error, or the saved path once done
    bool done = false;
    // The checklist: one id per offered account and whether it is ticked (built on the first
    // frame from account_ids, or the whole vault).
    std::vector<std::string> row_ids;
    std::vector<char> row_checked;
    bool rows_built = false;
    std::size_t exported = 0;
    std::string passphrase;
    std::string confirm;
};

struct ImportBundleState {
    bool open = false;
    std::filesystem::path picked_path;
    std::array<char, kBundlePassphraseBufLen> passphrase_buf{};
    std::string error;      // a plain sentence about why the bundle could not be opened
    std::string error_raw;  // the technical text behind "Details"
    bool have_preview = false;
    core::Vault preview_vault;
    core::store::MergeReport preview_report;
    bool done = false;
    std::string status;
    bool wrong_passphrase = false; // the last unlock failed on the passphrase; cleared on typing
    // One row per account of the bundle (payload: index in preview_vault.accounts).
    std::vector<screens::add_account_detail::BulkRow> rows;
};

// Empty `ids` exports every account in the vault.
void request_export_bundle(ExportBundleState& state,
                            std::vector<std::string> ids);

void request_import_bundle(ImportBundleState& state,
                            std::filesystem::path path);

// Safe to call every frame; renders nothing until a request comes in. The dialog closes on lock
// and vault change and when the user leaves the Accounts screen.
void draw_export_bundle_modal(app::AppState& app, ExportBundleState& state);

// The import steps for a page: reads the bundle with the passphrase in state.passphrase_buf
// (sets have_preview and the rows, or wrong_passphrase, or error); merges the ticked rows into
// the vault and saves it (sets done and each row's result).
void bundle_unlock(app::AppState& app, ImportBundleState& state);
void bundle_import(app::AppState& app, ImportBundleState& state);
// Clears the import state and wipes the passphrase.
void reset_import_bundle(ImportBundleState& state);

// Fills state.rows from state.preview_vault (new or replacing, every row ticked). Called after a
// bundle was unlocked; capture scenes call it after seeding a preview.
void build_import_rows(app::AppState& app, ImportBundleState& state);

}  // namespace sam::ui::widgets
