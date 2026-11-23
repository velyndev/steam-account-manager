#include "ui/screens/add_account_detail.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/media.h>
#include <facet/widgets/overlays.h>

#include "core/account_store/store.hpp"
#include "core/crypto/rng.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/sda/info_dat.hpp"
#include "core/sda/mafile.hpp"
#include "core/steam_local/loginusers.hpp"
#include "core/steam_login/session.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/format.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/util.hpp"

#ifdef SAM_UI_CAPTURE
#include "app/ui_capture.hpp"
#endif

namespace sam::ui::screens {

namespace {

using namespace add_account_detail;

constexpr const char* kNoSteamIdNote = "No Steam ID yet: its data stays empty until it signs in to Steam on this PC.";
constexpr const char* kUnreadableMafile = "Not a readable maFile: the file is damaged or in another format.";
constexpr const char* kUnreadableInfoDat = "Not a readable info.dat: the file is damaged or in another format.";

void wipe(std::string& s) {
    crypto::zero_buffer(s.data(), s.size());
    s.clear();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The typed path (a file, or a folder scanned for `ext`) and the queued paths as one list.
// Writes *path_error when the typed path does not exist.
std::vector<std::filesystem::path> gather(const std::string& typed_raw, const std::vector<std::string>& queued,
                                          std::string_view ext, std::string* path_error) {
    std::vector<std::filesystem::path> batch;
    const std::string typed = trim_path(typed_raw);
    if (!typed.empty()) {
        const std::filesystem::path p(to_wide(typed));
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            auto found = scan_dir_for_extension(p, ext);
            batch.insert(batch.end(), found.begin(), found.end());
        } else if (std::filesystem::exists(p, ec)) {
            batch.push_back(p);
        } else {
            *path_error = "Nothing exists at this path.";
        }
    }
    for (const auto& s : queued) batch.emplace_back(to_wide(s));
    std::sort(batch.begin(), batch.end());
    batch.erase(std::unique(batch.begin(), batch.end()), batch.end());
    return batch;
}

void refresh_imported(app::AppState& state, std::vector<std::string> ids, bool validate_gc) {
    if (ids.size() == 1) {
        state.pull_all_for_account(ids.front());
    } else if (!ids.empty()) {
        state.refresh_accounts_staggered(std::move(ids));
        if (validate_gc) {
            state.start_gc_validate(false);
        } else {
            state.start_gc_autopull();
        }
    }
}

// --- maFile ------------------------------------------------------------------------------------

struct MafileImportResult {
    bool ok = false;
    bool merged = false;
    std::string error;
    std::string account_id;
    std::string login;
    std::string raw;
};

MafileImportResult import_one_mafile(app::AppState& state, const std::filesystem::path& path,
                                     const std::string& mafile_password, const std::string& steam_password) {
    MafileImportResult r;
    try {
        auto loaded = sam::sda::load_mafile(path, crypto::make_secure(mafile_password));

        std::uint64_t resolved_sid = loaded.session_steam_id;
        if (resolved_sid == 0 && !loaded.guard.account_name.empty()) {
            resolved_sid = steam_local::lookup_steam_id(loaded.guard.account_name);
        }
        const bool has_tokens = !loaded.session_access_token.empty() && !loaded.session_refresh_token.empty();
        auto access = crypto::make_secure(loaded.session_access_token);
        auto refresh = crypto::make_secure(loaded.session_refresh_token);

        if (resolved_sid == 0 && has_tokens) resolved_sid = steam_login::jwt_steam_id(access);

        core::Account* existing =
            core::store::find_existing_account(state.vault, resolved_sid, loaded.guard.account_name);

        if (existing) {
            existing->sda = std::move(loaded.guard);
            if (existing->steam_id_64 == 0 && resolved_sid != 0) existing->steam_id_64 = resolved_sid;
            if (has_tokens && resolved_sid != 0) {
                existing->access_token = access;
                existing->refresh_token = refresh;
                existing->access_token_expires = steam_login::jwt_expiry(access);
                existing->steam_login_secure =
                    crypto::make_secure(steam_login::make_steam_login_secure(resolved_sid, access));
                if (existing->session_id.empty()) existing->session_id = crypto::random_session_id();
            }
            if (!steam_password.empty()) existing->password = crypto::make_secure(steam_password);
            r.account_id = existing->id;
            r.login = existing->login;
            r.merged = true;
        } else {
            core::Account a;
            a.id = generate_ulid();
            a.login = loaded.guard.account_name;
            a.sda = std::move(loaded.guard);
            a.steam_id_64 = resolved_sid;
            if (has_tokens && resolved_sid != 0) {
                a.access_token = access;
                a.refresh_token = refresh;
                a.access_token_expires = steam_login::jwt_expiry(access);
                a.steam_login_secure = crypto::make_secure(steam_login::make_steam_login_secure(resolved_sid, access));
                a.session_id = crypto::random_session_id();
            }
            if (!steam_password.empty()) a.password = crypto::make_secure(steam_password);
            a.created_unix = now_seconds();
            r.account_id = a.id;
            r.login = a.login;
            state.vault.accounts.push_back(std::move(a));
        }
        r.ok = true;
    } catch (const sam::sda::MafileEncrypted&) {
        r.error = "Encrypted: enter the file password and scan again.";
    } catch (const sam::sda::MafileWrongPassword&) {
        r.error = "The file password is wrong.";
    } catch (const std::exception& ex) {
        r.error = kUnreadableMafile;
        r.raw = ex.what();
    }
    return r;
}

struct MafileFlow {
    enum class Phase { Pick, Preview, Result };
    Phase phase = Phase::Pick;
    std::string path;
    std::string path_error;
    std::vector<std::string> queued;
    std::string mafile_password;
    std::string steam_password;
    bool steam_password_open = false;
    std::vector<std::filesystem::path> files;
    std::vector<BulkRow> rows;
    std::string single_account_id;
    int encrypted = 0;

    void reset() {
        wipe(mafile_password);
        wipe(steam_password);
        *this = {};
    }
};

MafileFlow& mafile_flow() {
    static MafileFlow f;
    return f;
}

// Reads every file once without touching the vault, so the preview says what each will do.
void scan_mafiles(app::AppState& state, MafileFlow& f) {
    f.path_error.clear();
    f.files = gather(f.path, f.queued, ".mafile", &f.path_error);
    f.rows.clear();
    f.encrypted = 0;
    if (!f.path_error.empty()) return;
    if (f.files.empty()) {
        f.path_error = "No .maFile files in this folder.";
        return;
    }
    for (std::size_t i = 0; i < f.files.size(); ++i) {
        const auto& p = f.files[i];
        BulkRow row;
        row.source = to_utf8(p.filename().wstring());
        row.name = to_utf8(p.stem().wstring());
        row.payload = i;
        try {
            const auto loaded = sam::sda::load_mafile(p, crypto::make_secure(f.mafile_password));
            std::uint64_t sid = loaded.session_steam_id;
            if (sid == 0 && !loaded.guard.account_name.empty()) sid = steam_local::lookup_steam_id(loaded.guard.account_name);
            if (sid == 0 && !loaded.session_access_token.empty() && !loaded.session_refresh_token.empty()) {
                sid = steam_login::jwt_steam_id(crypto::make_secure(loaded.session_access_token));
            }
            if (!loaded.guard.account_name.empty()) row.name = loaded.guard.account_name;
            const bool exists =
                core::store::find_existing_account(state.vault, sid, loaded.guard.account_name) != nullptr;
            row.status = exists ? RowStatus::Update : RowStatus::New;
            row.note = exists ? "Adds the authenticator to the account in the vault."
                       : sid == 0 ? kNoSteamIdNote
                                  : "";
        } catch (const sam::sda::MafileEncrypted&) {
            row.status = RowStatus::Blocked;
            row.note = "Encrypted: needs the file password.";
            ++f.encrypted;
        } catch (const sam::sda::MafileWrongPassword&) {
            row.status = RowStatus::Blocked;
            row.note = "The file password is wrong.";
            ++f.encrypted;
        } catch (const std::exception& ex) {
            row.status = RowStatus::Blocked;
            row.note = kUnreadableMafile;
            row.raw = ex.what();
        }
        row.checked = row_importable(row);
        f.rows.push_back(std::move(row));
    }
    f.phase = MafileFlow::Phase::Preview;
}

void import_mafiles(app::AppState& state, MafileFlow& f) {
    std::vector<std::string> ids;
    for (auto& row : f.rows) {
        if (!row_importable(row) || !row.checked) continue;
        const auto r = import_one_mafile(state, f.files[row.payload], f.mafile_password, f.steam_password);
        if (!r.ok) {
            row.status = RowStatus::Failed;
            row.note = r.error;
            row.raw = r.raw;
            continue;
        }
        row.status = r.merged ? RowStatus::Updated : RowStatus::Imported;
        row.account_id = r.account_id;
        if (!r.login.empty()) row.name = r.login;
        const core::Account* a = state.find_account(r.account_id);
        row.note = (a != nullptr && a->steam_id_64 == 0) ? kNoSteamIdNote
                   : r.merged                              ? "Added the authenticator to the account in the vault."
                                                           : "";
        ids.push_back(r.account_id);
    }
    finish_rows(f.rows);
    state.vault_dirty = true;
    state.save_vault_if_dirty();
    f.single_account_id = ids.size() == 1 ? ids.front() : std::string{};
    refresh_imported(state, std::move(ids), false);
    wipe(f.mafile_password);
    wipe(f.steam_password);
    f.path.clear();
    f.queued.clear();
    f.phase = MafileFlow::Phase::Result;
}

void pick_mafiles(app::AppState& state, MafileFlow& f) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Choose maFiles";
    opts.allow_multiselect = true;
    opts.filters = {{L"Steam mobile authenticator (*.maFile)", L"*.maFile"}, {L"All files (*.*)", L"*.*"}};
    const auto picked = platform::file_dialog::open_file(opts);
    if (!picked.ok) return;
    if (picked.paths.empty()) {
        f.queued.push_back(to_utf8(picked.path.wstring()));
    } else {
        for (const auto& p : picked.paths) f.queued.push_back(to_utf8(p.wstring()));
    }
    scan_mafiles(state, f);
}

void pick_folder_into(app::AppState& state, std::string* path, const wchar_t* title) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = title;
    const auto picked = platform::file_dialog::pick_folder(opts);
    if (picked.ok) *path = to_utf8(picked.path.wstring());
}

// The drop target of a pick step. The files arrive through the window's own drop handler (any
// spot of the window works); a click opens the file dialog.
bool drop_zone(const char* label, const char* hint_text) {
    static const char* const kAccept[] = {"SAM_FILES"};
    return facet::DropZone(label, kAccept, {.hint = hint_text, .icon = facet::Icon("upload")}) ==
           facet::DropZoneAction::Activated;
}

constexpr const char* kFileSteps[] = {"Choose files", "Review", "Done"};

// The line above a preview table: "Select all" at the start; at the end the file password when
// some files are locked, and `extra` (a popover button) when given.
template <typename Extra>
void preview_tools(std::vector<BulkRow>& rows, int locked, std::string* password, bool* rescan, Extra extra) {
    facet::BeginStack("##tools", {.orientation = facet::Orientation::Horizontal,
                                  .align = facet::LayoutAlign::Center,
                                  .width = facet::AvailableWidthUnits()});
    select_all_box("##select", rows);
    facet::StackSpacer();
    facet::BeginStack("##tools-end", {.orientation = facet::Orientation::Horizontal, .align = facet::LayoutAlign::Center});
    if (locked > 0) {
        char placeholder[80];
        std::snprintf(placeholder, sizeof(placeholder), "Unlocks %d encrypted file%s", locked, locked == 1 ? "" : "s");
        facet::SecureField("File password", password, {.placeholder = placeholder, .width = 360.0F});
        ImGui::BeginDisabled(password->empty());
        if (facet::Button("Read again")) *rescan = true;
        ImGui::EndDisabled();
    }
    extra();
    facet::EndStack();
    facet::EndStack();
    gap(facet::T().spacing.sm);
}

// Why the shared flow shell: choose, review and done read as three steps of one flow, the table
// fills the card so only it scrolls, and the commit button stays in the bar under it.
void draw_mafile(app::AppState& state) {
    MafileFlow& f = mafile_flow();
    const facet::Theme& t = facet::T();
    const bool privacy = state.settings.privacy_mode;

    if (f.phase == MafileFlow::Phase::Pick) {
        begin_flow("##mafile", kFileSteps, 0, kFormWidth);
        begin_step_card("##mafile-card", "Choose maFiles",
                        "Each .maFile holds one account's Steam Guard secrets. Accounts already in the vault get the "
                        "authenticator added.");
        if (drop_zone("Drop .maFile files or folders##mafile-drop",
                      "Drop .maFile files or whole folders anywhere in this window, or click to choose files")) {
            pick_mafiles(state, f);
        }
        gap(t.spacing.lg);
        facet::PathField("Or a path", &f.path,
                         {.placeholder = "A .maFile, or a folder of them",
                          .error = f.path_error.empty() ? nullptr : f.path_error.c_str(),
                          .helpText = "Browse picks a folder; every .maFile in it is read."});
        if (facet::PathFieldBrowsePressed()) pick_folder_into(state, &f.path, L"Choose a folder of maFiles");
        facet::SecureField("File password", &f.mafile_password, {.placeholder = "Only for encrypted maFiles"});
        end_step_card();
        begin_commit_bar();
        ImGui::BeginDisabled(trim_path(f.path).empty() && f.queued.empty());
        if (facet::Button("Read maFiles", {.variant = facet::ButtonVariant::Primary})) scan_mafiles(state, f);
        ImGui::EndDisabled();
        end_flow();
        return;
    }

    if (f.phase == MafileFlow::Phase::Preview) {
        begin_flow("##mafile", kFileSteps, 1, kWideWidth);
        char title[64];
        std::snprintf(title, sizeof(title), "%s maFile%s found", kit::format_count(static_cast<std::int64_t>(f.rows.size())).c_str(),
                      f.rows.size() == 1 ? "" : "s");
        begin_step_card("##mafile-review", nullptr, nullptr, true);
        bool rescan = false;
        static bool steam_pw_open = false;
        bulk_header(title, f.rows, false);
        preview_tools(f.rows, f.encrypted, &f.mafile_password, &rescan, [&] {
            if (facet::Button(f.steam_password.empty() ? "Set a Steam password" : "Steam password set",
                              {.icon = facet::Icon("lock")})) {
                steam_pw_open = !steam_pw_open;
            }
            if (facet::BeginPopover("##steam-pw", &steam_pw_open, {.width = 360.0F})) {
                facet::SecureField("##steam-password", &f.steam_password, {.placeholder = "Steam password"});
                hint("Stored for every selected account. Each can get its own later in Edit account.");
                facet::EndPopover();
            }
        });
        bulk_table("##mafile-rows", f.rows, {.hide_names = privacy, .source_header = "File"});
        end_step_card();
        if (rescan) scan_mafiles(state, f);
        const std::string status = selection_status(f.rows) + ". Nothing is written until you import.";
        begin_commit_bar(status.c_str());
        if (facet::Button("Choose other files")) f.reset();
        const int n = selected_count(f.rows);
        char label[64];
        std::snprintf(label, sizeof(label), n == 1 ? "Import 1 account" : "Import %s accounts", kit::format_count(n).c_str());
        ImGui::BeginDisabled(n == 0);
        if (facet::Button(label, {.variant = facet::ButtonVariant::Primary})) import_mafiles(state, f);
        ImGui::EndDisabled();
        end_flow();
        return;
    }

    begin_flow("##mafile", kFileSteps, 3, kWideWidth);
    kit::web_api_key_notice(state, {.compact = true});
    if (state.settings.web_api_key.empty()) gap(t.spacing.sm);
    begin_step_card("##mafile-done", nullptr, nullptr, true);
    bulk_header("Import finished", f.rows, true);
    bulk_table("##mafile-results", f.rows, {.selectable = false, .hide_names = privacy, .source_header = "File"});
    end_step_card();
    begin_commit_bar();
    const std::string id = f.single_account_id;
    if (facet::Button("Import more maFiles")) {
        f.reset();
    } else {
        open_accounts_button(state, id);
    }
    end_flow();
}
// --- info.dat ----------------------------------------------------------------------------------

struct InfoDatFlow {
    enum class Phase { Pick, Preview, Result };
    Phase phase = Phase::Pick;
    std::string path;
    std::string path_error;
    std::vector<std::string> queued;
    std::string file_password;
    std::string field_key;
    bool keys_open = false;
    std::vector<sam::sda::InfoDatAccount> entries;
    std::vector<BulkRow> rows;
    std::string single_account_id;
    int locked_files = 0;
    int files_read = 0;

    void wipe_entries() {
        for (auto& e : entries) {
            wipe(e.password);
            wipe(e.shared_secret);
        }
        entries.clear();
    }
    void reset() {
        wipe_entries();
        wipe(file_password);
        wipe(field_key);
        *this = {};
    }
};

InfoDatFlow& info_dat_flow() {
    static InfoDatFlow f;
    return f;
}

std::uint64_t entry_steam_id(const sam::sda::InfoDatAccount& e) {
    std::uint64_t sid = 0;
    try {
        sid = e.steam_id_str.empty() ? 0 : std::stoull(e.steam_id_str);
    } catch (...) {
        sid = 0;
    }
    if (sid == 0) sid = steam_local::lookup_steam_id(e.name);
    return sid;
}

void scan_info_dat(app::AppState& state, InfoDatFlow& f) {
    f.path_error.clear();
    const auto files = gather(f.path, f.queued, ".dat", &f.path_error);
    f.wipe_entries();
    f.rows.clear();
    f.locked_files = 0;
    f.files_read = 0;
    if (!f.path_error.empty()) return;
    if (files.empty()) {
        f.path_error = "No .dat files in this folder.";
        return;
    }
    for (const auto& p : files) {
        const std::string file_label = to_utf8(p.filename().wstring());
        try {
            auto entries = sam::sda::load_info_dat(p, crypto::make_secure(f.file_password), f.field_key);
            ++f.files_read;
            if (entries.empty()) {
                f.rows.push_back({.name = file_label, .source = file_label, .note = "The file lists no accounts.",
                                  .status = RowStatus::Blocked});
            }
            for (auto& e : entries) {
                BulkRow row;
                row.source = file_label;
                if (e.name.empty()) {
                    row.name = e.alias.empty() ? std::string("Unnamed entry") : e.alias;
                    row.status = RowStatus::Blocked;
                    row.note = "The entry has no account name.";
                    f.rows.push_back(std::move(row));
                    continue;
                }
                row.name = e.name;
                const std::uint64_t sid = entry_steam_id(e);
                const bool exists = core::store::find_existing_account(state.vault, sid, e.name) != nullptr;
                row.status = exists ? RowStatus::Update : RowStatus::New;
                if (exists) {
                    row.note = "Replaces the stored details of the account in the vault.";
                } else if (sid == 0) {
                    row.note = kNoSteamIdNote;
                } else if (e.shared_secret.empty()) {
                    row.note = "No Steam Guard secret in the file.";
                }
                row.payload = f.entries.size();
                row.checked = true;
                f.entries.push_back(std::move(e));
                f.rows.push_back(std::move(row));
            }
        } catch (const sam::sda::InfoDatEncrypted&) {
            ++f.locked_files;
            f.rows.push_back({.name = file_label, .source = file_label,
                              .note = "Encrypted: needs the file password.", .status = RowStatus::Blocked});
        } catch (const sam::sda::InfoDatWrongPassword&) {
            ++f.locked_files;
            f.rows.push_back({.name = file_label, .source = file_label, .note = "The file password is wrong.",
                              .status = RowStatus::Blocked});
        } catch (const std::exception& ex) {
            f.rows.push_back({.name = file_label, .source = file_label, .note = kUnreadableInfoDat,
                              .status = RowStatus::Blocked, .raw = ex.what()});
        }
    }
    f.phase = InfoDatFlow::Phase::Preview;
}

void apply_info_dat_entry(core::Account& a, const sam::sda::InfoDatAccount& e, std::uint64_t sid64, bool fresh) {
    a.login = e.name;
    if (!e.password.empty()) a.password = crypto::make_secure(e.password);
    if (sid64 != 0) a.steam_id_64 = sid64;

    if (!e.shared_secret.empty()) {
        if (!a.sda.has_value()) {
            core::SteamGuardAccount g;
            g.account_name = e.name;
            a.sda = std::move(g);
        }
        a.sda->shared_secret = e.shared_secret;
        if (a.sda->account_name.empty()) a.sda->account_name = e.name;
    }

    if (!e.description.empty()) a.notes = to_wide(e.description);
    if (!e.profile_url.empty()) a.web.profile_url = e.profile_url;
    if (!e.avatar_url.empty()) a.web.avatar_url_full = e.avatar_url;

    a.bans.community_banned = e.community_banned;
    a.bans.vac_banned = e.vac_banned;
    a.bans.vac_ban_count = e.vac_ban_count;
    a.bans.game_ban_count = e.game_ban_count;
    a.bans.days_since_last_ban = e.days_since_last_ban;
    if (!e.economy_ban.empty()) a.bans.economy_ban = e.economy_ban;

    if (fresh) a.created_unix = now_seconds();
}

void import_info_dat(app::AppState& state, InfoDatFlow& f) {
    std::vector<std::string> ids;
    for (auto& row : f.rows) {
        if (!row_importable(row) || !row.checked) continue;
        const auto& e = f.entries[row.payload];
        const std::uint64_t sid64 = entry_steam_id(e);
        if (core::Account* existing = core::store::find_existing_account(state.vault, sid64, e.name)) {
            apply_info_dat_entry(*existing, e, sid64, false);
            row.account_id = existing->id;
            row.status = RowStatus::Updated;
        } else {
            core::Account a;
            a.id = generate_ulid();
            apply_info_dat_entry(a, e, sid64, true);
            row.account_id = a.id;
            row.status = RowStatus::Imported;
            state.vault.accounts.push_back(std::move(a));
        }
        const core::Account* acc = state.find_account(row.account_id);
        row.note = (acc != nullptr && acc->steam_id_64 == 0) ? kNoSteamIdNote
                   : row.status == RowStatus::Updated        ? "Replaced the stored details of the account."
                                                             : "";
        ids.push_back(row.account_id);
    }
    finish_rows(f.rows);
    state.vault_dirty = true;
    state.save_vault_if_dirty();
    f.single_account_id = ids.size() == 1 ? ids.front() : std::string{};
    refresh_imported(state, std::move(ids), false);
    f.wipe_entries();
    wipe(f.file_password);
    f.path.clear();
    f.queued.clear();
    f.phase = InfoDatFlow::Phase::Result;
}

void info_dat_keys(InfoDatFlow& f) {
    facet::SecureField("File password", &f.file_password, {.placeholder = "Only for encrypted files"});
    if (facet::BeginDisclosure("Passwords come out unreadable?", &f.keys_open)) {
        facet::SecureField("Extra key", &f.field_key,
                           {.helpText = "Some SDA versions protect each password with a second key. Leave it empty "
                                        "unless the imported passwords look wrong."});
        facet::EndDisclosure();
    }
}
void pick_info_dat(app::AppState& state, InfoDatFlow& f) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Choose info.dat";
    opts.allow_multiselect = true;
    opts.filters = {{L"info.dat (*.dat)", L"*.dat"}, {L"All files (*.*)", L"*.*"}};
    const auto picked = platform::file_dialog::open_file(opts);
    if (!picked.ok) return;
    if (picked.paths.empty()) {
        f.queued.push_back(to_utf8(picked.path.wstring()));
    } else {
        for (const auto& p : picked.paths) f.queued.push_back(to_utf8(p.wstring()));
    }
    scan_info_dat(state, f);
}

void draw_info_dat(app::AppState& state) {
    InfoDatFlow& f = info_dat_flow();
    const facet::Theme& t = facet::T();
    const bool privacy = state.settings.privacy_mode;

    if (f.phase == InfoDatFlow::Phase::Pick) {
        begin_flow("##info-dat", kFileSteps, 0, kFormWidth);
        begin_step_card("##info-dat-card", "Choose info.dat files",
                        "An info.dat export lists several accounts with their passwords and Steam Guard secrets.");
        if (drop_zone("Drop info.dat files##info-dat-drop",
                      "Drop info.dat files anywhere in this window, or click to choose")) {
            pick_info_dat(state, f);
        }
        gap(t.spacing.lg);
        facet::PathField("Or a path", &f.path,
                         {.placeholder = "An info.dat, or a folder of .dat files",
                          .error = f.path_error.empty() ? nullptr : f.path_error.c_str(),
                          .helpText = "Browse picks a folder; every .dat file in it is read."});
        if (facet::PathFieldBrowsePressed()) pick_folder_into(state, &f.path, L"Choose a folder of info.dat files");
        info_dat_keys(f);
        end_step_card();
        begin_commit_bar();
        ImGui::BeginDisabled(trim_path(f.path).empty() && f.queued.empty());
        if (facet::Button("Read info.dat", {.variant = facet::ButtonVariant::Primary})) scan_info_dat(state, f);
        ImGui::EndDisabled();
        end_flow();
        return;
    }

    if (f.phase == InfoDatFlow::Phase::Preview) {
        begin_flow("##info-dat", kFileSteps, 1, kWideWidth);
        const int accounts = static_cast<int>(f.entries.size());
        char title[96];
        std::snprintf(title, sizeof(title), "%s account%s in %d file%s", kit::format_count(accounts).c_str(),
                      accounts == 1 ? "" : "s", f.files_read, f.files_read == 1 ? "" : "s");
        begin_step_card("##info-dat-review", nullptr, nullptr, true);
        bulk_header(title, f.rows, false);
        bool rescan = false;
        preview_tools(f.rows, f.locked_files, &f.file_password, &rescan, [] {});
        bulk_table("##info-dat-rows", f.rows, {.hide_names = privacy, .source_header = "File"});
        end_step_card();
        if (rescan) scan_info_dat(state, f);
        const std::string status = selection_status(f.rows) + ". Nothing is written until you import.";
        begin_commit_bar(status.c_str());
        if (facet::Button("Choose other files")) f.reset();
        const int n = selected_count(f.rows);
        char label[64];
        std::snprintf(label, sizeof(label), n == 1 ? "Import 1 account" : "Import %s accounts", kit::format_count(n).c_str());
        ImGui::BeginDisabled(n == 0);
        if (facet::Button(label, {.variant = facet::ButtonVariant::Primary})) import_info_dat(state, f);
        ImGui::EndDisabled();
        end_flow();
        return;
    }

    begin_flow("##info-dat", kFileSteps, 3, kWideWidth);
    kit::web_api_key_notice(state, {.compact = true});
    if (state.settings.web_api_key.empty()) gap(t.spacing.sm);
    begin_step_card("##info-dat-done", nullptr, nullptr, true);
    bulk_header("Import finished", f.rows, true);
    bulk_table("##info-dat-results", f.rows, {.selectable = false, .hide_names = privacy, .source_header = "File"});
    end_step_card();
    begin_commit_bar();
    const std::string id = f.single_account_id;
    if (facet::Button("Import more files")) {
        f.reset();
    } else {
        open_accounts_button(state, id);
    }
    end_flow();
}
// --- NFA token ---------------------------------------------------------------------------------

struct LoginToken {
    std::string login;
    std::string token;
};

LoginToken split_login_token(std::string raw) {
    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
    };
    raw = trim(std::move(raw));
    const auto first = raw.find("----");
    if (first == std::string::npos) return {std::string{}, raw};
    std::string login = trim(raw.substr(0, first));
    std::string rest = raw.substr(first + 4);
    const auto second = rest.find("----");
    std::string token = (second == std::string::npos) ? rest : rest.substr(0, second);
    return {std::move(login), trim(std::move(token))};
}

struct NfaFlow {
    std::string token;
    std::string checked_text;  // the text `check` was made from
    JwtCheck check;
    bool done = false;
    JwtImportResult result;

    void reset() {
        wipe(token);
        wipe(checked_text);
        *this = {};
    }
};

NfaFlow& nfa_flow() {
    static NfaFlow f;
    return f;
}

std::string days_text(std::int64_t expires) {
    const std::int64_t left = expires - now_seconds();
    if (left <= 0) return "Expired";
    const long long days = static_cast<long long>(left / 86400);
    char buf[48];
    if (days < 1) {
        std::snprintf(buf, sizeof(buf), "In %lld hours", static_cast<long long>(left / 3600));
    } else {
        std::snprintf(buf, sizeof(buf), days == 1 ? "In 1 day" : "In %lld days", days);
    }
    return buf;
}

// The warnings a token can carry, each next to the token it is about.
void token_warnings(bool expired, bool client_audience, bool steam_issuer, bool no_login, bool added) {
    const facet::Theme& t = facet::T();
    if (expired) {
        facet::InlineAlert("The token has expired##nfa-expired", nullptr,
                           added ? "The account was added, but signing in fails until you import a fresh token."
                                 : "The account can be added, but signing in fails until you import a fresh token.",
                           {.tone = facet::Tone::Destructive});
        gap(t.spacing.sm);
    }
    if (!client_audience) {
        facet::InlineAlert("Not a Steam client token##nfa-audience", nullptr,
                           "The token's audience does not include \"client\", so the Steam client may refuse it at "
                           "sign-in.",
                           {.tone = facet::Tone::Warning});
        gap(t.spacing.sm);
    }
    if (!steam_issuer) {
        facet::InlineAlert("Not issued by Steam##nfa-issuer", nullptr,
                           "The token's issuer is not Steam, so it may not be a Steam refresh token at all.",
                           {.tone = facet::Tone::Warning});
        gap(t.spacing.sm);
    }
    if (no_login) {
        facet::InlineAlert("No login in the token##nfa-login", nullptr,
                           "Sign-in needs the account's login. Paste it as login----token, or set it later in Edit "
                           "account.",
                           {.tone = facet::Tone::Warning});
        gap(t.spacing.sm);
    }
}

constexpr const char* kTokenSteps[] = {"Paste the token", "Done"};

// Why live validation: a token is one long opaque string, so the user learns at once whether it
// parses, whose it is and when it expires, before anything is stored.
void draw_nfa(app::AppState& state) {
    NfaFlow& f = nfa_flow();
    const facet::Theme& t = facet::T();

    if (f.done) {
        begin_flow("##nfa", kTokenSteps, 2, kFormWidth);
        const auto& r = f.result;
        const core::Account* acc = state.find_account(r.account_id);
        const std::string name = acc != nullptr && !acc->login.empty() ? kit::login_text(state, *acc)
                                                                       : std::string("the account");
        const std::string title = std::string(r.merged ? "Updated " : "Added ") + name;
        facet::BeginCard("##nfa-done");
        done_header(title.c_str(), r.merged ? "The account signs in with the new token now."
                                            : "The account is in the NFA group and signs in with this token.",
                    r.expired);
        token_warnings(r.expired, r.client_audience, r.steam_issuer, r.login.empty(), true);
        facet::EndCard();
        gap(t.spacing.sm);
        kit::web_api_key_notice(state, {.compact = true});
        begin_commit_bar();
        const std::string id = r.account_id;
        if (facet::Button("Paste another token")) {
            f.reset();
        } else {
            open_accounts_button(state, id);
        }
        end_flow();
        return;
    }

    begin_flow("##nfa", kTokenSteps, 0, kFormWidth);
    begin_step_card("##nfa-card", "Paste the token",
                    "An NFA account has no password: the token is its only credential, stored encrypted.");
    if (f.token != f.checked_text) {
        wipe(f.checked_text);
        f.checked_text = f.token;
        f.check = check_jwt_token(f.token);
    }
    const bool has_text = !trim_path(f.token).empty();
    facet::TextArea("##token", &f.token,
                    {.placeholder = "login----eyJhbGciOi...",
                     .minLines = 3,
                     .maxLines = 6,
                     .error = has_text && !f.check.ok ? f.check.error.c_str() : nullptr,
                     .helpText = "Paste the whole line: the login, four dashes, then the token. A token without the "
                                 "login works too, but then the account has no login to sign in with."});

    if (has_text && f.check.ok) {
        gap(t.spacing.md);
        facet::BeginKeyValueList("##nfa-facts");
        facet::KeyValue("Login", f.check.login.empty() ? "Not in the line"
                                 : state.settings.privacy_mode ? kit::kHiddenLogin
                                                               : f.check.login.c_str());
        const std::string sid = f.check.steam_id != 0 ? std::to_string(f.check.steam_id) : std::string("Unknown");
        facet::KeyValue("Steam ID", sid.c_str());
        const std::string exp = days_text(f.check.expires);
        facet::KeyValue("Expires", exp.c_str(), {.tone = f.check.expired ? facet::Tone::Destructive : facet::Tone::Neutral});
        const bool exists = core::store::find_existing_account(state.vault, f.check.steam_id, f.check.login) != nullptr;
        facet::KeyValue("In the vault", exists ? "Yes: its token is replaced" : "No: a new NFA account");
        facet::EndKeyValueList();
        gap(t.spacing.md);
        token_warnings(f.check.expired, f.check.client_audience, f.check.steam_issuer, f.check.login.empty(), false);
    }
    end_step_card();

    begin_commit_bar();
    ImGui::BeginDisabled(!(has_text && f.check.ok));
    if (facet::Button("Add NFA account", {.variant = facet::ButtonVariant::Primary})) {
        f.result = import_jwt_token(state, f.token);
        if (f.result.ok) {
            state.vault_dirty = true;
            state.save_vault_if_dirty();
            state.nfa_dead_notified.erase(f.result.account_id);
            state.pull_all_for_account(f.result.account_id);
            wipe(f.token);
            wipe(f.checked_text);
            f.done = true;
        } else {
            f.check.ok = false;
            f.check.error = f.result.error;
        }
    }
    ImGui::EndDisabled();
    end_flow();
}
}  // namespace

namespace add_account_detail {

JwtCheck check_jwt_token(const std::string& raw) {
    JwtCheck c;
    LoginToken lt = split_login_token(raw);
    c.login = lt.login;
    if (lt.token.empty()) {
        c.error = "Paste a token first.";
        return c;
    }
    auto rt = crypto::make_secure(lt.token);
    crypto::zero_buffer(lt.token.data(), lt.token.size());
    const std::int64_t exp = steam_login::jwt_expiry(rt);
    if (exp == 0) {
        c.error = "This is not a Steam token: its expiry date could not be read. Paste the whole line from where "
                  "you copied it.";
        return c;
    }
    c.client_audience = steam_login::jwt_audience(rt).find("client") != std::string::npos;
    c.steam_issuer = steam_login::jwt_issuer(rt).find("steam") != std::string::npos;
    c.expires = exp;
    c.expired = exp <= now_seconds();
    std::uint64_t sid = steam_login::jwt_steam_id(rt);
    if (sid == 0 && !c.login.empty()) sid = steam_local::lookup_steam_id(c.login);
    c.steam_id = sid;
    c.ok = true;
    return c;
}

JwtImportResult import_jwt_token(app::AppState& state, const std::string& raw, bool assign_nfa_group) {
    JwtImportResult r;
    LoginToken lt = split_login_token(raw);
    std::string login_hint = lt.login;
    if (lt.token.empty()) {
        r.error = "Paste a token first.";
        return r;
    }

    auto rt = crypto::make_secure(lt.token);
    crypto::zero_buffer(lt.token.data(), lt.token.size());
    const std::int64_t exp = steam_login::jwt_expiry(rt);
    if (exp == 0) {
        r.error = "This is not a Steam token: its expiry date could not be read. Paste the whole line from where "
                  "you copied it.";
        return r;
    }
    const std::string aud = steam_login::jwt_audience(rt);
    r.client_audience = aud.find("client") != std::string::npos;
    r.steam_issuer = steam_login::jwt_issuer(rt).find("steam") != std::string::npos;
    r.expires = exp;
    r.expired = exp <= now_seconds();

    std::uint64_t sid = steam_login::jwt_steam_id(rt);
    if (sid == 0 && !login_hint.empty()) sid = steam_local::lookup_steam_id(login_hint);
    r.steam_id = sid;

    auto apply = [&](core::Account& a, bool fresh) {
        a.refresh_token = rt;
        a.refresh_token_expires = exp;
        if (sid != 0) a.steam_id_64 = sid;
        if (!login_hint.empty()) a.login = login_hint;
        if (a.session_id.empty()) a.session_id = crypto::random_session_id();
        if (fresh) a.created_unix = now_seconds();

        const bool nfa = a.password.empty();
        a.is_nfa = nfa;
        if (nfa && assign_nfa_group) a.group_id = core::store::ensure_nfa_group(state.vault);
    };

    core::Account* existing = core::store::find_existing_account(state.vault, sid, login_hint);
    if (existing) {
        apply(*existing, false);
        r.account_id = existing->id;
        r.login = existing->login;
        r.merged = true;
    } else {
        core::Account a;
        a.id = generate_ulid();
        apply(a, true);
        r.account_id = a.id;
        r.login = a.login;
        state.vault.accounts.push_back(std::move(a));
    }
    r.ok = true;
    return r;
}

void draw_mafile_import(app::AppState& state) { draw_mafile(state); }
void reset_mafile_import() { mafile_flow().reset(); }
void queue_mafiles(app::AppState& state, std::vector<std::string> paths) {
    MafileFlow& f = mafile_flow();
    if (f.phase == MafileFlow::Phase::Result) f.reset();
    f.queued.insert(f.queued.end(), std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
    scan_mafiles(state, f);
}

void draw_info_dat_import(app::AppState& state) { draw_info_dat(state); }
void reset_info_dat_import() { info_dat_flow().reset(); }
void queue_info_dat(app::AppState& state, std::vector<std::string> paths) {
    InfoDatFlow& f = info_dat_flow();
    if (f.phase == InfoDatFlow::Phase::Result) f.reset();
    f.queued.insert(f.queued.end(), std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
    scan_info_dat(state, f);
}

void draw_nfa_token(app::AppState& state) { draw_nfa(state); }
void reset_nfa_token() { nfa_flow().reset(); }

}  // namespace add_account_detail

#ifdef SAM_UI_CAPTURE
namespace {

using app::AppState;

void setup_base(AppState& state, Method m) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::AddAccount;
    reset_screen();
    open_method(m);
    mark_seeded(state);
}

void setup_chooser(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::AddAccount;
    reset_screen();
    mark_seeded(state);
}

void setup_mafile(AppState& state) { setup_base(state, Method::MaFile); }
void setup_info_dat(AppState& state) { setup_base(state, Method::InfoDat); }
void setup_nfa(AppState& state) { setup_base(state, Method::NfaToken); }

void setup_nfa_error(AppState& state) {
    setup_base(state, Method::NfaToken);
    nfa_flow().token = "demo-login----not-a-token";
}

// A folder of 40 maFiles with every kind of outcome, seeded without reading any file.
std::vector<BulkRow> demo_rows(const AppState& state, bool result) {
    std::vector<BulkRow> rows;
    for (int i = 0; i < 40; ++i) {
        BulkRow r;
        char name[32];
        std::snprintf(name, sizeof(name), "demo-import-%02d", i);
        r.name = name;
        char file[48];
        std::snprintf(file, sizeof(file), "7656119800000%04d.maFile", i);
        r.source = file;
        r.payload = static_cast<std::size_t>(i);
        if (i < 4 && static_cast<std::size_t>(i) < state.vault.accounts.size()) {
            r.name = state.vault.accounts[static_cast<std::size_t>(i)].login;
            r.status = RowStatus::Update;
            r.note = "Adds the authenticator to the account in the vault.";
        } else if (i == 6 || i == 11) {
            r.status = RowStatus::Blocked;
            r.note = "Encrypted: needs the file password.";
        } else if (i == 9) {
            r.status = RowStatus::Blocked;
            r.note = kUnreadableMafile;
            r.raw = "[json.exception.parse_error.101] unexpected end of input";
        } else {
            r.status = RowStatus::New;
            r.note = i % 7 == 3 ? kNoSteamIdNote : "";
        }
        r.checked = row_importable(r) && i != 15;
        if (result && row_importable(r) && r.checked) {
            if (i == 7) {
                r.status = RowStatus::Failed;
                r.note = kUnreadableMafile;
                r.raw = "the file changed after it was read";
            } else {
                r.note = r.status == RowStatus::Update ? "Added the authenticator to the account in the vault." : r.note;
                r.status = r.status == RowStatus::Update ? RowStatus::Updated : RowStatus::Imported;
            }
        }
        rows.push_back(std::move(r));
    }
    if (result) finish_rows(rows);
    return rows;
}

void setup_mafile_preview(AppState& state) {
    setup_base(state, Method::MaFile);
    MafileFlow& f = mafile_flow();
    f.rows = demo_rows(state, false);
    f.encrypted = 2;
    f.phase = MafileFlow::Phase::Preview;
}

void setup_mafile_result(AppState& state) {
    setup_base(state, Method::MaFile);
    MafileFlow& f = mafile_flow();
    f.rows = demo_rows(state, true);
    f.phase = MafileFlow::Phase::Result;
}

void setup_mafile_path_error(AppState& state) {
    setup_base(state, Method::MaFile);
    MafileFlow& f = mafile_flow();
    f.path = "D:\\Backups\\maFiles-old";
    f.path_error = "Nothing exists at this path.";
}

}  // namespace
SAM_UI_CAPTURE_SCENE(add_chooser, "add-chooser", &setup_chooser, 30)
SAM_UI_CAPTURE_SCENE(add_mafile, "add-mafile", &setup_mafile, 30)
SAM_UI_CAPTURE_SCENE(add_mafile_preview, "add-mafile-preview", &setup_mafile_preview, 30)
SAM_UI_CAPTURE_SCENE(add_mafile_result, "add-mafile-result", &setup_mafile_result, 30)
SAM_UI_CAPTURE_SCENE(add_mafile_path_error, "add-mafile-path-error", &setup_mafile_path_error, 30)
SAM_UI_CAPTURE_SCENE(add_info_dat, "add-info-dat", &setup_info_dat, 30)
SAM_UI_CAPTURE_SCENE(add_nfa, "add-nfa", &setup_nfa, 30)
SAM_UI_CAPTURE_SCENE(add_nfa_error, "add-nfa-error", &setup_nfa_error, 30)
#endif

}  // namespace sam::ui::screens
