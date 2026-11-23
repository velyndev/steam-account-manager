// Capture scenes of the Authenticator screen and its dialogs. Debug builds only; see
// app/ui_capture.hpp. The harness fixes the clock, so codes and countdowns are stable.

#ifdef SAM_UI_CAPTURE

#include <cstdio>
#include <string>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "core/crypto/secure_string.hpp"
#include "ui/screens/authenticator/dialogs.hpp"

namespace {

using sam::app::AppState;
using sam::app::ui_capture::demo_account_id;
namespace auth = sam::ui::screens::authenticator;

// demo-acc-00 has an authenticator; demo-acc-02 and demo-acc-05 have none (every third).
constexpr int kLinked = 0;
constexpr int kUnlinked = 2;
constexpr int kUnlinkedNoSession = 5;

sam::core::Account* open_on(AppState& state, int index) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Authenticator;
    if (index < 0) return nullptr;
    state.selected_account_id = demo_account_id(index);
    return state.find_account(state.selected_account_id);
}

void give_session(sam::core::Account* a) {
    if (a != nullptr) a->access_token = sam::crypto::make_secure("capture-demo-access-token");
}

void setup_hidden(AppState& state) {
    open_on(state, kLinked);
    state.settings.sda.hide_current_code = true;
}

void setup_revocation(AppState& state) {
    open_on(state, kLinked);
    auth::capture_reveal_revocation(demo_account_id(kLinked));
}

void setup_unlinked(AppState& state) {
    give_session(open_on(state, kUnlinked));
}

void setup_unlinked_no_session(AppState& state) {
    open_on(state, kUnlinkedNoSession);
}

void setup_no_selection(AppState& state) {
    open_on(state, -1);
}

void setup_none_linked(AppState& state) {
    open_on(state, -1);
    for (auto& a : state.vault.accounts) a.sda.reset();
}

void setup_empty_vault(AppState& state) {
    open_on(state, -1);
    state.vault.accounts.clear();
}

void setup_bad_secret(AppState& state) {
    if (auto* a = open_on(state, kLinked)) a->sda->shared_secret.clear();
}

void setup_incomplete(AppState& state) {
    if (auto* a = open_on(state, kLinked)) {
        a->sda->fully_enrolled = false;
        a->sda->revocation_code.clear();
    }
}

void setup_filtered(AppState& state) {
    open_on(state, kLinked);
    state.settings.sda.show_next_code = false;
}

// ---- Link flow ----

auth::LinkFlowState& link_on(AppState& state, int index) {
    give_session(open_on(state, index));
    auth::request_link(state, demo_account_id(index));
    return auth::dialogs().link;
}

void setup_link_intro(AppState& state) {
    link_on(state, kUnlinked);
}

void setup_link_working(AppState& state) {
    auto& s = link_on(state, kUnlinked);
    s.working = true;
    s.working_text = "Asking Steam to add an authenticator";
}

void setup_link_intro_error(AppState& state) {
    auto& s = link_on(state, kUnlinked);
    s.error = "Steam refused to add an authenticator (status 29).";
}

// The revocation and activation steps need a stored authenticator: use a linked demo account
// and show it as freshly added.
void setup_link_revocation(AppState& state) {
    auto& s = link_on(state, kLinked);
    s.step = auth::LinkFlowState::Step::Revocation;
    s.acknowledged = false;
}

void setup_link_activation(AppState& state) {
    auto& s = link_on(state, kLinked);
    s.step = auth::LinkFlowState::Step::Activation;
    s.bad_code = true;
    std::snprintf(s.activation.data(), s.activation.size(), "%s", "7K2QX");
}

void setup_link_error(AppState& state) {
    auto* a = open_on(state, kLinked);
    if (a != nullptr) a->sda->fully_enrolled = false;
    auth::request_link(state, demo_account_id(kLinked));
    auto& s = auth::dialogs().link;
    s.error = "Steam reports no mobile authenticator on this account, so this maFile cannot be marked as linked. "
              "It may have been removed, or its activation was never finished.";
}

void setup_link_done(AppState& state) {
    auto& s = link_on(state, kLinked);
    s.step = auth::LinkFlowState::Step::Done;
}

void setup_verify(AppState& state) {
    open_on(state, kLinked);
    auto& s = auth::dialogs().link;
    s = auth::LinkFlowState{};
    s.open = true;
    s.mode = auth::LinkFlowState::Mode::Verify;
    s.account_id = demo_account_id(kLinked);
    s.step = auth::LinkFlowState::Step::Done;
}

// ---- Other dialogs ----

void setup_remove(AppState& state) {
    open_on(state, kLinked);
    auth::request_remove(demo_account_id(kLinked));
}

void setup_backup(AppState& state) {
    open_on(state, kLinked);
    auth::request_backup(demo_account_id(kLinked));
}

void setup_backup_missing(AppState& state) {
    if (auto* a = open_on(state, kLinked)) a->sda->uri.clear();
    auth::request_backup(demo_account_id(kLinked));
}

void setup_export(AppState& state) {
    open_on(state, kLinked);
    auth::request_export(demo_account_id(kLinked));
    auth::dialogs().export_mafiles.passphrase = "demo passphrase";
    auth::dialogs().export_mafiles.confirm = "demo passphrase";
}

void setup_secrets(AppState& state) {
    open_on(state, kLinked);
    auth::request_secrets(demo_account_id(kLinked));
}

}  // namespace

SAM_UI_CAPTURE_SCENE(auth_hidden, "authenticator-hidden", &setup_hidden, 30)
SAM_UI_CAPTURE_SCENE(auth_revocation, "authenticator-revocation", &setup_revocation, 30)
SAM_UI_CAPTURE_SCENE(auth_unlinked, "authenticator-unlinked", &setup_unlinked, 30)
SAM_UI_CAPTURE_SCENE(auth_unlinked_nosession, "authenticator-unlinked-nosession", &setup_unlinked_no_session, 30)
SAM_UI_CAPTURE_SCENE(auth_no_selection, "authenticator-no-selection", &setup_no_selection, 30)
SAM_UI_CAPTURE_SCENE(auth_none_linked, "authenticator-none-linked", &setup_none_linked, 30)
SAM_UI_CAPTURE_SCENE(auth_empty_vault, "authenticator-empty-vault", &setup_empty_vault, 30)
SAM_UI_CAPTURE_SCENE(auth_bad_secret, "authenticator-bad-secret", &setup_bad_secret, 30)
SAM_UI_CAPTURE_SCENE(auth_incomplete, "authenticator-incomplete", &setup_incomplete, 30)
SAM_UI_CAPTURE_SCENE(auth_no_next, "authenticator-no-next", &setup_filtered, 30)
SAM_UI_CAPTURE_SCENE(auth_link_intro, "authenticator-link-intro", &setup_link_intro, 40)
SAM_UI_CAPTURE_SCENE(auth_link_working, "authenticator-link-working", &setup_link_working, 40)
SAM_UI_CAPTURE_SCENE(auth_link_intro_error, "authenticator-link-intro-error", &setup_link_intro_error, 40)
SAM_UI_CAPTURE_SCENE(auth_link_revocation, "authenticator-link-revocation", &setup_link_revocation, 40)
SAM_UI_CAPTURE_SCENE(auth_link_activation, "authenticator-link-activation", &setup_link_activation, 40)
SAM_UI_CAPTURE_SCENE(auth_link_error, "authenticator-link-error", &setup_link_error, 40)
SAM_UI_CAPTURE_SCENE(auth_link_done, "authenticator-link-done", &setup_link_done, 40)
SAM_UI_CAPTURE_SCENE(auth_verify, "authenticator-verify", &setup_verify, 40)
SAM_UI_CAPTURE_SCENE(auth_remove, "authenticator-remove", &setup_remove, 40)
SAM_UI_CAPTURE_SCENE(auth_backup, "authenticator-backup", &setup_backup, 40)
SAM_UI_CAPTURE_SCENE(auth_backup_missing, "authenticator-backup-missing", &setup_backup_missing, 40)
SAM_UI_CAPTURE_SCENE(auth_export, "authenticator-export", &setup_export, 40)
SAM_UI_CAPTURE_SCENE(auth_secrets, "authenticator-secrets", &setup_secrets, 40)

#endif  // SAM_UI_CAPTURE
