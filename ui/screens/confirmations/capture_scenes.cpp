// Capture scenes of the Confirmations screen (Debug builds only): the states the built-in
// "confirmations" scene does not show.

#ifdef SAM_UI_CAPTURE

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "ui/screens/confirmations/view.hpp"
#include "ui/screens/confirmations_detail.hpp"

namespace {

namespace cd = sam::ui::screens::confirmations_detail;
namespace cv = sam::ui::screens::confirmations_view;
using sam::app::AppState;
using sam::app::ui_capture::demo_account_id;

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The demo vault on Confirmations, with this screen's own state reset (it outlives a scene).
void open_screen(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Confirmations;
    cv::view() = cv::ViewState{};
    std::lock_guard lk(cd::g_mtx);
    cd::g_last_scan = cd::ScanResult{};
    cd::g_show_scan_modal = false;
}

// The pending confirmations of one demo account, as the seeding left them.
std::vector<cd::Pick> picks_of(int account) {
    std::vector<cd::Pick> picks;
    const std::string id = demo_account_id(account);
    std::lock_guard lk(cd::g_mtx);
    if (const auto it = cd::g_lists.find(id); it != cd::g_lists.end()) {
        for (const auto& c : it->second.items) picks.push_back({id, c});
    }
    return picks;
}

void select(const std::vector<cd::Pick>& picks) {
    for (const auto& p : picks) cv::view().selection.SetItemSelected(cv::row_key(p.account_id, p.conf.id), true);
}

void setup_selected(AppState& state) {
    open_screen(state);
    select(picks_of(7));
    select({picks_of(0).front()});
}

void setup_detail(AppState& state) {
    open_screen(state);
    select({picks_of(0).front()});
}

void setup_deny(AppState& state) {
    open_screen(state);
    const auto picks = picks_of(0);
    select({picks.front()});
    cv::ask_decision(state, {picks.front()}, false);
}

void setup_bulk_approve(AppState& state) {
    open_screen(state);
    std::vector<cd::Pick> picks = picks_of(7);
    picks.push_back(picks_of(0).front());
    select(picks);
    cv::ask_decision(state, picks, true);
}

void setup_approve_recovery(AppState& state) {
    open_screen(state);
    for (const auto& p : picks_of(7)) {
        if (p.conf.type == sam::sda::ConfirmationType::AccountRecovery) {
            select({p});
            cv::ask_decision(state, {p}, true);
        }
    }
}

void setup_history(AppState& state) {
    open_screen(state);
    const std::int64_t now = now_unix();
    struct Seed {
        int account;
        sam::sda::ConfirmationType type;
        const char* headline;
        bool allow;
        sam::sda::AuditSource source;
        std::int64_t ago;
    };
    const Seed seeds[] = {
        {2, sam::sda::ConfirmationType::MarketListing, "Sell - Demo Sticker | Placeholder", true,
         sam::sda::AuditSource::Auto, 86400 * 2},
        {5, sam::sda::ConfirmationType::Trade, "Trade with Otter Placeholder", false, sam::sda::AuditSource::UserSingle,
         86400},
        {7, sam::sda::ConfirmationType::MarketListing, "Sell - Demo Case Key", true, sam::sda::AuditSource::UserBulk,
         7200},
        {7, sam::sda::ConfirmationType::MarketListing, "Sell - Demo Graffiti | Lorem", true,
         sam::sda::AuditSource::UserBulk, 7200},
        {6, sam::sda::ConfirmationType::PhoneNumberChange, "Change phone number", true,
         sam::sda::AuditSource::UserSingle, 1800},
        {0, sam::sda::ConfirmationType::Trade, "Trade with Marten Mock", true, sam::sda::AuditSource::UserSingle, 600},
    };
    // The audit log lives in the capture data dir, which both theme passes share.
    if (!state.conf_audit.entries().empty()) {
        cv::view().history_open = true;
        return;
    }
    for (const Seed& s : seeds) {
        sam::sda::AuditEntry e;
        e.unix_time = now - s.ago;
        e.account_id = demo_account_id(s.account);
        if (const auto* a = state.find_account(e.account_id)) e.account_login = a->login;
        e.conf_id = std::to_string(8100000000LL + s.ago);
        e.conf_type = s.type;
        e.headline = s.headline;
        e.allow = s.allow;
        e.source = s.source;
        state.conf_audit.record(std::move(e));
    }
    cv::view().history_open = true;
}

void setup_scan(AppState& state) {
    open_screen(state);
    cd::ScanResult r;
    r.targeted = 3;
    r.linked = 1;
    r.skipped_encrypted = 1;
    r.skipped_ambiguous = 1;
    r.entries = {
        {"76561190000000004.maFile", demo_account_id(4), cd::ScanOutcome::Linked},
        {"demo-acc-09.maFile", demo_account_id(9), cd::ScanOutcome::Encrypted},
        {"demo-acc-1.maFile", {}, cd::ScanOutcome::Ambiguous},
        {"76561190000000021.maFile", {}, cd::ScanOutcome::NotNeeded},
        {"76561190000000022.maFile", {}, cd::ScanOutcome::NotNeeded},
    };
    std::lock_guard lk(cd::g_mtx);
    cd::g_last_scan = std::move(r);
    cd::g_show_scan_modal = true;
}

void setup_empty(AppState& state) {
    open_screen(state);
    {
        std::lock_guard lk(cd::g_mtx);
        for (auto& [id, list] : cd::g_lists) {
            list.items.clear();
            list.refreshing = false;
            list.last_error.clear();
        }
    }
    state.pending_confirmations_count.store(0);
    for (const auto& a : state.vault.accounts) {
        if (a.sda.has_value()) state.conf_last_refresh_unix[a.id] = now_unix() - 240;
    }
}

void setup_problems(AppState& state) {
    open_screen(state);
    {
        std::lock_guard lk(cd::g_mtx);
        cd::g_lists[demo_account_id(3)].last_error = "no stored password, open Add Account and run the full login";
        cd::g_lists[demo_account_id(6)].last_error = "auto-relogin failed: InvalidPassword";
    }
    state.conf_permanent_failure.insert(demo_account_id(7));
}

void setup_auto_approve(AppState& state) {
    open_screen(state);
    state.settings.confirmations.auto_approve_enabled = true;
    state.settings.confirmations.auto_approve_market = true;
}

}  // namespace

SAM_UI_CAPTURE_SCENE(conf_selected, "confirmations-selected", &setup_selected, 30)
SAM_UI_CAPTURE_SCENE(conf_detail, "confirmations-detail", &setup_detail, 30)
SAM_UI_CAPTURE_SCENE(conf_deny, "confirmations-deny", &setup_deny, 30)
SAM_UI_CAPTURE_SCENE(conf_bulk_approve, "confirmations-bulk-approve", &setup_bulk_approve, 30)
SAM_UI_CAPTURE_SCENE(conf_approve_recovery, "confirmations-approve-recovery", &setup_approve_recovery, 30)
SAM_UI_CAPTURE_SCENE(conf_history,"confirmations-history", &setup_history, 30)
SAM_UI_CAPTURE_SCENE(conf_scan, "confirmations-scan", &setup_scan, 30)
SAM_UI_CAPTURE_SCENE(conf_empty, "confirmations-empty", &setup_empty, 30)
SAM_UI_CAPTURE_SCENE(conf_problems, "confirmations-problems", &setup_problems, 30)
SAM_UI_CAPTURE_SCENE(conf_auto,"confirmations-auto-approve", &setup_auto_approve, 30)

#endif  // SAM_UI_CAPTURE
