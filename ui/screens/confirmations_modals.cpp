// maFile linking and the folder scan for the Confirmations screen. The dialogs that show their
// results live in ui/screens/confirmations/dialogs.cpp.

#include <cstdint>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app/job_pump.hpp"
#include "core/log.hpp"
#include "core/sda/mafile.hpp"
#include "core/strings.hpp"
#include "platform/file_dialog.hpp"
#include "ui/screens/confirmations_detail.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens {

namespace confirmations_detail {

bool needs_mafile_data(const core::Account& a) {
    if (!a.sda.has_value()) return false;
    return a.sda->identity_secret.empty() || a.sda->device_id.empty();
}

namespace {

void merge_guard_into_account(core::Account& a, const core::SteamGuardAccount& g) {
    if (!a.sda.has_value()) a.sda = core::SteamGuardAccount{};
    auto& s = *a.sda;
    if (!g.shared_secret.empty())   s.shared_secret   = g.shared_secret;
    if (!g.serial_number.empty())   s.serial_number   = g.serial_number;
    if (!g.revocation_code.empty()) s.revocation_code = g.revocation_code;
    if (!g.uri.empty())             s.uri             = g.uri;
    if (g.server_time != 0)         s.server_time     = g.server_time;
    if (s.account_name.empty())     s.account_name    = g.account_name;
    if (!g.token_gid.empty())       s.token_gid       = g.token_gid;
    if (!g.identity_secret.empty()) s.identity_secret = g.identity_secret;
    if (!g.secret_1.empty())        s.secret_1        = g.secret_1;
    if (!g.device_id.empty())       s.device_id       = g.device_id;
    s.status         = g.status;
    s.fully_enrolled = g.fully_enrolled;
}

bool mafile_matches_account(const sam::sda::MafileLoadResult& loaded,
                             const std::string& login_lower,
                             std::uint64_t steam_id_64) {
    if (loaded.session_steam_id != 0 && steam_id_64 != 0 &&
        loaded.session_steam_id == steam_id_64) {
        return true;
    }
    if (!loaded.guard.account_name.empty()) {
        return core::to_lower(loaded.guard.account_name) == login_lower;
    }
    return false;
}

}  // namespace

void link_mafile_for_account(app::AppState& state, const core::Account& a) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Choose maFile for this account";
    opts.filters = {
        {L"Steam mobile authenticator (*.maFile)", L"*.maFile"},
        {L"All files (*.*)", L"*.*"},
    };
    const auto picked = platform::file_dialog::open_file(opts);
    if (!picked.ok) return;

    const std::string aid = a.id;
    const std::string login_lower = core::to_lower(a.login);
    const std::uint64_t sid = a.steam_id_64;
    const std::filesystem::path path = picked.path;

    {
        std::lock_guard lk(g_mtx);
        g_lists[aid].refreshing = true;
        g_lists[aid].last_error.clear();
    }

    app::job_pump::submit([&state, aid, login_lower, sid, path]() mutable {
        sam::sda::MafileLoadResult loaded;
        try {
            loaded = sam::sda::load_mafile(path, {});
        } catch (const sam::sda::MafileEncrypted&) {
            std::lock_guard lk(g_mtx);
            g_lists[aid].refreshing = false;
            g_lists[aid].last_error = "maFile is encrypted; decrypt it first";
            return;
        } catch (const std::exception& ex) {
            std::lock_guard lk(g_mtx);
            g_lists[aid].refreshing = false;
            g_lists[aid].last_error = std::string{"maFile load failed: "} + ex.what();
            return;
        }

        if (!mafile_matches_account(loaded, login_lower, sid)) {
            std::lock_guard lk(g_mtx);
            g_lists[aid].refreshing = false;
            g_lists[aid].last_error =
                "maFile does not match this account (got '" +
                loaded.guard.account_name + "')";
            return;
        }

        state.post_ui_callback([&state, aid, guard = std::move(loaded.guard)]() mutable {
            auto* acc = state.find_account(aid);
            if (!acc) return;
            merge_guard_into_account(*acc, guard);
            state.vault_dirty = true;
            state.save_vault_if_dirty();
            SAM_LOG_INFO("confirmation: linked maFile for '{}'", acc->login);

            core::Account snap = *acc;
            {
                std::lock_guard lk(g_mtx);
                g_lists[aid].refreshing = false;
                g_lists[aid].last_error.clear();
            }
            submit_account_refresh(state, snap);
        });
    });
}

void scan_folder_for_mafiles(app::AppState& state) {
    if (g_scan_in_progress.load()) return;

    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Pick folder containing maFiles";
    const auto picked = platform::file_dialog::pick_folder(opts);
    if (!picked.ok) return;

    struct Target {
        std::string id;
        std::string login_lower;
        std::string steam_id_str;
        std::uint64_t steam_id_64 = 0;
    };
    std::vector<Target> targets;
    for (const auto& a : state.vault.accounts) {
        if (!needs_mafile_data(a)) continue;
        Target t;
        t.id = a.id;
        t.login_lower = core::to_lower(a.login);
        t.steam_id_64 = a.steam_id_64;
        if (t.steam_id_64 != 0) t.steam_id_str = std::to_string(t.steam_id_64);
        targets.push_back(std::move(t));
    }

    if (targets.empty()) {
        std::lock_guard lk(g_mtx);
        g_last_scan = ScanResult{};
        g_show_scan_modal = true;
        return;
    }

    g_scan_in_progress.store(true);
    const int targeted_count = static_cast<int>(targets.size());
    const std::filesystem::path folder = picked.path;
    SAM_LOG_INFO("confirmation: scanning '{}' for {} missing accounts",
                 folder.string(), targeted_count);

    app::job_pump::submit([&state, folder, targets = std::move(targets),
                            targeted_count]() mutable {
        ScanResult res;
        res.targeted = targeted_count;
        const auto note = [&res](const std::filesystem::path& p, std::string account_id, ScanOutcome outcome) {
            res.entries.push_back({to_utf8(p.filename().wstring()), std::move(account_id), outcome});
        };

        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            folder, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::recursive_directory_iterator end;

        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) { ec.clear(); continue; }
            const auto& path = it->path();
            const std::string ext = core::to_lower(path.extension().string());
            if (ext != ".mafile") continue;

            const std::string stem = core::to_lower(path.stem().string());

            int candidate = -1;
            int matches = 0;
            for (std::size_t i = 0; i < targets.size(); ++i) {
                if (!targets[i].steam_id_str.empty() &&
                    stem.find(targets[i].steam_id_str) != std::string::npos) {
                    candidate = static_cast<int>(i);
                    ++matches;
                }
            }
            if (matches == 0) {
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    if (targets[i].login_lower.empty()) continue;
                    if (stem.find(targets[i].login_lower) != std::string::npos) {
                        candidate = static_cast<int>(i);
                        ++matches;
                    }
                }
            }
            if (matches == 0) {
                note(path, {}, ScanOutcome::NotNeeded);
                continue;
            }
            if (matches > 1) {
                ++res.skipped_ambiguous;
                note(path, {}, ScanOutcome::Ambiguous);
                continue;
            }

            const auto& tgt = targets[candidate];
            sam::sda::MafileLoadResult loaded;
            try {
                loaded = sam::sda::load_mafile(path, {});
            } catch (const sam::sda::MafileEncrypted&) {
                ++res.skipped_encrypted;
                note(path, tgt.id, ScanOutcome::Encrypted);
                continue;
            } catch (...) {
                ++res.parse_errors;
                note(path, tgt.id, ScanOutcome::Unreadable);
                continue;
            }

            if (!mafile_matches_account(loaded, tgt.login_lower, tgt.steam_id_64)) {
                ++res.skipped_no_match;
                note(path, tgt.id, ScanOutcome::NoMatch);
                continue;
            }
            if (loaded.guard.identity_secret.empty() ||
                loaded.guard.device_id.empty()) {
                ++res.skipped_no_match;
                note(path, tgt.id, ScanOutcome::MissingSecrets);
                continue;
            }

            const std::string aid = tgt.id;
            state.post_ui_callback(
                [&state, aid, guard = std::move(loaded.guard)]() mutable {
                    auto* acc = state.find_account(aid);
                    if (!acc) return;
                    merge_guard_into_account(*acc, guard);
                    state.vault_dirty = true;
                    std::lock_guard lk(g_mtx);
                    if (g_lists[aid].last_error.find("missing maFile data") !=
                        std::string::npos) {
                        g_lists[aid].last_error.clear();
                    }
                });
            ++res.linked;
            note(path, aid, ScanOutcome::Linked);
            targets.erase(targets.begin() + candidate);
            if (targets.empty()) break;
        }

        state.post_ui_callback([&state, res = std::move(res)]() mutable {
            state.save_vault_if_dirty();
            SAM_LOG_INFO("confirmation: scan done, linked {}/{} (skipped: "
                         "encrypted={} no_match={} ambiguous={} parse_errors={})",
                         res.linked, res.targeted,
                         res.skipped_encrypted, res.skipped_no_match,
                         res.skipped_ambiguous, res.parse_errors);
            std::lock_guard lk(g_mtx);
            g_last_scan = std::move(res);
            g_show_scan_modal = true;
        });
        g_scan_in_progress.store(false);
    });
}

}  // namespace confirmations_detail

}  // namespace sam::ui::screens
