// The link flow (with its "check with Steam" mode) and the removal of an authenticator: one
// modal, one step per screen, every wait and failure shown inside the step it belongs to.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/job_pump.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/log.hpp"
#include "core/sda/revoke.hpp"
#include "core/sda/totp.hpp"
#include "core/steam_login/session.hpp"
#include "core/time_aligner.hpp"
#include "platform/clipboard.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/screens/authenticator/dialogs.hpp"

namespace sam::ui::screens::authenticator {

Dialogs& dialogs() {
    static Dialogs d;
    return d;
}

namespace {

using LinkStep = LinkFlowState::Step;

constexpr float kDialogWidth = 480.0F;
constexpr const char* kAccountGone = "This account is no longer in the vault.";
constexpr const char* kSessionExpired =
    "The session has expired and no refresh token is available. Sign in again from Add account, "
    "then try again.";

// ---- Steam requests (unchanged behaviour) --------------------------------------------------

void fail_link(LinkFlowState& s, std::string error, std::string details = {}) {
    s.working = false;
    s.working_text.clear();
    s.error = std::move(error);
    s.error_details = std::move(details);
}

void dispatch_add(app::AppState& app, LinkFlowState& s) {
    const std::string aid = s.account_id;
    app::job_pump::submit([aid, &app, &s] {
        bool token_ok = true;
        sda::AddAuthenticatorResult result;
        {
            auto* a = app.find_account(aid);
            if (!a) {
                app.post_ui_callback([&s] { fail_link(s, kAccountGone); });
                return;
            }
            if (steam_login::needs_refresh(*a, 300)) {
                token_ok = !a->refresh_token.empty() && steam_login::refresh_access_token(*a);
            }
            if (token_ok) result = sda::add_authenticator(*a);
        }

        app.post_ui_callback([&app, &s, aid, token_ok, result = std::move(result)]() mutable {
            if (s.account_id != aid) return;
            if (!token_ok) {
                fail_link(s, kSessionExpired);
                return;
            }
            if (!result.ok || !result.sda.has_value()) {
                char buf[64] = "";
                if (result.status_code != 0) std::snprintf(buf, sizeof(buf), "Steam status %d", result.status_code);
                fail_link(s, "Steam did not add an authenticator to this account. Try again in a few minutes.", buf);
                if (!result.raw_response.empty()) SAM_LOG_WARN("AddAuthenticator failed: {}", result.raw_response);
                return;
            }

            auto* a = app.find_account(aid);
            if (!a) {
                fail_link(s, kAccountGone);
                return;
            }
            a->sda = std::move(*result.sda);
            if (a->sda->account_name.empty()) a->sda->account_name = a->login;
            a->sda->fully_enrolled = false;

            app.vault_dirty = true;
            app.save_vault_if_dirty();
            app.flush_pending_save();

            s.working = false;
            s.working_text.clear();
            s.acknowledged = false;
            s.step = LinkStep::Revocation;
        });
    });
}

void dispatch_verify_active(app::AppState& app, LinkFlowState& s);

void dispatch_finalize(app::AppState& app, LinkFlowState& s) {
    const std::string aid = s.account_id;
    const std::string sms_code = s.activation.data();
    app::job_pump::submit([aid, sms_code, &app, &s] {
        sda::FinalizeResult result;
        bool token_ok = true;
        {
            auto* a = app.find_account(aid);
            if (!a) {
                app.post_ui_callback([&s] { fail_link(s, kAccountGone); });
                return;
            }
            if (steam_login::needs_refresh(*a, 300)) {
                token_ok = !a->refresh_token.empty() && steam_login::refresh_access_token(*a);
            }
            if (token_ok) result = sda::finalize_add(*a, sms_code);
        }

        app.post_ui_callback([&app, &s, aid, token_ok, result = std::move(result)] {
            if (s.account_id != aid) return;
            if (!token_ok) {
                fail_link(s, kSessionExpired);
                return;
            }
            if (result.ok) {
                auto* a = app.find_account(aid);
                if (a && a->sda.has_value()) {
                    a->sda->fully_enrolled = true;
                    app.vault_dirty = true;
                    app.save_vault_if_dirty();
                }
                s.working = false;
                s.step = LinkStep::Done;
                return;
            }
            if (result.needs_retry) {
                s.bad_code = true;
                s.activation.fill(0);
                s.working = false;
                s.step = LinkStep::Activation;
                return;
            }
            s.working_text = "Checking the authenticator with Steam";
            s.working = true;
            dispatch_verify_active(app, s);
        });
    });
}

void dispatch_verify_active(app::AppState& app, LinkFlowState& s) {
    const std::string aid = s.account_id;
    app::job_pump::submit([aid, &app, &s] {
        sda::TwoFactorStatus status;
        bool token_ok = true;
        std::string mafile_token_gid;
        {
            auto* a = app.find_account(aid);
            if (!a || !a->sda.has_value()) {
                app.post_ui_callback([&s] { fail_link(s, kAccountGone); });
                return;
            }
            mafile_token_gid = a->sda->token_gid;
            if (steam_login::needs_refresh(*a, 300)) {
                token_ok = !a->refresh_token.empty() && steam_login::refresh_access_token(*a);
            }
            if (token_ok) status = sda::query_two_factor_status(*a);
        }

        app.post_ui_callback([&app, &s, aid, token_ok, mafile_token_gid, status = std::move(status)] {
            if (s.account_id != aid) return;
            if (!token_ok) {
                fail_link(s, kSessionExpired);
                return;
            }
            if (!status.ok) {
                fail_link(s, "Steam could not be reached to check the authenticator. Try again in a few minutes.",
                          status.error);
                return;
            }

            const bool has_gid = !mafile_token_gid.empty();
            const bool steam_has_authenticator = status.authenticator_type == 1 || !status.token_gid.empty();
            const bool confirmed = has_gid ? (!status.token_gid.empty() && status.token_gid == mafile_token_gid)
                                           : status.authenticator_type == 1;
            if (confirmed) {
                if (auto* a = app.find_account(aid); a && a->sda.has_value()) {
                    a->sda->fully_enrolled = true;
                    app.vault_dirty = true;
                    app.save_vault_if_dirty();
                }
                s.working = false;
                s.step = LinkStep::Done;
                return;
            }
            fail_link(s, (has_gid && steam_has_authenticator)
                             ? "Another authenticator is active on this account, so the codes saved here "
                               "will not match it. Remove the other authenticator, or import the files of "
                               "the one that is active."
                             : "Steam has no mobile authenticator on record for this account, so the one "
                               "saved here cannot be marked as linked. It may have been removed, or its "
                               "activation was never finished.");
        });
    });
}

void dispatch_remove(app::AppState& app, RemoveFlowState& s) {
    const std::string aid = s.account_id;
    const int scheme = s.scheme;
    app::job_pump::submit([aid, scheme, &app, &s] {
        sda::RemoveResult result;
        bool token_ok = true;
        {
            auto* a = app.find_account(aid);
            if (!a) {
                app.post_ui_callback([&s] {
                    s.working = false;
                    s.error = kAccountGone;
                });
                return;
            }
            if (steam_login::needs_refresh(*a, 300)) {
                token_ok = !a->refresh_token.empty() && steam_login::refresh_access_token(*a);
            }
            if (token_ok) result = sda::remove_authenticator(*a, scheme);
        }

        app.post_ui_callback([&app, &s, aid, token_ok, result = std::move(result)] {
            if (s.account_id != aid) return;
            s.working = false;
            if (!token_ok) {
                s.error = kSessionExpired;
                return;
            }
            if (!result.ok) {
                s.error = "Steam did not remove the authenticator. Check that the revocation code is the "
                          "right one, then try again.";
                s.error_details = result.error;
                return;
            }
            if (auto* a = app.find_account(aid)) {
                a->sda.reset();
                app.vault_dirty = true;
                app.save_vault_if_dirty();
                app.flush_pending_save();
            }
            s.done = true;
        });
    });
}

// ---- Shared pieces ----------------------------------------------------------------------------

void wrapped(const char* text, bool secondary = false) {
    facet::StyledText(text, {.secondary = secondary, .wrap = true});
}

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

void uppercase(char* buf) {
    for (char* c = buf; *c != '\0'; ++c) *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
}

bool enter_pressed_on_field() {
    return ImGui::IsItemDeactivated() &&
           (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
}

// Opens the footer row: leading items, then StackSpacer, then the trailing buttons.
void begin_footer(const char* id) {
    gap(facet::T().spacing.lg);
    facet::BeginStack(id, {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
}

void working_line(const std::string& text) {
    facet::Spinner(text.empty() ? "Waiting for Steam" : text.c_str());
}

// A failure in plain words; Steam's own text, which helps support, behind "Details".
void error_block(const char* title, const std::string& error, const std::string& details) {
    (void)facet::InlineAlert(title, nullptr, error.c_str(), {.tone = facet::Tone::Destructive});
    if (details.empty()) return;
    gap(facet::T().spacing.xs);
    if (facet::BeginDisclosure("Details", nullptr, {.style = facet::DisclosureStyle::Link})) {
        facet::StyledText(details.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        facet::EndDisclosure();
    }
}

// ---- Link flow --------------------------------------------------------------------------------

constexpr const char* const kLinkSteps[] = {"Request", "Revocation code", "Activation code", "Done"};

void close_link(LinkFlowState& s) {
    s.open = false;
}

void start_add(app::AppState& app, LinkFlowState& s) {
    s.error.clear();
    s.error_details.clear();
    s.working = true;
    s.working_text = "Asking Steam to add an authenticator";
    dispatch_add(app, s);
}

void start_finalize(app::AppState& app, LinkFlowState& s) {
    s.bad_code = false;
    s.error.clear();
    s.error_details.clear();
    s.working = true;
    s.working_text = "Checking the activation code with Steam";
    dispatch_finalize(app, s);
}

void start_verify(app::AppState& app, LinkFlowState& s) {
    s.bad_code = false;
    s.error.clear();
    s.error_details.clear();
    s.working = true;
    s.working_text = "Checking the authenticator with Steam";
    dispatch_verify_active(app, s);
}

// Step 1: what linking does, then the request to Steam.
void link_intro(app::AppState& app, LinkFlowState& s) {
    wrapped("Steam sends an activation code by SMS to the phone number on the account, or by email when "
            "the account has no phone.");
    gap(facet::T().spacing.sm);
    wrapped("Next you get a revocation code. Write it down before you finish: it is the only way to remove "
            "the authenticator if you lose this vault.");
    if (!s.error.empty()) {
        gap(facet::T().spacing.md);
        error_block("Steam did not link the authenticator", s.error, s.error_details);
    }
    begin_footer("##intro-footer");
    if (s.working) working_line(s.working_text);
    facet::StackSpacer();
    if (facet::Button("Cancel", {.variant = facet::ButtonVariant::Plain})) close_link(s);
    if (facet::Button(s.error.empty() ? "Ask Steam to link" : "Try again",
                      {.variant = facet::ButtonVariant::Primary, .loading = s.working})) {
        start_add(app, s);
    }
    facet::EndStack();
}

// Step 2: the revocation code, which cannot be skipped without the acknowledgement.
void link_revocation(core::Account& a, LinkFlowState& s) {
    if (!a.sda.has_value()) {
        (void)facet::InlineAlert("Authenticator data missing", nullptr,
                                 "The vault holds no authenticator for this account any more.",
                                 {.tone = facet::Tone::Destructive});
        begin_footer("##rcode-missing-footer");
        facet::StackSpacer();
        if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) close_link(s);
        facet::EndStack();
        return;
    }
    (void)facet::InlineAlert("Write this code down now", nullptr,
                             "It is the only way to remove the authenticator if you lose this vault or "
                             "this PC. Steam cannot show it again.",
                             {.tone = facet::Tone::Warning, .icon = icons::token});
    gap(facet::T().spacing.md);
    if (facet::CopyField("Revocation code", a.sda->revocation_code.c_str(), {.size = facet::ControlSize::Large})) {
        platform::clipboard::set_text(a.sda->revocation_code);
    }
    gap(facet::T().spacing.md);
    (void)facet::Checkbox("I wrote the revocation code down outside this app", &s.acknowledged);

    begin_footer("##rcode-footer");
    facet::StackSpacer();
    if (facet::Button("Finish later", {.variant = facet::ButtonVariant::Plain})) close_link(s);
    facet::ItemTooltip("The authenticator stays half linked. Reopen this flow from the Authenticator screen "
                       "to enter the activation code.");
    ImGui::BeginDisabled(!s.acknowledged);
    if (facet::Button("Continue to activation", {.variant = facet::ButtonVariant::Primary})) {
        s.bad_code = false;
        s.activation.fill(0);
        s.step = LinkStep::Activation;
    }
    ImGui::EndDisabled();
    facet::EndStack();
}

// Step 3: the activation code from the SMS or email.
void link_activation(app::AppState& app, core::Account& a, LinkFlowState& s) {
    wrapped("Steam sent an activation code by SMS or email. Enter it to finish linking.");
    gap(facet::T().spacing.md);
    ImGui::BeginDisabled(s.working);
    if (facet::TextField("Activation code", s.activation.data(), s.activation.size(),
                         {.placeholder = "From the SMS or email",
                          .selectAllOnFocus = true,
                          .width = 320.0F,
                          .error = s.bad_code ? "Steam rejected that code. Enter the newest one it sent." : nullptr})) {
        uppercase(s.activation.data());
    }
    const bool submitted = enter_pressed_on_field();
    ImGui::EndDisabled();
    const bool has_code = s.activation[0] != '\0';

    if (!s.error.empty()) {
        gap(facet::T().spacing.md);
        error_block("Linking did not finish", s.error, s.error_details);
        if (a.sda.has_value() && !a.sda->fully_enrolled) {
            gap(facet::T().spacing.sm);
            wrapped("A half-linked authenticator is stored in the vault. Reopen this flow later to enter the "
                    "activation code.",
                    true);
        }
    }

    begin_footer("##activation-footer");
    if (s.working) {
        working_line(s.working_text);
    } else if (facet::Button("It already works", {.variant = facet::ButtonVariant::Plain})) {
        start_verify(app, s);
    }
    if (!s.working) {
        facet::ItemTooltip("Activated this authenticator elsewhere already? Steam confirms it and the link is "
                           "marked as finished, without an activation code.");
    }
    facet::StackSpacer();
    if (facet::Button(s.error.empty() ? "Cancel" : "Close", {.variant = facet::ButtonVariant::Plain})) close_link(s);
    ImGui::BeginDisabled(!has_code);
    if (facet::Button("Finish linking", {.variant = facet::ButtonVariant::Primary, .loading = s.working}) ||
        (submitted && has_code && !s.working)) {
        start_finalize(app, s);
    }
    ImGui::EndDisabled();
    facet::EndStack();
}

void link_done(LinkFlowState& s) {
    (void)facet::InlineAlert("Authenticator linked", nullptr,
                             "This app now generates the account's Steam Guard codes and can approve its "
                             "trade and market confirmations.",
                             {.tone = facet::Tone::Success});
    begin_footer("##done-footer");
    facet::StackSpacer();
    if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) close_link(s);
    facet::EndStack();
}

// Check-with-Steam mode: one screen, no steps.
void verify_body(LinkFlowState& s) {
    if (s.working) {
        working_line(s.working_text);
    } else if (!s.error.empty()) {
        error_block("Steam did not confirm this authenticator", s.error, s.error_details);
    } else if (s.step == LinkStep::Done) {
        (void)facet::InlineAlert("Steam confirms this authenticator", nullptr,
                                 "It is the authenticator Steam has on record for the account, and it is "
                                 "marked as fully linked.",
                                 {.tone = facet::Tone::Success});
    }
    begin_footer("##verify-footer");
    facet::StackSpacer();
    if (facet::Button("Close", {.variant = s.working ? facet::ButtonVariant::Secondary : facet::ButtonVariant::Primary})) {
        close_link(s);
    }
    facet::EndStack();
}

// ---- Remove flow ------------------------------------------------------------------------------

bool code_matches(const std::string& typed, const std::string& expected) {
    return !expected.empty() && typed.size() == expected.size() &&
           std::equal(typed.begin(), typed.end(), expected.begin(), [](char l, char r) {
               return std::toupper(static_cast<unsigned char>(l)) == std::toupper(static_cast<unsigned char>(r));
           });
}

void remove_done(RemoveFlowState& s) {
    (void)facet::InlineAlert(s.scheme == 2 ? "Steam Guard removed" : "Switched to email codes", nullptr,
                             s.scheme == 2 ? "The account has no Steam Guard now. Only its password protects it."
                                           : "Steam Guard stays on. Sign-in codes now go to the account's email.",
                             {.tone = facet::Tone::Success});
    gap(facet::T().spacing.sm);
    wrapped("Trades and Community Market listings are on hold for 15 days.", true);
    begin_footer("##remove-done-footer");
    facet::StackSpacer();
    if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) s.open = false;
    facet::EndStack();
}

// Why this order: consequences first, then the choice that changes them, then the code that
// gets the account back, then the check and the button, so nothing is confirmed unread.
void remove_body(app::AppState& app, core::Account& a, RemoveFlowState& s) {
    const facet::Theme& t = facet::T();
    (void)facet::InlineAlert("Trades are held for 15 days", nullptr,
                             "After the authenticator is removed, Steam holds trades and Community Market "
                             "listings of this account for 15 days.",
                             {.tone = facet::Tone::Warning});
    gap(t.spacing.lg);

    static const facet::ChoiceItem kSchemes[] = {
        {.label = "Switch to email codes", .detail = "Steam Guard stays on; sign-in codes go to the account's email."},
        {.label = "Turn Steam Guard off", .detail = "Only the password protects the account. Not recommended."},
    };
    int scheme_index = s.scheme == 2 ? 1 : 0;
    ImGui::BeginDisabled(s.working);
    if (facet::RadioGroup("After removal", &scheme_index, kSchemes)) s.scheme = scheme_index == 1 ? 2 : 1;
    ImGui::EndDisabled();
    gap(t.spacing.lg);

    const bool has_rcode = !a.sda->revocation_code.empty();
    if (has_rcode) {
        if (facet::CopyField("Revocation code", a.sda->revocation_code.c_str(),
                             {.masked = true, .helpText = "Keep a copy: it cannot be regenerated."})) {
            platform::clipboard::set_text(a.sda->revocation_code);
        }
    } else {
        (void)facet::InlineAlert("No revocation code stored", nullptr,
                                 "Steam refuses the removal without it. Add the code you saved when the "
                                 "authenticator was linked, on the Authenticator screen.",
                                 {.tone = facet::Tone::Destructive});
    }
    gap(t.spacing.lg);

    const std::string expected = time_aligner::synced()
                                     ? sda::generate_code(a.sda->shared_secret, time_aligner::aligned_now())
                                     : std::string{};
    const std::string typed = s.code.data();
    const bool code_ok = code_matches(typed, expected);
    const char* code_error = nullptr;
    if (!expected.empty() && typed.size() >= expected.size() && !code_ok) {
        code_error = "This is not the current code of this authenticator.";
    }
    ImGui::BeginDisabled(s.working || !has_rcode);
    if (facet::TextField("Current code", s.code.data(), s.code.size(),
                         {.placeholder = "5 characters",
                          .maxLength = 5,
                          .selectAllOnFocus = true,
                          .width = 320.0F,
                          .error = code_error,
                          .helpText = expected.empty() ? "Waiting for the Steam time sync"
                                                       : "Type the code shown on the Authenticator screen."})) {
        uppercase(s.code.data());
    }
    ImGui::EndDisabled();

    if (!s.error.empty()) {
        gap(t.spacing.md);
        error_block("Steam did not remove the authenticator", s.error, s.error_details);
    }

    begin_footer("##remove-footer");
    if (s.working) working_line("Asking Steam to remove the authenticator");
    facet::StackSpacer();
    if (facet::Button("Cancel", {.variant = facet::ButtonVariant::Plain})) s.open = false;
    ImGui::BeginDisabled(!(code_ok && has_rcode));
    if (facet::Button(s.scheme == 2 ? "Remove Steam Guard" : "Remove authenticator",
                      {.variant = facet::ButtonVariant::Destructive, .loading = s.working})) {
        s.error.clear();
        s.error_details.clear();
        s.working = true;
        dispatch_remove(app, s);
    }
    ImGui::EndDisabled();
    facet::EndStack();
}

void account_line(app::AppState& app, const core::Account& a) {
    kit::account_row(app, a, {.layout = kit::AccountRowOptions::Layout::OneLine});
    gap(facet::T().spacing.md);
}

void missing_account(bool* open, const char* message) {
    (void)facet::InlineAlert("##missing", nullptr, message, {.tone = facet::Tone::Destructive});
    begin_footer("##missing-footer");
    facet::StackSpacer();
    if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) *open = false;
    facet::EndStack();
}

}  // namespace

// ---- Requests ---------------------------------------------------------------------------------

void request_link(app::AppState& app, const std::string& account_id) {
    LinkFlowState& s = dialogs().link;
    s.mode = LinkFlowState::Mode::Link;
    s.account_id = account_id;
    s.activation.fill(0);
    s.error.clear();
    s.error_details.clear();
    s.working = false;
    s.working_text.clear();
    s.acknowledged = false;
    s.bad_code = false;
    s.open = true;
    auto* a = app.find_account(account_id);
    if (a && a->sda.has_value() && !a->sda->fully_enrolled) {
        s.acknowledged = true;
        s.step = LinkStep::Activation;
    } else {
        s.step = LinkStep::Intro;
    }
}

void request_verify(app::AppState& app, const std::string& account_id) {
    LinkFlowState& s = dialogs().link;
    s.mode = LinkFlowState::Mode::Verify;
    s.account_id = account_id;
    s.activation.fill(0);
    s.acknowledged = true;
    s.step = LinkStep::Activation;
    s.open = true;
    start_verify(app, s);
}

void request_remove(const std::string& account_id) {
    RemoveFlowState& s = dialogs().remove;
    s.account_id = account_id;
    s.code.fill(0);
    s.error.clear();
    s.error_details.clear();
    s.scheme = 1;
    s.working = false;
    s.done = false;
    s.open = true;
}

// ---- Lifetime ---------------------------------------------------------------------------------

void reset_dialogs(app::AppState& /*app*/) {
    Dialogs& d = dialogs();
    // A request in flight keeps its account id and flags: its answer must still reach the vault
    // (a new authenticator's secrets, a removal). Only the dialog closes.
    const bool link_busy = d.link.working;
    LinkFlowState link_kept = d.link;
    d.link = LinkFlowState{};
    if (link_busy) {
        d.link.account_id = link_kept.account_id;
        d.link.mode = link_kept.mode;
        d.link.step = link_kept.step;
        d.link.working = true;
        d.link.working_text = link_kept.working_text;
    }
    const bool remove_busy = d.remove.working;
    const std::string remove_id = d.remove.account_id;
    const int remove_scheme = d.remove.scheme;
    d.remove = RemoveFlowState{};
    if (remove_busy) {
        d.remove.account_id = remove_id;
        d.remove.scheme = remove_scheme;
        d.remove.working = true;
    }
    d.backup = BackupState{};
    if (!d.export_mafiles.passphrase.empty()) crypto::zero_buffer(d.export_mafiles.passphrase.data(), d.export_mafiles.passphrase.size());
    if (!d.export_mafiles.confirm.empty()) crypto::zero_buffer(d.export_mafiles.confirm.data(), d.export_mafiles.confirm.size());
    d.export_mafiles = ExportState{};
    d.secrets = SecretsState{};
}

void close_dialogs_not_for(const std::string& selected_id) {
    Dialogs& d = dialogs();
    if (d.link.open && !d.link.working && d.link.account_id != selected_id) d.link.open = false;
    if (d.remove.open && !d.remove.working && d.remove.account_id != selected_id) d.remove.open = false;
    if (d.backup.open && d.backup.account_id != selected_id) d.backup.open = false;
    if (d.export_mafiles.open && d.export_mafiles.opened_for != selected_id) d.export_mafiles.open = false;
    if (d.secrets.open && d.secrets.account_id != selected_id) d.secrets.open = false;
}

// ---- Dialogs ----------------------------------------------------------------------------------

// Why a modal with a step indicator and not WizardFooter: the steps advance on Steam's answers,
// not on Next, and none of them can be gone back to, so each step owns its buttons.
void draw_link_dialog(app::AppState& app) {
    LinkFlowState& s = dialogs().link;
    const bool verify = s.mode == LinkFlowState::Mode::Verify;
    const char* title = verify ? "Check with Steam###authenticator-link" : "Link authenticator###authenticator-link";
    const bool was_open = s.open;
    if (facet::BeginModal(title, &s.open, {.dismissible = !s.working, .closeButton = !s.working, .width = kDialogWidth})) {
        auto* a = app.find_account(s.account_id);
        if (a == nullptr) {
            missing_account(&s.open, kAccountGone);
        } else {
            account_line(app, *a);
            if (verify) {
                verify_body(s);
            } else {
                facet::StepIndicator("##link-steps", static_cast<int>(s.step), kLinkSteps);
                gap(facet::T().spacing.lg);
                switch (s.step) {
                    case LinkStep::Intro: link_intro(app, s); break;
                    case LinkStep::Revocation: link_revocation(*a, s); break;
                    case LinkStep::Activation: link_activation(app, *a, s); break;
                    case LinkStep::Done: link_done(s); break;
                }
            }
        }
        facet::EndModal();
    }
    if (was_open && !s.open && !s.working) {
        s.activation.fill(0);
        s.error.clear();
        s.error_details.clear();
    }
}

void draw_remove_dialog(app::AppState& app) {
    RemoveFlowState& s = dialogs().remove;
    const bool was_open = s.open;
    if (facet::BeginModal("Remove authenticator###authenticator-remove", &s.open,
                          {.dismissible = !s.working, .closeButton = !s.working, .width = kDialogWidth})) {
        auto* a = app.find_account(s.account_id);
        if (a == nullptr) {
            missing_account(&s.open, kAccountGone);
        } else if (s.done) {
            account_line(app, *a);
            remove_done(s);
        } else if (!a->sda.has_value()) {
            account_line(app, *a);
            missing_account(&s.open, "This account has no authenticator.");
        } else {
            account_line(app, *a);
            remove_body(app, *a, s);
        }
        facet::EndModal();
    }
    if (was_open && !s.open && !s.working) s.code.fill(0);
}

}  // namespace sam::ui::screens::authenticator
