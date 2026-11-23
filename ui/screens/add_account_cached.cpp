#include "ui/screens/add_account_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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
#include <facet/widgets/layout.h>

#include "core/account_store/store.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/steam_local/connect_cache.hpp"
#include "core/steam_local/loginusers.hpp"
#include "core/steam_login/session.hpp"
#include "ui/kit/actions.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/util.hpp"

#ifdef SAM_UI_CAPTURE
#include "app/ui_capture.hpp"
#endif

namespace sam::ui::screens {

namespace {

using namespace add_account_detail;

struct CachedCandidate {
    steam_local::LocalAccount local;
    crypto::SecureString token;
    std::int64_t expires = 0;
    std::uint64_t steam_id = 0;
    bool expired = false;
};

struct ThisPcFlow {
    bool scanned = false;
    bool done = false;
    std::vector<CachedCandidate> candidates;
    std::vector<BulkRow> rows;
    std::string single_account_id;
};

ThisPcFlow& flow() {
    static ThisPcFlow f;
    return f;
}

std::string validity_note(const CachedCandidate& c) {
    if (c.expired) return "Saved session expired: it can be added, but signs in only after Steam signs it in again here.";
    const long long days = static_cast<long long>((c.expires - now_seconds()) / 86400);
    char buf[64];
    std::snprintf(buf, sizeof(buf), days == 1 ? "Saved session valid for 1 day" : "Saved session valid for %lld days", days);
    return buf;
}

void rescan(app::AppState& state, ThisPcFlow& f) {
    f.candidates.clear();
    f.rows.clear();
    f.done = false;
    const std::int64_t now = now_seconds();
    for (const auto& la : steam_local::read_loginusers()) {
        if (la.account_name.empty()) continue;
        if (core::store::find_existing_account(state.vault, la.steam_id_64, la.account_name) != nullptr) continue;

        auto token = steam_local::read_connect_cache_token(la.account_name);
        if (!token) continue;
        const std::int64_t exp = steam_login::jwt_expiry(*token);
        if (exp == 0) continue;

        CachedCandidate c;
        c.local = la;
        c.expires = exp;
        c.expired = exp <= now;
        c.steam_id = steam_login::jwt_steam_id(*token);
        if (c.steam_id == 0) c.steam_id = la.steam_id_64;
        c.token = std::move(*token);

        BulkRow row;
        row.name = c.local.persona_name.empty() ? c.local.account_name
                                                : c.local.persona_name + " (" + c.local.account_name + ")";
        row.source = c.steam_id != 0 ? std::to_string(c.steam_id) : std::string("No Steam ID");
        row.note = validity_note(c);
        row.status = RowStatus::New;
        row.checked = !c.expired;
        row.payload = f.candidates.size();
        f.candidates.push_back(std::move(c));
        f.rows.push_back(std::move(row));
    }
    f.scanned = true;
}

void import_selected(app::AppState& state, ThisPcFlow& f) {
    const std::string cached_group = core::store::ensure_cached_group(state.vault);
    const std::string cached_tag = core::store::ensure_cached_tag(state.vault);
    std::vector<std::string> ids;
    for (auto& row : f.rows) {
        if (!row.checked) {
            row.status = RowStatus::Skipped;
            row.note = "Not selected.";
            continue;
        }
        const CachedCandidate& c = f.candidates[row.payload];
        const std::string raw = c.local.account_name + "----" + std::string(c.token.begin(), c.token.end());
        const JwtImportResult r = import_jwt_token(state, raw, false);
        if (!r.ok) {
            row.status = RowStatus::Failed;
            row.note = r.error;
            continue;
        }
        state.nfa_dead_notified.erase(r.account_id);
        if (core::Account* a = state.find_account(r.account_id)) {
            a->group_id = cached_group;
            if (std::find(a->tag_ids.begin(), a->tag_ids.end(), cached_tag) == a->tag_ids.end()) {
                a->tag_ids.push_back(cached_tag);
            }
        }
        row.status = r.merged ? RowStatus::Updated : RowStatus::Imported;
        row.account_id = r.account_id;
        row.note = r.expired ? "Added, but its saved session had expired: sign it in to Steam on this PC, then take it again, or add a password in Edit account."
                             : "Added to the Cached group.";
        ids.push_back(r.account_id);
    }
    if (!ids.empty()) {
        state.vault_dirty = true;
        state.save_vault_if_dirty();
        if (ids.size() == 1) {
            state.pull_all_for_account(ids.front());
        } else {
            state.refresh_accounts_staggered(ids);
            state.start_gc_validate(false);
        }
    }
    f.single_account_id = ids.size() == 1 ? ids.front() : std::string{};
    for (auto& c : f.candidates) crypto::zero_buffer(c.token.data(), c.token.size());
    f.candidates.clear();
    f.done = true;
}

constexpr const char* kPcSteps[] = {"Choose accounts", "Done"};

// Why the shared flow shell with a checklist table: this PC can hold many remembered logins, and
// the age of each saved session decides whether one is worth taking, so that fact sits on every
// row while the commit button stays in the bar.
void draw(app::AppState& state) {
    ThisPcFlow& f = flow();
    const facet::Theme& t = facet::T();
    if (!f.scanned) rescan(state, f);
    const bool privacy = state.settings.privacy_mode;

    if (f.done) {
        begin_flow("##this-pc", kPcSteps, 2, kWideWidth);
        kit::web_api_key_notice(state, {.compact = true});
        if (state.settings.web_api_key.empty()) gap(t.spacing.sm);
        begin_step_card("##this-pc-done", nullptr, nullptr, true);
        bulk_header("Accounts taken from this PC", f.rows, true);
        bulk_table("##this-pc-results", f.rows,
                   {.selectable = false, .hide_names = privacy, .source_header = "Steam ID"});
        end_step_card();
        begin_commit_bar();
        const std::string id = f.single_account_id;
        if (facet::Button("Scan this PC again")) {
            rescan(state, f);
        } else {
            open_accounts_button(state, id);
        }
        end_flow();
        return;
    }

    constexpr const char* kAbout =
        "Accounts Steam on this PC remembers. They are added without a password and sign in with the saved Steam "
        "session; add a password later in Edit account to make one a full account. Accounts already in the vault "
        "are not listed.";
    if (f.rows.empty()) {
        begin_flow("##this-pc", kPcSteps, 0, kFormWidth);
        begin_step_card("##this-pc-empty", "Accounts on this PC", kAbout);
        const bool again = kit::empty_state("No new accounts on this PC",
                                            {.message = "Steam on this computer remembers no login that is not in "
                                                        "the vault yet. Sign in to Steam with \"Remember me\" "
                                                        "ticked, then scan again.",
                                             .icon = facet::Icon("monitor"),
                                             .action_label = "Scan again",
                                             .action_icon = facet::Icon("refresh")});
        end_step_card();
        if (again) rescan(state, f);
        begin_commit_bar();
        end_flow();
        return;
    }

    begin_flow("##this-pc", kPcSteps, 0, kWideWidth);
    begin_step_card("##this-pc-card", nullptr, nullptr, true);
    char title[64];
    std::snprintf(title, sizeof(title), "%zu account%s on this PC", f.rows.size(), f.rows.size() == 1 ? "" : "s");
    bulk_header(title, f.rows, false);
    hint(kAbout);
    gap(t.spacing.sm);
    facet::BeginStack("##tools", {.orientation = facet::Orientation::Horizontal,
                                  .align = facet::LayoutAlign::Center,
                                  .width = facet::AvailableWidthUnits()});
    select_all_box("##select", f.rows);
    facet::StackSpacer();
    bool again = facet::Button("Scan again", {.icon = facet::Icon("refresh")});
    facet::EndStack();
    gap(t.spacing.sm);
    bulk_table("##this-pc-rows", f.rows, {.hide_names = privacy, .source_header = "Steam ID"});
    end_step_card();
    if (again) rescan(state, f);
    begin_commit_bar(selection_status(f.rows).c_str());
    const int n = selected_count(f.rows);
    char label[64];
    std::snprintf(label, sizeof(label), n == 1 ? "Add 1 account" : "Add %d accounts", n);
    ImGui::BeginDisabled(n == 0);
    if (facet::Button(label, {.variant = facet::ButtonVariant::Primary})) import_selected(state, f);
    ImGui::EndDisabled();
    end_flow();
}
}  // namespace

namespace add_account_detail {

void draw_this_pc(app::AppState& state) { draw(state); }

void reset_this_pc() {
    ThisPcFlow& f = flow();
    for (auto& c : f.candidates) crypto::zero_buffer(c.token.data(), c.token.size());
    f = {};
}

}  // namespace add_account_detail

#ifdef SAM_UI_CAPTURE
namespace {

void seed(app::AppState& state, bool empty) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::AddAccount;
    reset_screen();
    open_method(Method::ThisPc);
    ThisPcFlow& f = flow();
    f.scanned = true;
    if (!empty) {
        const char* const personas[] = {"Placeholder Lynx", "", "Demo Heron", "Sample Badger", ""};
        for (int i = 0; i < 5; ++i) {
            CachedCandidate c;
            char login[32];
            std::snprintf(login, sizeof(login), "demo-local-%02d", i);
            c.local.account_name = login;
            c.local.persona_name = personas[i];
            c.steam_id = 76561198000100000ULL + static_cast<std::uint64_t>(i);
            c.expires = now_seconds() + (i == 3 ? -86400 : 86400 * (40 + i * 30));
            c.expired = i == 3;
            BulkRow row;
            row.name = c.local.persona_name.empty() ? c.local.account_name
                                                    : c.local.persona_name + " (" + c.local.account_name + ")";
            row.source = std::to_string(c.steam_id);
            row.note = validity_note(c);
            row.checked = !c.expired;
            row.payload = f.candidates.size();
            f.candidates.push_back(std::move(c));
            f.rows.push_back(std::move(row));
        }
    }
    mark_seeded(state);
}

void setup_this_pc(app::AppState& state) { seed(state, false); }
void setup_this_pc_empty(app::AppState& state) { seed(state, true); }

}  // namespace
SAM_UI_CAPTURE_SCENE(add_this_pc, "add-this-pc", &setup_this_pc, 30)
SAM_UI_CAPTURE_SCENE(add_this_pc_empty, "add-this-pc-empty", &setup_this_pc_empty, 30)
#endif

}  // namespace sam::ui::screens
