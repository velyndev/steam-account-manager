#include "ui/widgets/bundle_dialogs.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "core/crypto/secure_string.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/secrets.hpp"
#include "ui/kit/transient.hpp"
#include "ui/util.hpp"

namespace sam::ui::widgets {

namespace {

namespace detail = screens::add_account_detail;
using detail::BulkRow;
using detail::RowStatus;

constexpr float kDialogWidth = 600.0F;  // units: the checklist and the two passphrase fields

void wipe(std::array<char, kBundlePassphraseBufLen>& buf) { crypto::zero_buffer(buf.data(), buf.size()); }
void wipe(std::string& s) {
    crypto::zero_buffer(s.data(), s.size());
    s.clear();
}

void gap(float units) { ImGui::Dummy(ImVec2(0.0F, facet::Px(units))); }

std::string default_export_filename() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_MSC_VER)
    localtime_s(&tm, &now);
#else
    tm = *std::localtime(&now);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "steam-accounts-%Y-%m-%d.samvault", &tm);
    return buf;
}

// Cancel and the primary action at the trailing end of the dialog, the primary last.
bool footer(const char* primary, bool enabled, bool* cancel) {
    bool pressed = false;
    facet::BeginStack("##footer", {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
    facet::BeginStack("##footer-actions", {.orientation = facet::Orientation::Horizontal});
    if (facet::Button("Cancel")) *cancel = true;
    ImGui::BeginDisabled(!enabled);
    if (facet::Button(primary, {.variant = facet::ButtonVariant::Primary})) pressed = true;
    ImGui::EndDisabled();
    facet::EndStack();
    facet::EndStack();
    return pressed;
}

// --- Export ------------------------------------------------------------------------------------

// The export state last drawn, so the transient reset and the screen-leave hook can close it.
ExportBundleState* g_export = nullptr;

void reset_export(ExportBundleState& s) {
    wipe(s.passphrase);
    wipe(s.confirm);
    s.status.clear();
    s.account_ids.clear();
    s.row_ids.clear();
    s.row_checked.clear();
    s.rows_built = false;
    s.done = false;
    s.exported = 0;
}

void close_export(app::AppState&) {
    if (g_export == nullptr) return;
    g_export->open = false;
    reset_export(*g_export);
}

void build_export_rows(app::AppState& app, ExportBundleState& s) {
    s.row_ids.clear();
    if (s.account_ids.empty()) {
        for (const auto& a : app.vault.accounts) s.row_ids.push_back(a.id);
    } else {
        for (const auto& id : s.account_ids) {
            if (app.find_account(id) != nullptr) s.row_ids.push_back(id);
        }
    }
    s.row_checked.assign(s.row_ids.size(), 1);
    s.rows_built = true;
}

const char* kind_text(const core::Account& a) {
    if (core::store::is_cached_account(a)) return "From this PC";
    if (a.is_nfa) return "NFA token";
    return a.sda.has_value() ? "Password, authenticator" : "Password";
}

// The checklist shows six rows where the window allows it and gives up rows (down to three)
// before the passphrase fields and the buttons leave a short window or a 1.5x scale.
int visible_rows() {
    const facet::Theme& t = facet::T();
    const float display = ImGui::GetMainViewport()->Size.y / facet::Px(1.0F);
    const int rows = static_cast<int>((display - 360.0F) / t.list.rowHeight);
    return std::clamp(rows, 3, 6);
}

struct ExportRows {
    app::AppState* app = nullptr;
    const std::vector<std::string>* ids = nullptr;
    std::vector<std::string> names;
};

bool export_row(void* user, int index, facet::ChoiceItem* out) {
    auto* rows = static_cast<ExportRows*>(user);
    const auto idx = static_cast<std::size_t>(index);
    const core::Account* a = rows->app->find_account((*rows->ids)[idx]);
    if (a == nullptr) return false;
    out->label = rows->names[idx].c_str();
    out->detail = kind_text(*a);
    return true;
}

// Why a searchable checklist: the same pattern as the Authenticator's maFile export, so picking
// accounts to export works the same everywhere; each row's detail says what kind of account it is.
void export_checklist(app::AppState& app, ExportBundleState& s) {
    ExportRows rows;
    rows.app = &app;
    rows.ids = &s.row_ids;
    for (const auto& id : s.row_ids) {
        const core::Account* a = app.find_account(id);
        rows.names.push_back(a != nullptr ? kit::display_name(app, *a) : std::string());
    }
    const auto n = s.row_ids.size();
    auto checked = std::make_unique<bool[]>(n > 0 ? n : 1);
    for (std::size_t i = 0; i < n; ++i) checked[i] = s.row_checked[i] != 0;
    if (facet::SearchableChecklist("Accounts", std::span<bool>(checked.get(), n), static_cast<int>(n), &export_row,
                                   &rows,
                                   {.placeholder = "Filter accounts",
                                    .visibleRows = visible_rows(),
                                    .emptyText = "The vault has no accounts to export."})) {
        for (std::size_t i = 0; i < n; ++i) s.row_checked[i] = checked[i] ? 1 : 0;
    }
}

void run_export(app::AppState& app, ExportBundleState& s) {
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < s.row_ids.size(); ++i) {
        if (s.row_checked[i] != 0) ids.push_back(s.row_ids[i]);
    }
    platform::file_dialog::Options opts;
    opts.parent = app.main_hwnd;
    opts.title = L"Export bundle";
    opts.default_filename = to_wide(default_export_filename());
    opts.filters = {{L"Steam Account Manager bundle (*.samvault)", L"*.samvault"}, {L"All files (*.*)", L"*.*"}};
    opts.default_extension = L"samvault";
    const auto picked = platform::file_dialog::save_file(opts);
    if (!picked.ok) return;
    try {
        const bool everything = s.account_ids.empty() && ids.size() == s.row_ids.size();
        const core::Vault subset = everything ? app.vault : core::store::subset_vault(app.vault, ids);
        auto pw = crypto::make_secure(s.passphrase);
        core::store::save_bundle(picked.path, subset, pw);
        crypto::zero_buffer(pw.data(), pw.size());
        s.exported = subset.accounts.size();
        s.status = to_utf8(picked.path.wstring());
        s.done = true;
        wipe(s.passphrase);
        wipe(s.confirm);
    } catch (const std::exception& ex) {
        s.status = ex.what();
    }
}

}  // namespace

void reset_import_bundle(ImportBundleState& s) {
    wipe(s.passphrase_buf);
    s.error.clear();
    s.error_raw.clear();
    s.status.clear();
    s.have_preview = false;
    s.preview_vault = {};
    s.preview_report = {};
    s.picked_path.clear();
    s.done = false;
    s.wrong_passphrase = false;
    s.rows.clear();
}

void bundle_unlock(app::AppState& app, ImportBundleState& s) {
    s.error.clear();
    s.error_raw.clear();
    s.wrong_passphrase = false;
    try {
        auto pw = crypto::make_secure(std::string_view(s.passphrase_buf.data()));
        core::Vault imported = core::store::load_bundle(s.picked_path, pw);
        crypto::zero_buffer(pw.data(), pw.size());
        s.preview_report = core::store::preview_merge(app.vault, imported);
        s.preview_vault = std::move(imported);
        s.have_preview = true;
        wipe(s.passphrase_buf);
        build_import_rows(app, s);
    } catch (const core::store::WrongPassword&) {
        s.wrong_passphrase = true;
    } catch (const core::store::CorruptVault& ex) {
        s.error = "The bundle is damaged and cannot be opened. Export it again on the other PC.";
        s.error_raw = ex.what();
    } catch (const core::store::UnsupportedVersion& ex) {
        s.error = "A newer version of the app wrote this bundle. Update the app on this PC, then open it again.";
        s.error_raw = ex.what();
    } catch (const std::exception& ex) {
        s.error = "The file could not be read. Check that it is a .samvault bundle and that it is not open elsewhere.";
        s.error_raw = ex.what();
    }
}

void bundle_import(app::AppState& app, ImportBundleState& s) {
    std::vector<std::string> ids;
    for (const auto& r : s.rows) {
        if (r.checked) ids.push_back(s.preview_vault.accounts[r.payload].id);
    }
    try {
        if (ids.size() == s.preview_vault.accounts.size()) {
            core::store::merge_into(app.vault, std::move(s.preview_vault));
        } else {
            core::store::merge_into(app.vault, core::store::subset_vault(s.preview_vault, ids));
        }
        app.vault_dirty = true;
        app.flush_pending_save();
        for (auto& r : s.rows) {
            if (!r.checked) continue;
            r.note = r.status == RowStatus::Update ? "Replaced the account in the vault." : r.note;
            r.status = r.status == RowStatus::Update ? RowStatus::Updated : RowStatus::Imported;
        }
        detail::finish_rows(s.rows);
        s.preview_vault = {};
        s.done = true;
    } catch (const std::exception& ex) {
        s.error = "The import failed and nothing was changed. Try again; if it keeps failing, export the bundle again.";
        s.error_raw = ex.what();
    }
}

void request_export_bundle(ExportBundleState& state, std::vector<std::string> ids) {
    reset_export(state);
    state.account_ids = std::move(ids);
    state.open = true;
    g_export = &state;
}

void request_import_bundle(ImportBundleState& state, std::filesystem::path path) {
    reset_import_bundle(state);
    state.picked_path = std::move(path);
    state.open = true;
}

void build_import_rows(app::AppState& app, ImportBundleState& state) {
    state.rows.clear();
    for (std::size_t i = 0; i < state.preview_vault.accounts.size(); ++i) {
        const core::Account& a = state.preview_vault.accounts[i];
        BulkRow row;
        row.name = !a.web.persona_name.empty() ? a.web.persona_name : a.login;
        row.source = a.login;
        const bool exists = core::store::find_existing_account(app.vault, a.steam_id_64, a.login) != nullptr;
        row.status = exists ? RowStatus::Update : RowStatus::New;
        row.note = exists ? "Replaces the account in the vault." : a.is_nfa ? "NFA token" : "";
        row.checked = true;
        row.payload = i;
        state.rows.push_back(std::move(row));
    }
}

// Why one modal with the checklist, the passphrase and one line about the file: the user decides
// what leaves the vault and under which passphrase in one place.
void draw_export_bundle_modal(app::AppState& app, ExportBundleState& state) {
    const facet::Theme& t = facet::T();
    g_export = &state;
    char title_id[64];
    std::snprintf(title_id, sizeof(title_id), "Export bundle###export-bundle-%p", static_cast<void*>(&state));
    if (!facet::BeginModal(title_id, &state.open, {.width = kDialogWidth})) {
        if (state.rows_built || state.done) reset_export(state);
        return;
    }
    if (!state.rows_built) build_export_rows(app, state);

    bool close = false;
    if (state.done) {
        char title[96];
        std::snprintf(title, sizeof(title), "Exported %s account%s", kit::format_count(static_cast<std::int64_t>(state.exported)).c_str(),
                      state.exported == 1 ? "" : "s");
        facet::InlineAlert(title, nullptr, state.status.c_str(), {.tone = facet::Tone::Success});
        gap(t.spacing.sm);
        facet::StyledText("Keep the file and its passphrase apart: together they are enough to sign in.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        gap(t.spacing.lg);
        facet::BeginStack("##done", {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
        facet::StackSpacer();
        facet::BeginStack("##done-actions", {.orientation = facet::Orientation::Horizontal});
        if (facet::Button("Show in folder", {.icon = facet::Icon("folder")})) {
            open_folder(std::filesystem::path(to_wide(state.status)).parent_path());
        }
        if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) close = true;
        facet::EndStack();
        facet::EndStack();
    } else {
        facet::StyledText("Anyone with this file and its passphrase can sign in to the ticked accounts.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        gap(t.spacing.md);
        export_checklist(app, state);
        gap(t.spacing.lg);
        // The two fields side by side: the dialog does not scroll, and the account list needs the height.
        const bool mismatch = !state.confirm.empty() && state.passphrase != state.confirm;
        facet::BeginGrid("##passphrases", {.minCellWidth = 240.0F, .maxColumns = 2, .gap = facet::Gap::Loose});
        facet::GridNext();
        (void)kit::master_password_field("Passphrase", &state.passphrase, {.show_strength = true});
        facet::GridNext();
        (void)kit::master_password_field("Confirm", &state.confirm,
                                         {.error = mismatch ? "The two passphrases do not match." : nullptr});
        facet::EndGrid();
        if (!state.status.empty()) {
            gap(t.spacing.md);
            if (detail::flow_error("export-error", "The bundle was not saved",
                                   "The file could not be written there. Choose another folder.", state.status,
                                   "Choose another place")) {
                state.status.clear();
                run_export(app, state);
            }
        }
        gap(t.spacing.lg);
        const int picked = static_cast<int>(std::count(state.row_checked.begin(), state.row_checked.end(), 1));
        char label[64];
        std::snprintf(label, sizeof(label), picked == 1 ? "Save 1 account to a file" : "Save %s accounts to a file",
                      kit::format_count(picked).c_str());
        const bool valid = !state.passphrase.empty() && state.passphrase == state.confirm && picked > 0;
        bool cancel = false;
        if (footer(label, valid, &cancel)) {
            state.status.clear();
            run_export(app, state);
        }
        if (cancel) close = true;
    }
    facet::EndModal();
    if (close) {
        state.open = false;
        reset_export(state);
    }
}

// Rule 1: the export dialog holds a passphrase and a list of accounts; locking, a vault change
// and leaving the Accounts screen close it.
SAM_KIT_TRANSIENT_RESET(bundle_export, &close_export)
SAM_KIT_SCREEN_LEAVE(bundle_export_leave, app::Screen::Accounts, &close_export)

}  // namespace sam::ui::widgets
