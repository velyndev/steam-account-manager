// Edit account (a drawer) and Edit notes (a modal), hosted by the Accounts screen. Both are keyed
// to the account they were opened for and close when another account is selected, when the user
// leaves Accounts, and when the vault locks or changes.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>

#include "core/account_store/store.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/steam_local/loginusers.hpp"
#include "core/steam_login/session.hpp"
#include "platform/clipboard.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/standing.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/screens/add_account_detail.hpp"
#include "ui/screens/add_account_screen.hpp"
#include "ui/util.hpp"

#ifdef SAM_UI_CAPTURE
#include "app/ui_capture.hpp"
#include "ui/widgets/bundle_dialogs.hpp"
#endif

namespace sam::ui::screens {

namespace {

using add_account_detail::gap;
using add_account_detail::hint;

constexpr float kDrawerWidth = 480.0F;  // units: one account's settings rows

void wipe(std::string& s) {
    crypto::zero_buffer(s.data(), s.size());
    s.clear();
}

std::string full_login_token(const core::Account& a, const std::string& jwt) {
    if (jwt.empty()) return {};
    if (!a.login.empty()) return a.login + "----" + jwt;
    if (a.steam_id_64 != 0) return std::to_string(a.steam_id_64) + "----" + jwt;
    return jwt;
}

std::string expiry_text(std::int64_t exp, std::int64_t now) {
    if (exp <= 0) return "Unknown";
    const std::int64_t remaining = exp - now;
    if (remaining <= 0) return "Expired";
    char buf[48];
    if (remaining < 86400) {
        std::snprintf(buf, sizeof(buf), "In %lld hours", static_cast<long long>(remaining / 3600));
    } else {
        std::snprintf(buf, sizeof(buf), "In %lld days", static_cast<long long>(remaining / 86400));
    }
    return buf;
}

// One labelled row: the label in the row's label column, a one-line description under it, the
// control beside it (stacked under the label when the drawer is narrow, the same for every row).
class Row {
public:
    Row(const char* label, const char* description = nullptr) { facet::BeginSettingsRow(label, {.description = description}); }
    ~Row() { facet::EndSettingsRow(); }
    Row(const Row&) = delete;
    Row& operator=(const Row&) = delete;
};

// The token for copying: masked, and copied through the app's clipboard that clears itself.
void token_row(const app::AppState& state, const std::string& full) {
    char help[96];
    std::snprintf(help, sizeof(help), "Copy gives the login and the token as one line; it is cleared after %d s.",
                  state.settings.clipboard_clear_seconds);
    Row row("Token", help);
    if (facet::CopyField("##value", full.c_str(), {.masked = true})) {
        platform::clipboard::set_text_with_auto_clear(full, std::chrono::seconds(state.settings.clipboard_clear_seconds));
    }
}

// --- State -------------------------------------------------------------------------------------

struct NotesEdit {
    bool open = false;
    std::string account_id;
    std::string notes;
};

NotesEdit& notes_edit() {
    static NotesEdit s;
    return s;
}

enum class Kind { Password, Nfa, Cached };

struct AccountEdit {
    bool open = false;
    std::string account_id;
    Kind kind = Kind::Password;
    std::string login;
    std::string password;
    std::string shared_secret;
    std::string notes;
    int group = 0;  // 0: no group, else index + 1 in vault.groups
    std::vector<std::string> tags;  // tag names
    core::TrustLabel trust = core::TrustLabel::Unset;

    bool secrets_open = false;
    bool replace_open = false;
    std::string replace_text;
    std::string replace_checked;
    add_account_detail::JwtCheck replace_check;
    std::string replace_error;
    bool convert_confirm = false;

    void clear_secrets() {
        wipe(password);
        wipe(shared_secret);
        wipe(replace_text);
        wipe(replace_checked);
    }
};

AccountEdit& account_edit() {
    static AccountEdit s;
    return s;
}

struct MintState {
    std::string account_id;  // the account a token is being created for; empty when none
    std::string error;
    std::string error_account_id;
};

MintState& mint() {
    static MintState m;
    return m;
}

void close_account_edit() {
    AccountEdit& e = account_edit();
    e.open = false;
    e.clear_secrets();
    e.account_id.clear();
    e.convert_confirm = false;
}

void close_notes() {
    NotesEdit& n = notes_edit();
    n.open = false;
    n.account_id.clear();
    n.notes.clear();
}

// Rule 1: both editors belong to one account; they close on lock, on a vault change and when the
// user leaves Accounts. The capture harness runs this before every scene.
void close_editors(app::AppState&) {
    close_account_edit();
    close_notes();
    mint().error.clear();
}

void load(app::AppState& state, AccountEdit& e, const core::Account& acc) {
    e.clear_secrets();
    e.account_id = acc.id;
    e.kind = !acc.is_nfa ? Kind::Password : core::store::is_cached_account(acc) ? Kind::Cached : Kind::Nfa;
    e.login = acc.login;
    e.password.assign(acc.password.begin(), acc.password.end());
    e.shared_secret = acc.sda.has_value() ? acc.sda->shared_secret : std::string{};
    e.notes = to_utf8(acc.notes);
    e.group = 0;
    for (std::size_t i = 0; i < state.vault.groups.size(); ++i) {
        if (state.vault.groups[i].id == acc.group_id) e.group = static_cast<int>(i) + 1;
    }
    e.tags.clear();
    for (const auto& tid : acc.tag_ids) {
        for (const auto& t : state.vault.tags) {
            if (t.id == tid) e.tags.push_back(t.name);
        }
    }
    e.trust = acc.trust;
    e.secrets_open = false;
    e.replace_open = false;
    e.replace_error.clear();
    e.replace_check = {};
    e.convert_confirm = false;
}

void apply_details(app::AppState& state, const AccountEdit& e, core::Account& acc) {
    acc.notes = to_wide(e.notes);
    if (e.group == 0) {
        acc.group_id.clear();
    } else if (e.group - 1 < static_cast<int>(state.vault.groups.size())) {
        acc.group_id = state.vault.groups[static_cast<std::size_t>(e.group - 1)].id;
    }
    std::vector<std::string> ids;
    for (const auto& name : e.tags) {
        for (const auto& t : state.vault.tags) {
            if (t.name == name && std::find(ids.begin(), ids.end(), t.id) == ids.end()) {
                ids.push_back(t.id);
                break;
            }
        }
    }
    acc.tag_ids = std::move(ids);
    acc.trust = e.trust;
}

void save(app::AppState& state, AccountEdit& e, core::Account& acc) {
    apply_details(state, e, acc);
    std::string new_id_for_refresh;
    if (e.kind == Kind::Password) {
        const std::string old_login = acc.login;
        acc.login = e.login;
        acc.password = crypto::make_secure(e.password);
        if (!e.shared_secret.empty()) {
            if (!acc.sda.has_value()) {
                core::SteamGuardAccount g;
                g.account_name = acc.login;
                acc.sda = std::move(g);
            }
            acc.sda->shared_secret = e.shared_secret;
            if (acc.sda->account_name.empty() || acc.sda->account_name == old_login) acc.sda->account_name = acc.login;
        }
        if (acc.steam_id_64 == 0 && !acc.login.empty()) {
            acc.steam_id_64 = steam_local::lookup_steam_id(acc.login);
            if (acc.steam_id_64 != 0) new_id_for_refresh = acc.id;
        }
    }
    state.vault_dirty = true;
    state.save_vault_if_dirty();
    if (!new_id_for_refresh.empty()) state.pull_all_for_account(new_id_for_refresh);
}

// --- Cards -------------------------------------------------------------------------------------

void identity_card(app::AppState& state, AccountEdit& e, const core::Account& acc) {
    facet::BeginCard("##identity");
    // The kind badge sits under the identity: beside it, the row's full width pushed it out of the card.
    kit::account_row(state, acc);
    (void)kit::account_kind_badge(acc);
    gap(facet::T().spacing.md);
    {
        Row row("Notes");
        facet::TextArea("##value", &e.notes, {.placeholder = "Anything to remember about this account",
                                              .minLines = 2,
                                              .maxLines = 6});
    }
    facet::EndCard();
}

void organisation_card(app::AppState& state, AccountEdit& e) {
    facet::BeginCard("Organisation");
    {
        Row row("Group");
        std::vector<facet::ChoiceItem> groups;
        groups.push_back({.label = "No group"});
        for (const auto& g : state.vault.groups) groups.push_back({.label = g.name.c_str()});
        (void)facet::Dropdown("##value", &e.group, groups);
    }
    {
        // Not inside a settings row: the row's pushed item width stretched every chip to the full
        // field. The field's own label takes the same label column, so the rows still line up.
        std::vector<const char*> names;
        for (const auto& t : state.vault.tags) names.push_back(t.name.c_str());
        (void)facet::TagInput("Tags", &e.tags,
                              {.placeholder = state.vault.tags.empty() ? "No tags in this vault" : "Add a tag",
                               .suggestions = names,
                               .allowCreate = false});
    }
    {
        Row row("Trust", "Your own rating of this account.");
        (void)kit::trust_dropdown("##value", &e.trust);
    }
    facet::EndCard();
}

void sign_in_card(AccountEdit& e) {
    facet::BeginCard("Sign-in");
    {
        Row row("Login", "Renaming here changes only the vault.");
        facet::TextField("##value", &e.login, {.error = e.login.empty() ? "The login cannot be empty." : nullptr});
    }
    if (facet::BeginDisclosure("Password and Steam Guard secret", &e.secrets_open,
                               {.summary = e.shared_secret.empty() ? "No secret stored" : nullptr})) {
        {
            Row row("Password", "Stored encrypted; sign-in types it into Steam.");
            facet::SecureField("##value", &e.password, {});
        }
        {
            Row row("Shared secret", "From the maFile; the app makes codes with it.");
            facet::SecureField("##value", &e.shared_secret, {});
        }
        facet::EndDisclosure();
    }
    facet::EndCard();
}

std::string friendly_mint_error(const std::string& raw) {
    if (raw.find("password") != std::string::npos || raw.find("Password") != std::string::npos) {
        return "Steam did not accept the stored password. Check it under Sign-in, then try again.";
    }
    return "The app could not sign in to Steam to create the token. Try again in a few minutes.";
}

void password_token_card(app::AppState& state, const core::Account& acc) {
    facet::BeginCard("Sign-in token");
    const std::string jwt = app::cs2_client_token(acc);
    const std::int64_t now = now_seconds();
    MintState& m = mint();
    if (jwt.empty()) {
        hint("Lets the app sign this account in without typing the password, and lets CS2 features connect. The "
             "app creates one the first time the account signs in, or now from the stored password.");
        gap(facet::T().spacing.sm);
        const bool creating = m.account_id == acc.id;
        const bool can_create = !acc.password.empty();
        ImGui::BeginDisabled(!can_create && !creating);
        if (facet::Button("Create token", {.icon = icons::token, .loading = creating})) {
            m.account_id = acc.id;
            m.error.clear();
            m.error_account_id.clear();
            const std::string id = acc.id;
            state.acquire_cm_token(id, [id](bool ok, std::string err) {
                MintState& ms = mint();
                ms.account_id.clear();
                if (!ok) {
                    ms.error = std::move(err);
                    ms.error_account_id = id;
                }
            });
        }
        ImGui::EndDisabled();
        if (!can_create) facet::ItemTooltip("Creating a token needs the stored password; this account has none.");
        if (!m.error.empty() && m.error_account_id == acc.id) {
            gap(facet::T().spacing.sm);
            (void)add_account_detail::flow_error("mint-error", "No token was created", friendly_mint_error(m.error).c_str(),
                                                 m.error);
        }
        facet::EndCard();
        return;
    }
    token_row(state, full_login_token(acc, jwt));
    {
        const std::int64_t exp = steam_login::jwt_expiry(crypto::make_secure(jwt));
        facet::BeginKeyValueList("##token-facts");
        facet::KeyValue("Expires", expiry_text(exp, now).c_str(),
                        {.tone = exp > 0 && exp <= now ? facet::Tone::Destructive : facet::Tone::Neutral});
        facet::EndKeyValueList();
    }
    if (acc.cm_status == core::NfaTokenStatus::Revoked) {
        gap(facet::T().spacing.sm);
        facet::InlineAlert("Steam refused this token##revoked", nullptr,
                           "It failed at the last sign-in. The app creates a new one at the next launch.",
                           {.tone = facet::Tone::Destructive});
    }
    facet::EndCard();
}

void nfa_token_card(app::AppState& state, AccountEdit& e, const core::Account& acc) {
    facet::BeginCard(e.kind == Kind::Cached ? "Saved session from this PC" : "NFA token");
    hint("This account has no password; the token is how it signs in.");
    gap(facet::T().spacing.sm);
    facet::BeginKeyValueList("##nfa-facts");
    facet::KeyValue("Login", acc.login.empty() ? "Unknown" : kit::login_text(state, acc).c_str());
    facet::KeyValue("Steam ID", acc.steam_id_64 != 0 ? std::to_string(acc.steam_id_64).c_str() : "Unknown");
    if (!acc.refresh_token.empty()) {
        facet::KeyValue("Expires", expiry_text(acc.refresh_token_expires, now_seconds()).c_str());
    }
    facet::EndKeyValueList();
    if (acc.refresh_token.empty()) {
        hint("No token stored. Paste one under Replace the token.");
    } else {
        std::string plain(acc.refresh_token.begin(), acc.refresh_token.end());
        token_row(state, full_login_token(acc, plain));
        crypto::zero_buffer(plain.data(), plain.size());
    }
    facet::EndCard();
}

// Replace the token and convert: they change how the account signs in, so they sit apart in the
// last card, closed until asked for, with quiet buttons (rule 3).
bool risky_card(app::AppState& state, AccountEdit& e, core::Account& acc) {
    const facet::Theme& t = facet::T();
    bool reloaded = false;
    facet::BeginCard("Change how it signs in");
    if (facet::BeginDisclosure("Replace the token", &e.replace_open)) {
        if (e.replace_text != e.replace_checked) {
            wipe(e.replace_checked);
            e.replace_checked = e.replace_text;
            e.replace_check = add_account_detail::check_jwt_token(e.replace_text);
            e.replace_error.clear();
        }
        const bool has_text = !add_account_detail::trim_path(e.replace_text).empty();
        const char* field_error = !e.replace_error.empty()           ? e.replace_error.c_str()
                                  : has_text && !e.replace_check.ok ? e.replace_check.error.c_str()
                                                                     : nullptr;
        facet::TextArea("##replace", &e.replace_text,
                        {.placeholder = "Paste the new login----token line",
                         .minLines = 3,
                         .maxLines = 5,
                         .error = field_error,
                         .helpText = has_text && e.replace_check.ok
                                         ? (e.replace_check.expired ? "This token has already expired."
                                                                    : "A valid token. The old one is overwritten.")
                                         : nullptr});
        ImGui::BeginDisabled(!(has_text && e.replace_check.ok));
        if (kit::quiet_destructive_button("Replace token", icons::token)) {
            const auto r = add_account_detail::import_jwt_token(state, e.replace_text, e.kind != Kind::Cached);
            if (!r.ok) {
                e.replace_error = r.error;
            } else {
                state.vault_dirty = true;
                state.save_vault_if_dirty();
                state.nfa_dead_notified.erase(r.account_id);
                state.pull_all_for_account(r.account_id);
                state.selected_account_id = r.account_id;
                if (const core::Account* fresh = state.find_account(r.account_id)) load(state, e, *fresh);
                kit::show_toast(state, "Token replaced", {.tone = widgets::ToastTone::Success});
                reloaded = true;
            }
        }
        ImGui::EndDisabled();
        facet::EndDisclosure();
    }
    if (!reloaded && e.kind == Kind::Cached) {
        gap(t.spacing.md);
        hint("Taken from this PC. Convert it to a full account to add a password and use it like any account you "
             "added.");
        gap(t.spacing.xs);
        if (facet::Button("Convert to full account")) e.convert_confirm = true;
        if (kit::confirm_dialog("Convert to a full account?", &e.convert_confirm,
                                "It leaves the Cached group and loses the Cached tag. Add its password under "
                                "Sign-in afterwards.",
                                "Convert account") == facet::DialogResult::Confirmed) {
            acc.is_nfa = false;
            auto& tids = acc.tag_ids;
            tids.erase(std::remove(tids.begin(), tids.end(), std::string(core::store::kCachedTagId)), tids.end());
            if (acc.group_id == core::store::kCachedGroupId) acc.group_id.clear();
            state.vault_dirty = true;
            state.save_vault_if_dirty();
            state.selected_account_id = acc.id;
            load(state, e, acc);
            reloaded = true;
        }
    }
    facet::EndCard();
    return reloaded;
}

}  // namespace

void draw_edit_notes_modal(app::AppState& state) {
    NotesEdit& s = notes_edit();
    if (state.notes_edit_requested) {
        state.notes_edit_requested = false;
        s.account_id = state.selected_account_id;
        s.notes.clear();
        if (const auto* acc = state.find_account(s.account_id)) s.notes = to_utf8(acc->notes);
        s.open = true;
    }
    if (s.open && state.selected_account_id != s.account_id) close_notes();
    if (!facet::BeginModal("Edit notes###edit-notes", &s.open)) return;
    const facet::Theme& t = facet::T();
    core::Account* acc = state.find_account(s.account_id);
    bool close = false;
    if (acc == nullptr) {
        facet::EmptyState("This account no longer exists", {.message = "It was removed while the dialog was open."});
        gap(t.spacing.lg);
        if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) close = true;
    } else {
        kit::account_row(state, *acc);
        gap(t.spacing.md);
        facet::TextArea("##notes", &s.notes,
                        {.placeholder = "Anything to remember about this account", .minLines = 5, .maxLines = 12});
        gap(t.spacing.lg);
        facet::BeginStack("##notes-footer", {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
        facet::StackSpacer();
        facet::BeginStack("##notes-actions", {.orientation = facet::Orientation::Horizontal});
        if (facet::Button("Cancel")) close = true;
        if (facet::Button("Save notes", {.variant = facet::ButtonVariant::Primary})) {
            acc->notes = to_wide(s.notes);
            state.vault_dirty = true;
            state.save_vault_if_dirty();
            close = true;
        }
        facet::EndStack();
        facet::EndStack();
    }
    facet::EndModal();
    if (close) close_notes();
}

#ifdef SAM_UI_CAPTURE
widgets::ExportBundleState& capture_export_state() {
    static widgets::ExportBundleState s;
    return s;
}
#endif

// Why a drawer of cards: the editor groups what belongs together (who it is, how it is filed,
// how it signs in) into cards, harmless first and the sign-in changes last, and the account list
// stays visible beside it.
void draw_edit_account_modal(app::AppState& state) {
#ifdef SAM_UI_CAPTURE
    widgets::draw_export_bundle_modal(state, capture_export_state());
#endif
    AccountEdit& e = account_edit();
    if (state.account_edit_requested) {
        state.account_edit_requested = false;
        if (const auto* acc = state.find_account(state.selected_account_id)) {
            load(state, e, *acc);
        } else {
            e.account_id = state.selected_account_id;
        }
        e.open = true;
    }
    if (e.open && state.selected_account_id != e.account_id) close_account_edit();

    // At most 45 percent of the window, so the list stays visible on a 900 wide window or at 1.5x.
    const float window_units = ImGui::GetMainViewport()->Size.x / facet::Px(1.0F);
    const float extent = std::clamp(window_units * 0.45F, 340.0F, kDrawerWidth);
    if (!facet::BeginDrawer("Edit account###edit-account", &e.open, {.extent = extent})) {
        if (!e.password.empty() || !e.shared_secret.empty() || !e.replace_text.empty()) e.clear_secrets();
        return;
    }
    const facet::Theme& t = facet::T();
    core::Account* acc = state.find_account(e.account_id);
    bool close = false;
    if (acc == nullptr) {
        facet::EmptyState("This account no longer exists", {.message = "It was removed while the editor was open."});
        gap(t.spacing.lg);
        if (facet::Button("Close", {.variant = facet::ButtonVariant::Primary})) close = true;
        facet::EndDrawer();
        if (close) close_account_edit();
        return;
    }

    const float footer_units = t.metrics.controlHeight + t.spacing.lg * 2.0F;
    const float body_units = std::max(t.metrics.controlHeight * 4.0F,
                                      ImGui::GetContentRegionAvail().y / facet::Px(1.0F) - footer_units);
    if (facet::BeginScrollArea("##edit-body", {.height = body_units})) {
        identity_card(state, e, *acc);
        gap(t.spacing.md);
        organisation_card(state, e);
        gap(t.spacing.md);
        if (e.kind == Kind::Password) {
            sign_in_card(e);
            gap(t.spacing.md);
            password_token_card(state, *acc);
        } else {
            nfa_token_card(state, e, *acc);
            gap(t.spacing.md);
            (void)risky_card(state, e, *acc);
        }
        gap(t.spacing.lg);
        facet::EndScrollArea();
    }

    gap(t.spacing.md);
    facet::BeginStack("##edit-footer", {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
    facet::BeginStack("##edit-actions", {.orientation = facet::Orientation::Horizontal});
    if (facet::Button("Cancel")) close = true;
    ImGui::BeginDisabled(e.kind == Kind::Password && e.login.empty());
    if (facet::Button("Save changes", {.variant = facet::ButtonVariant::Primary})) {
        if (core::Account* target = state.find_account(e.account_id)) save(state, e, *target);
        close = true;
    }
    ImGui::EndDisabled();
    facet::EndStack();
    facet::EndStack();
    facet::EndDrawer();
    if (close) close_account_edit();
}

SAM_KIT_TRANSIENT_RESET(edit_account, &close_editors)
SAM_KIT_SCREEN_LEAVE(edit_account_leave, app::Screen::Accounts, &close_editors)

#ifdef SAM_UI_CAPTURE
namespace {

void setup_edit(app::AppState& state, int index) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::Accounts;
    state.selected_account_id = app::ui_capture::demo_account_id(index);
    state.account_edit_requested = true;
}

void setup_edit_password(app::AppState& state) { setup_edit(state, 0); }
void setup_edit_mint(app::AppState& state) { setup_edit(state, 1); }
void setup_edit_nfa(app::AppState& state) {
    setup_edit(state, 4);
    account_edit().replace_open = true;
}

void setup_edit_notes(app::AppState& state) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::Accounts;
    state.selected_account_id = app::ui_capture::demo_account_id(3);
    if (core::Account* a = state.find_account(state.selected_account_id)) {
        a->notes = L"Main account for the weekend league. Trade lock ends after the next reset.";
    }
    state.notes_edit_requested = true;
}

void setup_bundle_export(app::AppState& state) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::Accounts;
    std::vector<std::string> ids;
    for (int i = 0; i < 8; ++i) ids.push_back(app::ui_capture::demo_account_id(i));
    widgets::request_export_bundle(capture_export_state(), std::move(ids));
    capture_export_state().passphrase = "placeholder pass";
    capture_export_state().confirm = "placeholder";
}

}  // namespace
SAM_UI_CAPTURE_SCENE(edit_account_password, "edit-account-password", &setup_edit_password, 30)
SAM_UI_CAPTURE_SCENE(edit_account_mint, "edit-account-mint", &setup_edit_mint, 30)
SAM_UI_CAPTURE_SCENE(edit_account_nfa, "edit-account-nfa", &setup_edit_nfa, 30)
SAM_UI_CAPTURE_SCENE(edit_notes, "edit-notes", &setup_edit_notes, 30)
SAM_UI_CAPTURE_SCENE(bundle_export, "bundle-export", &setup_bundle_export, 30)
#endif

}  // namespace sam::ui::screens
