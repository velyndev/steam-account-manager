#include "ui/screens/add_account_detail.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
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
#include <facet/widgets/navigation.h>

#include "app/job_pump.hpp"
#include "core/account_store/store.hpp"
#include "core/crypto/rng.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/log.hpp"
#include "core/sda/totp.hpp"
#include "core/steam_login/mobile_auth.hpp"
#include "core/steam_login/session.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/util.hpp"

#ifdef SAM_UI_CAPTURE
#include "app/ui_capture.hpp"
#endif

namespace sam::ui::screens {

namespace {

using add_account_detail::begin_column;
using add_account_detail::end_column;
using add_account_detail::hint;

struct LoginWizard {
    enum class Phase { Credentials, GuardWait, Polling, Done };
    Phase phase = Phase::Credentials;

    std::string username;
    std::string password;
    std::string shared_secret;
    std::string guard_code;
    bool secret_open = false;

    std::string client_id;
    std::string request_id;
    std::uint64_t steam_id = 0;
    std::int64_t poll_interval = 5;
    std::vector<steam_login::GuardKind> allowed_guards;
    steam_login::GuardKind chosen_guard = steam_login::GuardKind::None;
    bool has_device_code = false;
    bool has_email_code = false;
    bool confirmation_mode = false;
    bool email_confirmation_mode = false;

    core::Account account;
    std::string error;       // a flow error, shown on the step it happened in
    std::string guard_note;  // a rejected code, shown under the code field
    std::string guard_raw;   // Steam's own words for it, in the field's tooltip
    bool busy = false;
    bool poll_running = false;
    bool auto_guard_submitted = false;
    // Bumped on every new attempt and on cancel, so a late worker result of an old attempt is
    // dropped instead of landing in the new one.
    int generation = 0;

    bool relogin = false;
    std::string relogin_login;

    bool merged = false;
    std::string account_id;
    bool has_authenticator = false;

    void reset() {
        crypto::zero_buffer(password.data(), password.size());
        crypto::zero_buffer(shared_secret.data(), shared_secret.size());
        const int next = generation + 1;
        *this = {};
        generation = next;
    }
};

LoginWizard& wizard() {
    static LoginWizard w;
    return w;
}

bool poll_aborted(LoginWizard* w, int gen) {
    return w->generation != gen || w->phase == LoginWizard::Phase::Done ||
           w->phase == LoginWizard::Phase::Credentials;
}

void push_job(app::AppState& state, std::function<void()> fn) {
    std::lock_guard lk(state.job_mutex);
    state.completed_jobs.push_back({"", std::move(fn)});
}

void store_session(LoginWizard* w, app::AppState& state, crypto::SecureString at, crypto::SecureString rt,
                   std::int64_t expiry, crypto::SecureString ls, std::string sess_id) {
    core::Account* existing =
        core::store::find_existing_account(state.vault, w->account.steam_id_64, w->account.login);

    std::string id_for_refresh;
    if (existing) {
        existing->password = w->account.password;
        existing->steam_id_64 = w->account.steam_id_64;
        existing->access_token = at;
        existing->refresh_token = rt;
        existing->access_token_expires = expiry;
        existing->steam_login_secure = ls;
        existing->session_id = sess_id;
        existing->last_login_unix = now_seconds();
        id_for_refresh = existing->id;

        if (w->account.sda.has_value() && !w->account.sda->shared_secret.empty()) {
            if (!existing->sda.has_value()) {
                existing->sda = w->account.sda;
            } else {
                existing->sda->shared_secret = w->account.sda->shared_secret;
                if (existing->sda->account_name.empty()) existing->sda->account_name = w->account.sda->account_name;
            }
        }
        w->merged = true;
        w->has_authenticator = existing->sda.has_value();
    } else {
        w->account.id = add_account_detail::generate_ulid();
        w->account.access_token = at;
        w->account.refresh_token = rt;
        w->account.access_token_expires = expiry;
        w->account.steam_login_secure = ls;
        w->account.session_id = sess_id;
        w->account.created_unix = now_seconds();
        w->account.last_login_unix = now_seconds();
        id_for_refresh = w->account.id;
        w->merged = false;
        w->has_authenticator = w->account.sda.has_value();
        state.vault.accounts.push_back(std::move(w->account));
    }
    w->account_id = id_for_refresh;
    state.vault_dirty = true;
    state.save_vault_if_dirty();
    if (!id_for_refresh.empty()) state.pull_all_for_account(id_for_refresh);

    w->poll_running = false;
    w->busy = false;
    w->phase = LoginWizard::Phase::Done;
}

void launch_poll_worker(LoginWizard* w, app::AppState& state) {
    if (w->poll_running) return;
    w->poll_running = true;

    const std::string cid = w->client_id;
    const std::string rid = w->request_id;
    const auto interval = w->poll_interval;
    const auto sid = w->steam_id;
    const int gen = w->generation;

    app::job_pump::submit([cid, rid, interval, sid, gen, w, &state] {
        std::string local_cid = cid;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);

        while (std::chrono::steady_clock::now() < deadline) {
            auto poll = steam_login::poll_session(local_cid, rid);
            if (!poll.ok) {
                push_job(state, [w, gen, e = poll.error] {
                    if (poll_aborted(w, gen)) return;
                    w->poll_running = false;
                    w->busy = false;
                    w->error = e;
                });
                return;
            }
            if (poll.finished) {
                auto expiry = steam_login::jwt_expiry(poll.access_token);
                auto login_secure =
                    crypto::make_secure(steam_login::make_steam_login_secure(sid, poll.access_token));

                std::string sess_id = crypto::random_session_id();
                std::string registered_cookie;
                if (steam_login::transfer_login(sid, poll.refresh_token, sess_id, registered_cookie)) {
                    login_secure = crypto::make_secure(registered_cookie);
                } else {
                    SAM_LOG_WARN("add_account: transfer_login failed for steam id {}; "
                                 "mobileconf writes will need a relogin",
                                 sid);
                }

                push_job(state, [w, &state, gen, at = std::move(poll.access_token),
                                 rt = std::move(poll.refresh_token), expiry, ls = std::move(login_secure),
                                 sess_id = std::move(sess_id)]() mutable {
                    if (poll_aborted(w, gen)) return;
                    store_session(w, state, std::move(at), std::move(rt), expiry, std::move(ls), std::move(sess_id));
                });
                return;
            }
            if (!poll.new_client_id.empty()) local_cid = poll.new_client_id;
            std::this_thread::sleep_for(std::chrono::seconds(interval > 0 ? interval : 5));
        }

        push_job(state, [w, gen] {
            if (poll_aborted(w, gen)) return;
            w->poll_running = false;
            w->busy = false;
            w->error = "Steam did not confirm the sign-in within 2 minutes.";
        });
    });
}

void start_sign_in(LoginWizard& wiz, app::AppState& state) {
    ++wiz.generation;
    wiz.busy = true;
    wiz.error.clear();
    wiz.guard_note.clear();
    wiz.guard_code.clear();
    wiz.poll_running = false;
    wiz.auto_guard_submitted = false;
    wiz.phase = LoginWizard::Phase::Credentials;

    if (!wiz.shared_secret.empty()) {
        core::SteamGuardAccount g;
        g.account_name = wiz.username;
        g.shared_secret = wiz.shared_secret;
        wiz.account.sda = std::move(g);
    } else {
        wiz.account.sda.reset();
    }

    const std::string user = wiz.username;
    auto pw = crypto::make_secure(wiz.password);
    LoginWizard* w = &wiz;
    const int gen = wiz.generation;

    app::job_pump::submit([user, pw, w, gen, &state] {
        steam_login::MobileLogin login;
        login.username = user;
        login.password = pw;
        auto result = steam_login::begin_session(login);

        push_job(state, [w, gen, user, pw, r = std::move(result)] {
            if (w->generation != gen) return;
            w->busy = false;
            if (!r.ok) {
                w->error = r.error;
                return;
            }
            w->client_id = r.client_id;
            w->request_id = r.request_id;
            w->steam_id = r.steam_id;
            w->poll_interval = r.interval_seconds > 0 ? r.interval_seconds : 5;
            w->allowed_guards = r.allowed_confirmations;

            w->account.login = user;
            w->account.password = pw;
            w->account.steam_id_64 = r.steam_id;

            w->has_device_code = false;
            w->has_email_code = false;
            w->confirmation_mode = false;
            w->email_confirmation_mode = false;
            for (auto k : w->allowed_guards) {
                if (k == steam_login::GuardKind::DeviceCode)
                    w->has_device_code = true;
                else if (k == steam_login::GuardKind::EmailCode)
                    w->has_email_code = true;
                else if (k == steam_login::GuardKind::DeviceConfirmation)
                    w->confirmation_mode = true;
                else if (k == steam_login::GuardKind::EmailConfirmation)
                    w->email_confirmation_mode = true;
            }
            w->chosen_guard = w->has_device_code  ? steam_login::GuardKind::DeviceCode
                              : w->has_email_code ? steam_login::GuardKind::EmailCode
                                                  : steam_login::GuardKind::None;

            const bool needs_guard =
                w->confirmation_mode || w->email_confirmation_mode || w->chosen_guard != steam_login::GuardKind::None;
            w->phase = needs_guard ? LoginWizard::Phase::GuardWait : LoginWizard::Phase::Polling;
        });
    });
}

void submit_code(LoginWizard& wiz, app::AppState& state, std::string code, steam_login::GuardKind kind, bool automatic) {
    wiz.busy = true;
    wiz.guard_note.clear();
    const std::string cid = wiz.client_id;
    const std::uint64_t sid = wiz.steam_id;
    const bool conf = wiz.confirmation_mode || wiz.email_confirmation_mode;
    LoginWizard* w = &wiz;
    const int gen = wiz.generation;

    app::job_pump::submit([code = std::move(code), cid, sid, kind, conf, automatic, w, gen, &state] {
        std::string err;
        const bool ok = steam_login::submit_guard_code(cid, sid, code, kind, &err);
        push_job(state, [w, &state, gen, ok, err, conf, automatic] {
            if (w->generation != gen) return;
            w->busy = false;
            if (!ok) {
                w->guard_note = automatic ? "Steam did not accept the code made from the shared secret. Check the "
                                            "secret, or type the code the app shows."
                                          : "Steam did not accept this code. Codes change every 30 seconds: type "
                                            "the one the app shows now.";
                w->guard_raw = err;
                w->guard_code.clear();
                return;
            }
            if (conf) {
                launch_poll_worker(w, state);
            } else {
                w->phase = LoginWizard::Phase::Polling;
            }
        });
    });
}

void cancel(LoginWizard& wiz) {
    ++wiz.generation;
    wiz.busy = false;
    wiz.poll_running = false;
    wiz.error.clear();
    wiz.guard_note.clear();
    wiz.guard_code.clear();
    wiz.phase = LoginWizard::Phase::Credentials;
}

using add_account_detail::begin_commit_bar;
using add_account_detail::begin_flow;
using add_account_detail::begin_step_card;
using add_account_detail::done_header;
using add_account_detail::end_flow;
using add_account_detail::end_step_card;
using add_account_detail::flow_error;
using add_account_detail::gap;

constexpr const char* kSteps[] = {"Credentials", "Steam Guard", "Session", "Done"};

// Steam's answers in plain words (rule 5); the raw text stays behind "Details".
std::string friendly_login_error(const std::string& raw) {
    auto has = [&](const char* s) { return raw.find(s) != std::string::npos; };
    if (has("RateLimit") || has("rate limit") || has("429")) {
        return "Steam blocked sign-ins from this network for a while after too many attempts. Wait a few minutes, "
               "then try again.";
    }
    if (has("InvalidPassword") || has("password")) return "The login or the password is wrong. Check both, then try again.";
    if (has("2 minutes")) return "Steam did not confirm the sign-in within 2 minutes. Sign in again and approve it sooner.";
    if (has("Expired") || has("expired")) return "The sign-in request expired before it was confirmed. Sign in again.";
    if (has("timed out") || has("Timeout") || has("network") || has("HTTP") || has("curl")) {
        return "Steam could not be reached. Check the internet connection, then try again.";
    }
    return "Steam did not let this sign-in through. Check the login and password, then try again.";
}

// A centered spinner over what is being waited for: the one moving thing of a waiting step.
void waiting(const char* text) {
    const facet::StackOptions centered{.align = facet::LayoutAlign::Center, .width = facet::AvailableWidthUnits()};
    facet::BeginStack("##waiting-spin", centered);
    facet::Spinner("##spin", {.size = facet::ControlSize::Large});
    facet::EndStack();
    gap(facet::T().spacing.sm);
    facet::BeginStack("##waiting-text", centered);
    facet::StyledText(text);
    facet::EndStack();
}

void draw_credentials(LoginWizard& wiz, app::AppState& state) {
    const facet::Theme& t = facet::T();
    begin_step_card("##credentials", wiz.relogin ? "Sign in once more" : "Sign in to Steam",
                    wiz.relogin ? nullptr : "The login and password of the account. Steam may ask for a code next.");
    if (wiz.relogin) {
        const std::string message = "Steam ended the saved session of " + wiz.relogin_login +
                                    ". Sign in once more so the app can keep using this account.";
        facet::StyledText(message.c_str(), {.wrap = true});
        gap(t.spacing.lg);
    }
    ImGui::BeginDisabled(wiz.busy);
    facet::TextField("Login", &wiz.username, {.placeholder = "The name you sign in with, not the profile name"});
    facet::SecureField("Password", &wiz.password, {});
    gap(t.spacing.xs);
    if (facet::BeginDisclosure("Steam Guard secret", &wiz.secret_open,
                               {.summary = wiz.shared_secret.empty() ? "Optional" : "Set"})) {
        facet::SecureField("Shared secret", &wiz.shared_secret,
                           {.helpText = "From the account's maFile. With it the app types the Steam Guard codes."});
        facet::EndDisclosure();
    }
    ImGui::EndDisabled();
    if (!wiz.error.empty()) {
        gap(t.spacing.md);
        // The commit button retries, so the alert carries no second retry action.
        (void)flow_error("login-error", "Steam refused the sign-in", friendly_login_error(wiz.error).c_str(), wiz.error);
    }
    end_step_card();

    const bool ready = !wiz.username.empty() && !wiz.password.empty();
    begin_commit_bar(wiz.busy ? "Asking Steam to start a sign-in" : ready ? nullptr : "Enter the login and password");
    ImGui::BeginDisabled(!ready && !wiz.busy);
    if (facet::Button(wiz.relogin ? "Sign in again" : "Sign in to Steam",
                      {.variant = facet::ButtonVariant::Primary, .loading = wiz.busy})) {
        start_sign_in(wiz, state);
    }
    ImGui::EndDisabled();
}

// Why the code is centered and large: on this step the five characters are the only thing the
// user has to look at and type, so they are the focal point of the card.
void draw_guard(LoginWizard& wiz, app::AppState& state) {
    const facet::Theme& t = facet::T();
    const bool conf_active = wiz.confirmation_mode || wiz.email_confirmation_mode;
    if (conf_active) launch_poll_worker(&wiz, state);

    const bool can_auto_submit = !wiz.auto_guard_submitted && wiz.has_device_code && wiz.account.sda.has_value() &&
                                 !wiz.account.sda->shared_secret.empty();
    if (can_auto_submit) {
        wiz.auto_guard_submitted = true;
        wiz.guard_code = sam::sda::generate_code_now(wiz.account.sda->shared_secret);
        submit_code(wiz, state, wiz.guard_code, steam_login::GuardKind::DeviceCode, true);
    }

    const bool show_code_field = wiz.has_device_code || wiz.has_email_code;
    const char* title = wiz.confirmation_mode        ? "Approve the sign-in"
                        : wiz.email_confirmation_mode ? "Approve the sign-in by email"
                        : wiz.chosen_guard == steam_login::GuardKind::EmailCode ? "Enter the code from the email"
                                                                                 : "Enter the code from the app";
    begin_step_card("##guard", title);
    if (wiz.confirmation_mode) {
        waiting("Tap Approve in the Steam Mobile app.");
    } else if (wiz.email_confirmation_mode) {
        waiting("Open the email Steam sent and confirm the sign-in.");
    } else if (wiz.auto_guard_submitted && wiz.busy) {
        waiting("Sending a code made from the shared secret.");
    }

    bool edited = false;
    if (show_code_field) {
        if (conf_active || (wiz.auto_guard_submitted && wiz.busy)) gap(t.spacing.xl);
        const char* instruction = conf_active ? "Or type a Steam Guard code instead"
                                  : wiz.chosen_guard == steam_login::GuardKind::EmailCode
                                      ? "Steam sent a five-character code to the account's email address"
                                      : "The Steam Mobile app shows a five-character code for this account";
        // One centered stack per line: a stack centers a run as a whole, so the shorter code
        // cells would sit at the run's leading edge under the wider instruction.
        const facet::StackOptions centered{.align = facet::LayoutAlign::Center, .width = facet::AvailableWidthUnits()};
        facet::BeginStack("##code-text", centered);
        facet::StyledText(instruction, {.secondary = true});
        facet::EndStack();
        gap(t.spacing.md);
        facet::BeginStack("##code-cells", centered);
        ImGui::BeginDisabled(wiz.busy);
        edited = facet::PinCodeInput("##guard-code", &wiz.guard_code,
                                     {.length = 5,
                                      .numericOnly = false,
                                      .size = facet::ControlSize::Large,
                                      .error = wiz.guard_note.empty() ? nullptr : wiz.guard_note.c_str()});
        if (!wiz.guard_raw.empty() && !wiz.guard_note.empty()) facet::ItemTooltip("Steam said: %s", wiz.guard_raw.c_str());
        ImGui::EndDisabled();
        facet::EndStack();
        std::transform(wiz.guard_code.begin(), wiz.guard_code.end(), wiz.guard_code.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    }
    if (!wiz.error.empty()) {
        gap(t.spacing.lg);
        if (flow_error("guard-error", "The sign-in stopped", friendly_login_error(wiz.error).c_str(), wiz.error,
                       "Sign in again")) {
            start_sign_in(wiz, state);
        }
    }
    end_step_card();

    begin_commit_bar(wiz.busy ? "Waiting for Steam" : nullptr);
    if (facet::Button("Cancel sign-in")) cancel(wiz);
    if (show_code_field) {
        const bool complete = wiz.guard_code.size() == 5;
        ImGui::BeginDisabled(!complete && !wiz.busy);
        const bool pressed = facet::Button("Submit code", {.variant = facet::ButtonVariant::Primary, .loading = wiz.busy});
        ImGui::EndDisabled();
        if ((pressed || (edited && complete && !wiz.busy)) && complete) {
            submit_code(wiz, state, wiz.guard_code, wiz.chosen_guard, false);
        }
    }
}

void draw_polling(LoginWizard& wiz, app::AppState& state) {
    begin_step_card("##session", "Waiting for Steam");
    if (wiz.error.empty()) {
        launch_poll_worker(&wiz, state);
        waiting("Steam is confirming the session. This takes a few seconds.");
    } else if (flow_error("poll-error", "The sign-in stopped", friendly_login_error(wiz.error).c_str(), wiz.error,
                          "Sign in again")) {
        start_sign_in(wiz, state);
    }
    end_step_card();
    begin_commit_bar();
    if (facet::Button("Cancel sign-in")) cancel(wiz);
}

void draw_done(LoginWizard& wiz, app::AppState& state) {
    const facet::Theme& t = facet::T();
    const core::Account* acc = state.find_account(wiz.account_id);
    const std::string who = acc != nullptr ? kit::display_name(state, *acc) : std::string("the account");
    const std::string title = (wiz.merged ? "Signed in again as " : "Added ") + who;
    facet::BeginCard("##done");
    done_header(title.c_str(), wiz.merged ? "The account has a fresh session. Its data refreshes in the background."
                                          : "The account is in the vault. Its data loads in the background.");
    if (acc != nullptr) {
        const facet::StackOptions centered{.align = facet::LayoutAlign::Center, .width = facet::AvailableWidthUnits()};
        facet::BeginStack("##who", centered);
        kit::account_row(state, *acc);
        facet::EndStack();
    }
    bool import_mafile = false;
    if (!wiz.has_authenticator) {
        gap(t.spacing.lg);
        import_mafile = facet::InlineAlert("No authenticator in the vault##no-sda", nullptr,
                                           "Without its Steam Guard secret the app cannot make codes or approve trades "
                                           "for this account. If you have its maFile, import it and it joins this "
                                           "account.",
                                           {.actionLabel = "Import its maFile"}) == facet::InlineAlertAction::Action;
    }
    facet::EndCard();
    if (!wiz.merged) {
        gap(t.spacing.md);
        kit::web_api_key_notice(state, {.compact = true});
    }

    begin_commit_bar();
    const std::string id = wiz.account_id;
    if (facet::Button("Add another account")) {
        wiz.reset();
        add_account_detail::open_chooser();
    } else {
        add_account_detail::open_accounts_button(state, id);
    }
    if (import_mafile) {
        wiz.reset();
        add_account_detail::open_method(add_account_detail::Method::MaFile);
    }
}

}  // namespace

namespace add_account_detail {

void reset_full_login() { wizard().reset(); }

bool relogin_active() { return wizard().relogin && wizard().phase != LoginWizard::Phase::Done; }

bool begin_relogin(app::AppState& state, const std::string& login) {
    LoginWizard& wiz = wizard();
    if (wiz.phase != LoginWizard::Phase::Credentials || wiz.busy) return false;
    wiz.reset();
    wiz.username = login;
    wiz.relogin = true;
    wiz.relogin_login = login;
    if (core::Account* acc = core::store::find_existing_account(state.vault, 0, login)) {
        wiz.relogin_login = kit::login_text(state, *acc);
        if (!acc->password.empty()) wiz.password.assign(acc->password.begin(), acc->password.end());
    }
    return true;
}

// Why the shared flow shell: the step indicator says where the sign-in stands while it waits on
// Steam, the card holds only the current step, and its action stays in the commit bar.
void draw_full_login(app::AppState& state) {
    LoginWizard& wiz = wizard();
    int step = 0;
    switch (wiz.phase) {
        case LoginWizard::Phase::Credentials: step = 0; break;
        case LoginWizard::Phase::GuardWait: step = 1; break;
        case LoginWizard::Phase::Polling: step = 2; break;
        case LoginWizard::Phase::Done: step = 4; break;
    }
    begin_flow("##full-login", kSteps, step, kFormWidth);
    switch (wiz.phase) {
        case LoginWizard::Phase::Credentials: draw_credentials(wiz, state); break;
        case LoginWizard::Phase::GuardWait: draw_guard(wiz, state); break;
        case LoginWizard::Phase::Polling: draw_polling(wiz, state); break;
        case LoginWizard::Phase::Done: draw_done(wiz, state); break;
    }
    end_flow();
}

}  // namespace add_account_detail
#ifdef SAM_UI_CAPTURE
namespace {

using app::AppState;
using namespace add_account_detail;

void setup_login(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::AddAccount;
    reset_screen();
    open_method(Method::FullLogin);
    mark_seeded(state);
}

void setup_login_guard(AppState& state) {
    setup_login(state);
    LoginWizard& w = wizard();
    w.username = "demo-new-account";
    w.password = "placeholder";
    w.phase = LoginWizard::Phase::GuardWait;
    w.has_device_code = true;
    w.chosen_guard = sam::steam_login::GuardKind::DeviceCode;
    w.guard_code = "R4";
}

void setup_login_guard_error(AppState& state) {
    setup_login_guard(state);
    LoginWizard& w = wizard();
    w.guard_code.clear();
    w.guard_note = "Steam did not accept this code. Codes change every 30 seconds: type the one the app shows now.";
    w.guard_raw = "InvalidLoginAuthCode";
}

void setup_login_error(AppState& state) {
    setup_login(state);
    LoginWizard& w = wizard();
    w.username = "demo-new-account";
    w.password = "placeholder";
    w.error = "RateLimitExceeded";
}

void setup_login_relogin(AppState& state) {
    setup_login(state);
    state.pending_relogin_login = state.vault.accounts.size() > 3 ? state.vault.accounts[3].login : "demo-acc";
}

void setup_login_done(AppState& state) {
    setup_login(state);
    LoginWizard& w = wizard();
    w.phase = LoginWizard::Phase::Done;
    w.account_id = sam::app::ui_capture::demo_account_id(2);
    w.has_authenticator = false;
}

}  // namespace
SAM_UI_CAPTURE_SCENE(add_login, "add-login", &setup_login, 30)
SAM_UI_CAPTURE_SCENE(add_login_guard, "add-login-guard", &setup_login_guard, 30)
SAM_UI_CAPTURE_SCENE(add_login_guard_error, "add-login-guard-error", &setup_login_guard_error, 30)
SAM_UI_CAPTURE_SCENE(add_login_error, "add-login-error", &setup_login_error, 30)
SAM_UI_CAPTURE_SCENE(add_login_relogin, "add-login-relogin", &setup_login_relogin, 30)
SAM_UI_CAPTURE_SCENE(add_login_done, "add-login-done", &setup_login_done, 30)
#endif

}  // namespace sam::ui::screens
