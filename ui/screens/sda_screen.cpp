#include "ui/screens/sda_screen.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <span>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>

#include "core/sda/totp.hpp"
#include "core/time_aligner.hpp"
#include "platform/clipboard.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/transient.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/secrets.hpp"
#include "ui/screens/authenticator/dialogs.hpp"

namespace sam::ui::screens {

namespace {

namespace auth = authenticator;

constexpr float kListWidth = 280.0F;        // units; room for a persona name and its login
constexpr int kRevocationRevealSeconds = 10;
constexpr std::int64_t kSyncStaleAfterSeconds = 2 * 3600;

struct ScreenState {
    std::string query;
    bool detail_open = false;      // Compact width: the detail pane is shown instead of the list
    bool focus_search = false;     // put the keyboard focus into the search field this frame
    bool scroll_to_selection = false;  // bring the selected row into view (screen entry)
    int last_frame = -10;          // the frame the screen was last drawn; a gap means it was re-entered
    std::string last_picked_id;    // for settings.sda.auto_copy_on_select
    std::array<char, 32> rcode_input{};
    std::string rcode_input_owner;
};

ScreenState& ui_state() {
    static ScreenState s;
    return s;
}

// Unix seconds (system clock) until which a revocation code is shown, per account id.
std::unordered_map<std::string, std::int64_t>& rcode_reveal_until() {
    static std::unordered_map<std::string, std::int64_t> map;
    return map;
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

std::string lowercase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim_copy(const char* s) {
    std::string out(s);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) out.erase(out.begin());
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    return out;
}

// The accounts the list shows for `query`: those with an authenticator first, then the rest,
// each in vault order. The search matches the privacy-aware name and login only.
std::vector<core::Account*> visible_accounts(app::AppState& state, const std::string& query) {
    const std::string q = lowercase(trim_copy(query.c_str()));
    std::vector<core::Account*> out;
    out.reserve(state.vault.accounts.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (auto& a : state.vault.accounts) {
            if (a.sda.has_value() != (pass == 0)) continue;
            if (!q.empty()) {
                const std::string hay = lowercase(kit::display_name(state, a) + " " + kit::login_text(state, a));
                if (hay.find(q) == std::string::npos) continue;
            }
            out.push_back(&a);
        }
    }
    return out;
}

// Copies the current code of `a` the way the code widget does, and says so in a toast.
void copy_code(app::AppState& state, const core::Account& a) {
    if (!a.sda.has_value() || !time_aligner::synced()) return;
    const std::string code = sda::generate_code(a.sda->shared_secret, time_aligner::aligned_now());
    if (code.empty()) return;
    platform::clipboard::set_text_with_auto_clear(code, std::chrono::seconds(state.settings.clipboard_clear_seconds));
    kit::show_toast(state, "Copied the code of " + kit::display_name(state, a),
                    {.tone = widgets::ToastTone::Success, .id = "authenticator-copied", .seconds = 3});
}

void select_account(app::AppState& state, const core::Account& a) {
    state.selected_account_id = a.id;
    ui_state().detail_open = true;
}

// ---- List pane --------------------------------------------------------------------------------

// Why a search field over a virtual list: the job is "find one account and copy its code", so
// typing narrows the list, the arrows move the selection and Enter copies, all from the field.
void draw_list(app::AppState& state, const std::vector<core::Account*>& accounts, int current) {
    ScreenState& ui = ui_state();
    const facet::Theme& t = facet::T();

    if (ui.focus_search) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_search = false;
    }
    (void)facet::SearchField("##authenticator-search", &ui.query, {.placeholder = "Filter accounts"});
    const ImGuiID field_id = ImGui::GetItemID();
    int move = 0;
    if (ImGui::IsItemActive()) {
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, field_id);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, field_id);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, field_id)) move = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, field_id)) move = -1;
    }
    const bool enter = ImGui::IsItemDeactivated() &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        facet::ItemTooltip("Type to filter. Up and Down choose an account, Enter copies its code.");
    }

    const int count = static_cast<int>(accounts.size());
    if (count > 0) {
        if (move != 0) {
            const int next = current < 0 ? (move > 0 ? 0 : count - 1) : std::clamp(current + move, 0, count - 1);
            if (next != current) {
                state.selected_account_id = accounts[static_cast<std::size_t>(next)]->id;
                current = next;
                facet::SetNextVirtualListScrollTo(next);
            }
        }
        if (enter) {
            const core::Account* target = current >= 0 ? accounts[static_cast<std::size_t>(current)] : accounts.front();
            if (current < 0) state.selected_account_id = target->id;
            copy_code(state, *target);
            ui.focus_search = true;
        }
    }

    if (ui.scroll_to_selection) {
        if (current >= 0) facet::SetNextVirtualListScrollTo(current);
        ui.scroll_to_selection = false;
    }
    static ImGuiSelectionBasicStorage selection;
    selection.Clear();
    if (current >= 0) selection.SetItemSelected(selection.GetStorageIdFromIndex(current), true);

    if (facet::BeginVirtualList("##authenticator-accounts", count, &selection,
                                {.rowHeight = t.list.rowHeightLarge,
                                 .emptyText = ui.query.empty() ? "No accounts" : "No account matches the filter"})) {
        facet::RowRange rows;
        while (facet::NextVisibleRows(&rows)) {
            for (int i = rows.first; i < rows.last; ++i) {
                core::Account& a = *accounts[static_cast<std::size_t>(i)];
                ImGui::PushID(a.id.c_str());
                if (facet::VirtualListRow(i)) {
                    select_account(state, a);
                    copy_code(state, a);
                }
                const char* detail = nullptr;
                if (!a.sda.has_value()) detail = "No authenticator";
                else if (!a.sda->fully_enrolled) detail = "Activation not finished";
                kit::account_row(state, a, {.detail = detail});
                ImGui::PopID();
            }
        }
        facet::EndVirtualList();
    }

    // A click (or the list's own arrow keys) changed the selection: take the row that is new.
    int picked = -1;
    void* it = nullptr;
    ImGuiID id = 0;
    while (selection.GetNextSelectedItem(&it, &id)) {
        const int index = static_cast<int>(id);
        if (index != current && index >= 0 && index < count) picked = index;
    }
    if (picked >= 0) select_account(state, *accounts[static_cast<std::size_t>(picked)]);
}

// ---- Detail pane ------------------------------------------------------------------------------

void open_mafile_import(app::AppState& state) {
    state.pending_mafile_focus = true;
    kit::go_to(state, app::Screen::AddAccount);
}

// The card header line: the account, compact. The same
// line opens the card whether the account has an authenticator or not, so the identity has one
// place in every state.
void card_header(app::AppState& state, const core::Account& a) {
    kit::account_row(state, a, {.layout = kit::AccountRowOptions::Layout::OneLine});
}

// Why the hero card: the code is what every visit comes for, so it has a card of its own with
// the account it belongs to on its header line and nothing else competing for the space.
void draw_hero(app::AppState& state, const core::Account& a) {
    ScreenState& ui = ui_state();
    const facet::Theme& t = facet::T();
    facet::BeginCard("##code-hero");
    card_header(state, a);
    gap(t.spacing.lg);
    const kit::AuthenticatorCodeResult result =
        kit::authenticator_code(state, a, {.hero = true, .hero_code_size = 48.0F});
    if (result.unreadable) {
        if (facet::InlineAlert("This authenticator's secret is damaged", nullptr,
                               "The secret saved for it cannot make codes. Import the account's maFile again "
                               "to repair it.",
                               {.tone = facet::Tone::Destructive, .actionLabel = "Import maFile"}) ==
            facet::InlineAlertAction::Action) {
            open_mafile_import(state);
        }
        if (facet::BeginDisclosure("Details", nullptr, {.style = facet::DisclosureStyle::Link})) {
            facet::StyledText("The shared secret in the vault is empty or is not valid base64.",
                              {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
            facet::EndDisclosure();
        }
    }
    if (result.copied) {
        kit::show_toast(state, "Copied the code of " + kit::display_name(state, a),
                        {.tone = widgets::ToastTone::Success, .id = "authenticator-copied", .seconds = 3});
    }
    if (result.synced) {
        if (state.settings.sda.auto_copy_on_select && ui.last_picked_id != a.id) {
            const std::string code = sda::generate_code(a.sda->shared_secret, time_aligner::aligned_now());
            if (!code.empty()) {
                platform::clipboard::set_text_with_auto_clear(
                    code, std::chrono::seconds(state.settings.clipboard_clear_seconds));
            }
        }
        ui.last_picked_id = a.id;
    }
    facet::EndCard();
}

// Notices that change how far the code can be trusted. They sit under the hero so the code never
// moves when one appears.
void draw_notices(app::AppState& state, const core::Account& a) {
    const facet::Theme& t = facet::T();
    const bool synced = time_aligner::synced();
    const std::int64_t stale = time_aligner::seconds_since_last_success();
    if (!synced || stale >= kSyncStaleAfterSeconds) {
        char message[160];
        const char* title = "Waiting for the Steam time sync";
        if (!synced) {
            std::snprintf(message, sizeof(message), "Codes show once this PC's clock is matched to Steam's.");
        } else {
            title = "Time sync is out of date";
            const long long mins = static_cast<long long>(stale / 60);
            std::snprintf(message, sizeof(message),
                          mins >= 60 ? "Last matched to Steam's clock %lld h ago. Codes can be off if this PC's "
                                       "clock drifted."
                                     : "Last matched to Steam's clock %lld min ago. Codes can be off if this PC's "
                                       "clock drifted.",
                          mins >= 60 ? mins / 60 : mins);
        }
        gap(t.spacing.md);
        if (facet::InlineAlert(title, nullptr, message,
                               {.tone = facet::Tone::Warning, .actionLabel = "Sync with Steam now"}) ==
            facet::InlineAlertAction::Action) {
            time_aligner::sync_now_async();
        }
    }
    if (!a.sda->fully_enrolled) {
        gap(t.spacing.md);
        if (facet::InlineAlert("Linking is not finished", nullptr,
                               "The revocation code is saved, but the activation code was never confirmed, so "
                               "Steam may not accept these codes yet.",
                               {.tone = facet::Tone::Warning, .actionLabel = "Enter activation code"}) ==
            facet::InlineAlertAction::Action) {
            auth::request_link(state, a.id);
        }
    }
}

// One action of the row: a label and how to draw it.
struct RowButton {
    const char* label;
    facet::Icon icon{};
    bool destructive = false;  // kit::quiet_destructive_button
};

// One row of the authenticator list: icon, title, one line of description (or a value), and
// small trailing buttons that fit their labels, right-aligned. Returns the index of the button
// activated this frame, or -1.
int action_row(const char* id, facet::Icon icon, const char* title, const char* description,
               std::span<const RowButton> buttons, bool value_line = false, bool destructive = false) {
    const facet::Theme& t = facet::T();
    ImGui::PushID(id);
    const float scale = facet::Scale();
    const float pad = facet::Px(t.spacing.sm);
    const ImVec2 row_origin = ImGui::GetCursorScreenPos();
    const ImVec2 top_left(row_origin.x, row_origin.y + pad);
    const float avail_px = ImGui::GetContentRegionAvail().x;
    const float text_h = facet::MeasureText(facet::TextStyle::Body, "Ag").y +
                         facet::MeasureText(facet::TextStyle::Footnote, "Ag").y;
    const float icon_px = facet::Px(16.0F);
    const float button_h = facet::Px(t.metrics.controlHeightSmall);

    // The width of the button group, measured on the last frame (the first frame estimates it).
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID width_key = ImGui::GetID("##buttons-width");
    float buttons_px = storage->GetFloat(width_key, facet::Px(88.0F) * static_cast<float>(buttons.size()));

    const float text_x = top_left.x + icon_px + facet::Px(t.spacing.md);
    const float text_units =
        std::max(64.0F, (top_left.x + avail_px - buttons_px - facet::Px(t.spacing.lg) - text_x) / scale);

    ImGui::SetCursorScreenPos(ImVec2(top_left.x, top_left.y + std::floor((text_h - icon_px) * 0.5F)));
    facet::IconGlyph(icon, icon_px, ImGui::GetColorU32(destructive ? t.colors.destructive : t.colors.labelSecondary));
    ImGui::SetCursorScreenPos(ImVec2(text_x, top_left.y));
    facet::BeginStack("##text", {.gap = facet::Gap::None, .width = text_units});
    facet::StyledText(title, {.tone = destructive ? facet::Tone::Destructive : facet::Tone::Neutral});
    if (value_line) {
        facet::TruncatedText(description, {.style = facet::TextStyle::Footnote, .width = text_units});
    } else {
        facet::TruncatedText(description, {.style = facet::TextStyle::Footnote, .secondary = true, .width = text_units});
    }
    facet::EndStack();

    int pressed = -1;
    const float buttons_x = top_left.x + avail_px - buttons_px;
    ImGui::SetCursorScreenPos(ImVec2(buttons_x, top_left.y + std::floor((text_h - button_h) * 0.5F)));
    float measured_left = 0.0F;
    float measured_right = 0.0F;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (i > 0) ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
        const RowButton& b = buttons[i];
        const bool hit = b.destructive
                             ? kit::quiet_destructive_button(b.label, b.icon.IsValid() ? b.icon : facet::Icon("trash"),
                                                             facet::ControlSize::Small)
                             : facet::Button(b.label, {.icon = b.icon, .size = facet::ControlSize::Small});
        if (hit) pressed = static_cast<int>(i);
        if (i == 0) measured_left = ImGui::GetItemRectMin().x;
        measured_right = ImGui::GetItemRectMax().x;
    }
    if (!buttons.empty()) storage->SetFloat(width_key, measured_right - measured_left);

    // Close the row with an item of its full height, so the card grows to include it.
    ImGui::SetCursorScreenPos(row_origin);
    ImGui::Dummy(ImVec2(avail_px, std::max(text_h, button_h) + 2.0F * pad));
    ImGui::PopID();
    return pressed;
}

// The revocation code as the first row of the list: what gets the account back, hidden until
// asked for, with its purpose in one line.
void revocation_row(app::AppState& state, core::Account& a) {
    ScreenState& ui = ui_state();
    const facet::Theme& t = facet::T();
    const std::string& code = a.sda->revocation_code;
    if (!code.empty()) {
        const std::int64_t now = unix_now();
        std::int64_t& until = rcode_reveal_until()[a.id];
        const bool revealed = until > now;
        const RowButton hidden_buttons[] = {{"Show code", facet::Icon("eye")}, {"Copy", facet::Icon("copy")}};
        const RowButton shown_buttons[] = {{"Hide", facet::Icon("eye-off")}, {"Copy", facet::Icon("copy")}};
        const int hit = action_row("revocation", icons::token, "Revocation code",
                                   revealed ? code.c_str()
                                            : "Gets the account back if this app is lost. Keep a copy outside it.",
                                   revealed ? std::span<const RowButton>(shown_buttons)
                                            : std::span<const RowButton>(hidden_buttons),
                                   revealed);
        if (hit == 0) until = revealed ? 0 : now + kRevocationRevealSeconds;
        if (hit == 1) {
            platform::clipboard::set_text(code);
            kit::show_toast(state, "Copied the revocation code",
                            {.tone = widgets::ToastTone::Success, .id = "authenticator-rcode-copied", .seconds = 3});
        }
        return;
    }

    if (ui.rcode_input_owner != a.id) {
        ui.rcode_input_owner = a.id;
        ui.rcode_input.fill(0);
    }
    (void)action_row("revocation", icons::token, "Revocation code",
                     "Not saved. Add the code you wrote down when linking, or removal is not possible.", {}, false);
    const float indent = 16.0F + t.spacing.md;
    ImGui::Indent(facet::Px(indent));
    (void)facet::TextField("##revocation-input", ui.rcode_input.data(), ui.rcode_input.size(),
                           {.placeholder = "R12345", .width = 160.0F});
    ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
    const std::string trimmed = trim_copy(ui.rcode_input.data());
    ImGui::BeginDisabled(trimmed.empty());
    if (facet::Button("Save code")) {
        a.sda->revocation_code = trimmed;
        state.vault_dirty = true;
        state.save_vault_if_dirty();
        ui.rcode_input.fill(0);
    }
    ImGui::EndDisabled();
    facet::ItemTooltip("Saves it in the vault. Steam cannot regenerate this code.");
    ImGui::Unindent(facet::Px(indent));
    gap(t.spacing.sm);
}

// Why one card with a compact list: the revocation code and the occasional tasks read as one
// list of what can be done, each with a line on what it does; removal ends it behind a divider.
void draw_manage(app::AppState& state, core::Account& a) {
    facet::BeginCard("Manage");
    revocation_row(state, a);
    facet::Separator();
    static const RowButton kShowLink[] = {{"Show link"}};
    if (action_row("backup", facet::Icon("download"), "Back up", "A setup link for another authenticator app",
                   kShowLink) == 0) {
        auth::request_backup(a.id);
    }
    facet::Separator();
    static const RowButton kExport[] = {{"Export"}};
    if (action_row("export", facet::Icon("file"), "Export maFiles",
                   "Encrypted files for SDA or another copy of this app", kExport) == 0) {
        auth::request_export(a.id);
    }
    facet::Separator();
    static const RowButton kShow[] = {{"Show"}};
    if (action_row("secrets", facet::Icon("lock"), "Secrets", "Shared and identity secret, device ID, serial",
                   kShow) == 0) {
        auth::request_secrets(a.id);
    }
    facet::Separator();
    static const RowButton kCheck[] = {{"Check"}};
    if (action_row("verify", facet::Icon("refresh"), "Check with Steam",
                   "Confirms Steam has this authenticator on record", kCheck) == 0) {
        auth::request_verify(state, a.id);
    }
    facet::Separator({.opaque = true});
    static const RowButton kRemove[] = {{"Remove", facet::Icon("trash"), true}};
    if (action_row("remove", facet::Icon("trash"), "Remove authenticator",
                   "Email codes or no Steam Guard; trades held for 15 days", kRemove, false, true) == 0) {
        auth::request_remove(a.id);
    }
    facet::EndCard();
}

// One centered row of buttons under an empty state.
void begin_centered_row(const char* id) {
    facet::BeginStack(id, {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
}

void end_centered_row() {
    facet::StackSpacer();
    facet::EndStack();
}

// An account without an authenticator: the same card as the hero, with the account on its
// header line, holding what linking does and the two ways to get one.
void draw_unlinked(app::AppState& state, const core::Account& a) {
    const facet::Theme& t = facet::T();
    facet::BeginCard("##unlinked");
    card_header(state, a);
    gap(t.spacing.xl);
    (void)facet::EmptyState("No authenticator on this account",
                            {.message = "Linking makes this app the account's Steam Guard: it generates the sign-in "
                                        "codes and approves trades and market listings.",
                             .icon = icons::authenticator});
    gap(t.spacing.md);
    const bool has_session = !a.access_token.empty();
    begin_centered_row("##unlinked-actions");
    ImGui::BeginDisabled(!has_session);
    if (facet::Button("Link authenticator", {.variant = facet::ButtonVariant::Primary})) auth::request_link(state, a.id);
    ImGui::EndDisabled();
    if (facet::Button("Import maFile")) open_mafile_import(state);
    end_centered_row();
    if (!has_session) {
        gap(t.spacing.sm);
        facet::StyledText("Linking needs a signed-in session. Sign in with the full login on Add account first.",
                          {.style = facet::TextStyle::Footnote,
                           .secondary = true,
                           .wrap = true,
                           .align = facet::Align::Center,
                           .width = facet::AvailableWidthUnits()});
    }
    gap(t.spacing.lg);
    facet::EndCard();
}

void draw_no_selection(app::AppState& state, int linked) {
    gap(facet::T().spacing.xxxl);
    if (linked == 0) {
        if (kit::empty_state("No account has an authenticator yet",
                             {.message = "Choose an account on the left to link one, or import an maFile you "
                                         "already have.",
                              .icon = icons::authenticator,
                              .action_label = "Import maFile"})) {
            open_mafile_import(state);
        }
        return;
    }
    (void)kit::empty_state("Choose an account",
                           {.message = "Its current Steam Guard code shows here. Type to filter the list; Enter "
                                       "copies the code of the chosen account.",
                            .icon = icons::authenticator});
}

// Why this layout: the hero spans the pane, the notices sit under it, and one card with the
// revocation code and the tasks follows, so it starts above the fold even at 900x600.
void draw_detail(app::AppState& state, core::Account* a, int linked) {
    if (a == nullptr) {
        draw_no_selection(state, linked);
        return;
    }
    const facet::Theme& t = facet::T();
    ImGui::PushID(a->id.c_str());
    if (!a->sda.has_value()) {
        draw_unlinked(state, *a);
    } else {
        draw_hero(state, *a);
        draw_notices(state, *a);
        gap(t.spacing.sm);
        draw_manage(state, *a);
        gap(t.spacing.lg);
    }
    ImGui::PopID();
}

// Closes the dialogs and forgets what belongs to an account: the revealed revocation codes and
// a half-typed revocation code. Runs on lock, vault change, leaving the screen and before every
// capture scene.
void reset_transient(app::AppState& state) {
    auth::reset_dialogs(state);
    rcode_reveal_until().clear();
    ScreenState& ui = ui_state();
    ui.rcode_input.fill(0);
    ui.rcode_input_owner.clear();
    ui.focus_search = false;
    ui.scroll_to_selection = true;  // the next visit shows the selected row
}

void draw_dialogs(app::AppState& state) {
    auth::draw_link_dialog(state);
    auth::draw_remove_dialog(state);
    auth::draw_backup_dialog(state);
    auth::draw_export_dialog(state);
    auth::draw_secrets_dialog(state);
}

}  // namespace

void reset_authenticator_transient(app::AppState& state) {
    reset_transient(state);
}

// Why list and detail: finding the account is the first half of every visit and the code the
// second, so both stay on screen and the list never hides the code.
void draw_sda(app::AppState& state) {
    ScreenState& ui = ui_state();
    const int frame = ImGui::GetFrameCount();
    if (ui.last_frame < frame - 1) ui.scroll_to_selection = true;
    ui.last_frame = frame;

    // Type to filter: printable keys typed while nothing has the keyboard go into the filter
    // field, which then takes the focus. Nothing is focused on entry, so the field looks the same
    // in every capture and on every visit.
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsAnyItemActive() && ImGui::GetTopMostPopupModal() == nullptr && !io.KeyCtrl && !io.KeyAlt &&
        !io.InputQueueCharacters.empty()) {
        for (const ImWchar c : io.InputQueueCharacters) {
            if (c < 0x20 || c == 0x7F) continue;
            char utf8[5] = {};
            ImTextCharToUtf8(utf8, c);
            ui.query += utf8;
            ui.focus_search = true;
        }
    }

    // Dialog state follows the selection: a dialog opened for another account closes.
    auth::close_dialogs_not_for(state.selected_account_id);

    kit::PageScope page("authenticator", {.scroll = false});
    int linked = 0;
    for (const auto& a : state.vault.accounts) linked += a.sda.has_value() ? 1 : 0;
    const int total = static_cast<int>(state.vault.accounts.size());
    char context[96];
    if (total == 0) {
        std::snprintf(context, sizeof(context), "Steam Guard codes and authenticators");
    } else {
        std::snprintf(context, sizeof(context), total == 1 ? "%d of %d account has an authenticator"
                                                           : "%d of %d accounts have an authenticator",
                      linked, total);
    }
    { kit::PageHeaderScope header("Authenticator", {.context = context}); }

    if (total == 0) {
        gap(facet::T().spacing.xxxl);
        if (kit::empty_state("No accounts in this vault",
                             {.message = "Add an account first. Its authenticator is managed here once it is linked "
                                         "or imported.",
                              .icon = icons::authenticator,
                              .action_label = "Add account",
                              .action_icon = facet::Icon("plus")})) {
            kit::go_to(state, app::Screen::AddAccount);
        }
        draw_dialogs(state);
        return;
    }

    const std::vector<core::Account*> accounts = visible_accounts(state, ui.query);
    int current = -1;
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        if (accounts[i]->id == state.selected_account_id) current = static_cast<int>(i);
    }

    {
        facet::ListDetailScope view("##authenticator-view", &ui.detail_open, {.listWidth = kListWidth});
        if (facet::ListPaneScope list; list) draw_list(state, accounts, current);
        if (facet::DetailPaneScope detail; detail) {
            draw_detail(state, state.find_account(state.selected_account_id), linked);
        }
    }
    draw_dialogs(state);
}

}  // namespace sam::ui::screens

SAM_KIT_TRANSIENT_RESET(authenticator_screen, &sam::ui::screens::reset_authenticator_transient)
SAM_KIT_SCREEN_LEAVE(authenticator_screen, sam::app::Screen::Authenticator,
                     &sam::ui::screens::reset_authenticator_transient)

#ifdef SAM_UI_CAPTURE
namespace sam::ui::screens::authenticator {

void capture_reveal_revocation(const std::string& account_id) {
    rcode_reveal_until()[account_id] = unix_now() + 3600;
}

}  // namespace sam::ui::screens::authenticator
#endif
