// Backup link, maFile export and the secrets of an authenticator. Secrets sit in copy fields,
// masked until revealed.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>

#include "core/crypto/secure_string.hpp"
#include "core/sda/mafile_export.hpp"
#include "platform/clipboard.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/secrets.hpp"
#include "ui/screens/authenticator/dialogs.hpp"

namespace sam::ui::screens::authenticator {

namespace {

constexpr float kDialogWidth = 480.0F;
constexpr float kExportWidth = 520.0F;

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

void wrapped(const char* text, bool secondary = false) {
    facet::StyledText(text, {.secondary = secondary, .wrap = true});
}

void wipe(std::string& s) {
    if (!s.empty()) crypto::zero_buffer(s.data(), s.size());
    s.clear();
}

std::string sanitize_filename(std::string s) {
    for (char& c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == '/' || u == '\\' || u == ':' || u == '*' || u == '?' || u == '"' || u == '<' ||
            u == '>' || u == '|') {
            c = '_';
        }
    }
    if (s.empty()) s = "account";
    return s;
}

// A copy field that also puts the value on the clipboard through the app's clipboard helper.
void secret_field(const char* label, const std::string& value, bool masked) {
    if (value.empty()) return;
    if (facet::CopyField(label, value.c_str(), {.masked = masked})) platform::clipboard::set_text(value);
}

void close_footer(bool* open) {
    gap(facet::T().spacing.lg);
    facet::BeginStack("##close-footer", {.orientation = facet::Orientation::Horizontal,
                                         .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
    if (facet::Button("Close")) *open = false;
    facet::EndStack();
}

// ---- Export -----------------------------------------------------------------------------------

struct ExportRows {
    std::vector<const core::Account*> accounts;
    std::vector<std::string> labels;
    std::vector<std::string> details;
};

bool export_row(void* user, int index, facet::ChoiceItem* out) {
    const auto* rows = static_cast<const ExportRows*>(user);
    if (index < 0 || index >= static_cast<int>(rows->accounts.size())) return false;
    const auto i = static_cast<std::size_t>(index);
    out->label = rows->labels[i].c_str();
    out->detail = rows->details[i].empty() ? nullptr : rows->details[i].c_str();
    return true;
}

void run_export(app::AppState& app, ExportState& s) {
    platform::file_dialog::Options opts;
    opts.parent = app.main_hwnd;
    opts.title = L"Choose a folder for the maFiles";
    const auto picked = platform::file_dialog::pick_folder(opts);
    if (!picked.ok) return;

    crypto::SecureString pass;
    pass.assign(s.passphrase.begin(), s.passphrase.end());
    int wrote = 0;
    int failed = 0;
    std::vector<std::string> errors;
    for (const auto& a : app.vault.accounts) {
        if (!s.selected_ids.count(a.id)) continue;
        if (!a.sda.has_value()) {
            ++failed;
            continue;
        }
        const auto path = picked.path / (sanitize_filename(a.login) + ".maFile");
        std::string err;
        if (sda::encrypt_and_write_mafile(path, a, pass, &err)) {
            ++wrote;
        } else {
            ++failed;
            if (errors.size() < 3) errors.push_back(a.login + ": " + err);
        }
    }
    s.written = wrote;
    s.failed = failed;
    char buf[160];
    std::snprintf(buf, sizeof(buf), wrote == 1 ? "Wrote %d file to the folder." : "Wrote %d files to the folder.",
                  wrote);
    s.status = buf;
    if (failed > 0) {
        std::snprintf(buf, sizeof(buf), " %d failed.", failed);
        s.status += buf;
    }
    for (const auto& e : errors) s.status += "\n" + e;
    crypto::zero_buffer(pass.data(), pass.size());
}

}  // namespace

// ---- Requests ---------------------------------------------------------------------------------

void request_backup(const std::string& account_id) {
    BackupState& s = dialogs().backup;
    s.account_id = account_id;
    s.synthesized_uri.clear();
    s.open = true;
}

void request_export(const std::string& preselect_id) {
    ExportState& s = dialogs().export_mafiles;
    wipe(s.passphrase);
    wipe(s.confirm);
    s.passphrase.reserve(128);
    s.confirm.reserve(128);
    s.selected_ids.clear();
    if (!preselect_id.empty()) s.selected_ids.insert(preselect_id);
    s.opened_for = preselect_id;
    s.status.clear();
    s.written = 0;
    s.failed = 0;
    s.open = true;
}

void request_secrets(const std::string& account_id) {
    SecretsState& s = dialogs().secrets;
    s.account_id = account_id;
    s.open = true;
}

// ---- Dialogs ----------------------------------------------------------------------------------

// Why a single copy field: the link is one secret value the user moves to another app, so it is
// masked like a password and copied whole; a missing link gets an alert with the one fix.
void draw_backup_dialog(app::AppState& app) {
    BackupState& s = dialogs().backup;
    const bool was_open = s.open;
    if (facet::BeginModal("Back up authenticator###authenticator-backup", &s.open, {.width = kDialogWidth})) {
        auto* a = app.find_account(s.account_id);
        if (a == nullptr || !a->sda.has_value()) {
            (void)facet::InlineAlert("##gone", nullptr, "This account no longer has an authenticator.",
                                     {.tone = facet::Tone::Destructive});
        } else {
            kit::account_row(app, *a, {.layout = kit::AccountRowOptions::Layout::OneLine});
            gap(facet::T().spacing.md);
            wrapped("Add this setup link to another authenticator app (Aegis, 1Password, Google Authenticator) "
                    "to keep a second copy of the codes. Anyone with the link can generate them.");
            gap(facet::T().spacing.md);
            const std::string& stored = a->sda->uri;
            const std::string& effective = !stored.empty() ? stored : s.synthesized_uri;
            if (effective.empty()) {
                if (facet::InlineAlert("No setup link saved", nullptr,
                                       "Older imports often leave it out. It can be rebuilt from this "
                                       "authenticator's secret.",
                                       {.tone = facet::Tone::Warning, .actionLabel = "Rebuild link"}) ==
                    facet::InlineAlertAction::Action) {
                    s.synthesized_uri = sda::synthesize_otpauth_uri(a->login, a->sda->shared_secret);
                }
            } else {
                secret_field("Setup link", effective, true);
                if (stored.empty()) {
                    gap(facet::T().spacing.sm);
                    wrapped("Rebuilt from this authenticator's secret. It is not saved in the vault yet.", true);
                    gap(facet::T().spacing.sm);
                    if (facet::Button("Save link to vault")) {
                        a->sda->uri = effective;
                        app.vault_dirty = true;
                        app.save_vault_if_dirty();
                    }
                }
            }
        }
        close_footer(&s.open);
        facet::EndModal();
    }
    if (was_open && !s.open) {
        s.account_id.clear();
        s.synthesized_uri.clear();
    }
}

// Why this order: choose what to export, then protect it, then the one button; the result shows
// in place so the user sees it before closing.
void draw_export_dialog(app::AppState& app) {
    ExportState& s = dialogs().export_mafiles;
    const bool was_open = s.open;
    if (facet::BeginModal("Export maFiles###authenticator-export", &s.open, {.width = kExportWidth})) {
        const facet::Theme& t = facet::T();
        wrapped("Each account becomes one encrypted .maFile in a folder you choose. The passphrase is asked for "
                "again when the file is imported.");
        gap(t.spacing.md);

        ExportRows rows;
        for (const auto& a : app.vault.accounts) {
            if (!a.sda.has_value()) continue;
            rows.accounts.push_back(&a);
            rows.labels.push_back(kit::display_name(app, a));
            rows.details.push_back(a.web.persona_name.empty() || kit::login_hidden(app, a) ? std::string() : a.login);
        }
        const auto n = rows.accounts.size();
        auto checked = std::make_unique<bool[]>(n > 0 ? n : 1);
        // Only listed accounts count as selected, so the checklist's count, the button and the
        // export all agree (a preselected account without an authenticator is not exported).
        std::size_t selected = 0;
        for (std::size_t i = 0; i < n; ++i) {
            checked[i] = s.selected_ids.count(rows.accounts[i]->id) != 0;
            selected += checked[i] ? 1 : 0;
        }
        if (facet::SearchableChecklist("Accounts", std::span<bool>(checked.get(), n), static_cast<int>(n),
                                       &export_row, &rows,
                                       {.placeholder = "Filter accounts",
                                        .visibleRows = 6,
                                        .emptyText = "No account in this vault has an authenticator."})) {
            for (std::size_t i = 0; i < n; ++i) {
                if (checked[i]) s.selected_ids.insert(rows.accounts[i]->id);
                else s.selected_ids.erase(rows.accounts[i]->id);
            }
        }
        gap(t.spacing.lg);

        const bool nonempty = !s.passphrase.empty();
        const bool match = s.passphrase == s.confirm;
        (void)kit::master_password_field("Passphrase", &s.passphrase, {.show_strength = true});
        gap(t.spacing.sm);
        (void)kit::master_password_field(
            "Confirm", &s.confirm,
            {.error = nonempty && !s.confirm.empty() && !match ? "The two passphrases do not match." : nullptr});

        if (!s.status.empty()) {
            gap(t.spacing.md);
            const bool clean = s.written > 0 && s.failed == 0;
            (void)facet::InlineAlert(clean ? "Export finished" : "Export finished with problems", nullptr,
                                     s.status.c_str(), {.tone = clean ? facet::Tone::Success : facet::Tone::Warning});
        }

        gap(t.spacing.lg);
        facet::BeginStack("##export-footer", {.orientation = facet::Orientation::Horizontal,
                                              .width = facet::AvailableWidthUnits()});
        facet::StackSpacer();
        // Cancel while nothing was written; Close once an export ran (rule 8: Cancel abandons a
        // flow, Close leaves a finished one).
        if (facet::Button(s.status.empty() ? "Cancel" : "Close", {.variant = facet::ButtonVariant::Plain})) {
            s.open = false;
        }
        const bool ready = nonempty && match && selected > 0;
        ImGui::BeginDisabled(!ready);
        if (facet::Button("Choose folder and export", {.variant = facet::ButtonVariant::Primary})) run_export(app, s);
        ImGui::EndDisabled();
        facet::EndStack();
        facet::EndModal();
    }
    if (was_open && !s.open) {
        wipe(s.passphrase);
        wipe(s.confirm);
        s.selected_ids.clear();
        s.status.clear();
        s.written = 0;
        s.failed = 0;
    }
}

// Why one account, the selected one: the values are passwords in all but name, so the dialog
// shows the few of the account it was opened for, masked, and nothing else.
void draw_secrets_dialog(app::AppState& app) {
    SecretsState& s = dialogs().secrets;
    if (facet::BeginModal("Authenticator secrets###authenticator-secrets", &s.open, {.width = kExportWidth})) {
        const facet::Theme& t = facet::T();
        auto* a = app.find_account(s.account_id);
        if (a == nullptr || !a->sda.has_value()) {
            (void)facet::InlineAlert("##gone", nullptr, "This account no longer has an authenticator.",
                                     {.tone = facet::Tone::Destructive});
        } else {
            kit::account_row(app, *a, {.layout = kit::AccountRowOptions::Layout::OneLine});
            gap(t.spacing.md);
            (void)facet::InlineAlert("##secrets-warning", nullptr,
                                     "Anyone who sees these can generate codes and approve trades for the account. "
                                     "Treat them like a password.",
                                     {.tone = facet::Tone::Warning});
            gap(t.spacing.md);
            secret_field("Shared secret", a->sda->shared_secret, true);
            secret_field("Identity secret", a->sda->identity_secret, true);
            secret_field("Revocation code", a->sda->revocation_code, true);
            secret_field("Device ID", a->sda->device_id, false);
            secret_field("Serial number", a->sda->serial_number, false);
        }
        close_footer(&s.open);
        facet::EndModal();
    }
}

}  // namespace sam::ui::screens::authenticator
