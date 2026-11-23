#include "ui/screens/unlock/locked_card.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/image.h>
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
#include <facet/widgets/media.h>
#include <facet/widgets/overlays.h>

#include "app/app_paths.hpp"
#include "app/vault_registry.hpp"
#include "core/account_store/store.hpp"
#include "platform/file_dialog.hpp"
#include "platform/paths.hpp"
#include "ui/icons.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/secrets.hpp"
#include "ui/kit/standing.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/unlock/session.hpp"
#include "ui/screens/unlock_screen.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::unlock {

namespace {

constexpr float kCardUnits = 420.0F;
constexpr float kAppIconUnits = 64.0F;
constexpr float kVaultRowUnits = 48.0F;
constexpr float kVaultMarkUnits = 32.0F;
constexpr int kVaultListMaxRows = 4;  // more vaults scroll inside the list
constexpr int kScrollTopFrames = 6;   // frames the card holds its scroll at the top after arriving
constexpr float kFolderDialogUnits = 520.0F;
constexpr int kVaultNameMax = 64;
constexpr std::uint32_t kNewVaultColor = 0x4F8EF7FFu;  // the colour a new vault starts with
// Frames between the click and the work: the next frame draws the loading state completely and
// is presented, the work runs at the start of the one after it.
constexpr int kWorkDelayFrames = 2;
// Frames the card waits for the password field to become active before it gives up selecting.
constexpr int kSelectWaitFrames = 8;

enum class Mode {
    Unlock,       // the active vault exists: ask for its master password
    CreateFirst,  // the active vault has no file yet (first run, or a missing file): set it up
    CreateNew,    // "Create a new vault": a second vault next to the existing ones
};

enum class Work { None, Unlock, Remembered, CreateFirst, CreateNew };

struct CardState {
    bool initialized = false;
    std::string shown_for_vault;
    Mode mode = Mode::Unlock;

    std::string password;
    std::string confirm;
    std::string name;

    std::string field_error;  // the password did not open the vault
    std::string alert;        // the vault could not be opened or created for another reason
    std::string cache_note;   // why the remembered password was not used
    bool remembered = false;  // the active vault can open from its remembered password

    bool focus_pending = false;
    bool select_pending = false;
    int select_wait = 0;

    Work work = Work::None;
    int work_frames = 0;
    bool hold_work = false;  // capture only: stay in the loading state

    int last_frame = -100;
    float block_height = 0.0F;   // card and links, measured last frame
    // The block height the card is centred on: measured only while no error or hint is showing,
    // so an error line grows the card downwards and the field the eye is on stays put.
    float anchor_height = 0.0F;
    bool transient = false;      // this frame shows an error or hint that is not always there
    int scroll_top_frames = 0;
    int error_in_frames = 0;  // capture only: shows the wrong-password error after this many frames
    bool layout_ready = false;  // the card was measured once in this context
    ImGuiSelectionBasicStorage vault_selection;

    bool folder_open = false;
    std::string folder_path;
    std::string folder_checked;   // the path folder_problem was computed for
    const char* folder_problem = nullptr;
    bool folder_valid = false;
    std::string folder_error;     // set_custom_data_dir failed
    float folder_actions_width = 0.0F;
};

CardState g;

void clear_secrets() {
    g.password.clear();
    g.confirm.clear();
}

const app::VaultInfo* active_info(const app::AppState& state) {
    return state.vault_registry.find(platform::active_vault_id());
}

void reset_for_vault(const app::AppState& state) {
    const std::string active = platform::active_vault_id();
    g.initialized = true;
    g.shown_for_vault = active;
    g.mode = core::store::vault_exists(app::vault_path()) ? Mode::Unlock : Mode::CreateFirst;
    clear_secrets();
    g.name = "My vault";
    g.field_error.clear();
    g.alert.clear();
    g.cache_note.clear();
    g.remembered = g.mode == Mode::Unlock && remembered_password_available(state);
    g.focus_pending = true;
    g.select_pending = false;
    g.work = Work::None;
}

bool enter_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

// Enter on the field drawn last: the field ends its edit on Enter, or held keyboard focus.
bool submitted_from_last_field() {
    return (ImGui::IsItemDeactivated() || ImGui::IsItemFocused()) && enter_pressed();
}

std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

bool busy() {
    return g.work != Work::None;
}

// Whether the pending focus goes to the field drawn next. It waits until the card was measured
// once: on the very first frame the card sits at a guessed position, and focusing there would
// scroll a tall card away from its top.
bool focus_now() {
    if (!g.focus_pending || busy() || !g.layout_ready) return false;
    ImGui::SetKeyboardFocusHere();
    g.focus_pending = false;
    return true;
}

void start(Work work) {
    g.work = work;
    g.work_frames = kWorkDelayFrames;
    g.field_error.clear();
    g.alert.clear();
}

// The field error of a wrong password; the field takes the focus back with its text selected,
// so typing replaces it.
void set_wrong_password() {
    g.field_error = "Wrong master password.";
    g.focus_pending = true;
    g.select_pending = true;
    g.select_wait = kSelectWaitFrames;
}

void finish_open() {
    clear_secrets();
    g.cache_note.clear();
}

void run_work(app::AppState& state) {
    if (g.work == Work::None || g.hold_work) return;
    if (--g.work_frames > 0) return;
    const Work work = g.work;
    g.work = Work::None;
    switch (work) {
        case Work::Unlock: {
            const UnlockResult r = unlock_with_password(state, g.password);
            switch (r.outcome) {
                case UnlockOutcome::Opened: finish_open(); break;
                case UnlockOutcome::WrongPassword: set_wrong_password(); break;
                case UnlockOutcome::Corrupt:
                    g.alert = "The vault file is damaged and cannot be opened (" + r.message + ").";
                    break;
                case UnlockOutcome::Failed: g.alert = r.message; break;
            }
            break;
        }
        case Work::Remembered: {
            std::string why;
            if (unlock_with_remembered_password(state, &why)) {
                finish_open();
            } else {
                g.remembered = false;
                g.cache_note = why.empty() ? "The remembered master password is no longer on this PC. Enter it to unlock."
                                           : why;
                g.focus_pending = true;
            }
            break;
        }
        case Work::CreateFirst: {
            std::string err;
            if (create_active_vault(state, trimmed(g.name), g.password, err)) {
                finish_open();
            } else {
                g.alert = err;
            }
            break;
        }
        case Work::CreateNew: {
            std::string err;
            if (create_and_open_vault(state, trimmed(g.name), kNewVaultColor, g.password, err)) {
                finish_open();
            } else {
                g.alert = err;
            }
            break;
        }
        case Work::None: break;
    }
}

// The vaults in display order: registry order, then name.
std::vector<const app::VaultInfo*> sorted_vaults(const app::AppState& state) {
    std::vector<const app::VaultInfo*> items;
    items.reserve(state.vault_registry.vaults.size());
    for (const auto& v : state.vault_registry.vaults) items.push_back(&v);
    std::sort(items.begin(), items.end(), [](const app::VaultInfo* a, const app::VaultInfo* b) {
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });
    return items;
}

void field_label(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Callout});
}

// A vault's custom picture, decoded off the frame thread by Facet once per file version (the
// key carries the write time, so a picture changed in Settings shows here after a lock).
facet::ImageHandle vault_picture(const app::VaultInfo& v) {
    static std::unordered_map<std::string, facet::ImageHandle> cache;
    if (v.icon_file.empty()) return {};
    const std::filesystem::path file = app::vault_dir_for(v.id) / std::filesystem::path(v.icon_file);
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(file, ec);
    if (ec) return {};
    const std::string path = to_utf8(file.wstring());
    const std::string key = path + "|" + std::to_string(stamp.time_since_epoch().count());
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, facet::LoadImageFileAsync(path.c_str())).first;
    return it->second;
}

// The vault's own mark, as the sidebar's vault switcher draws it: its picture when it has one,
// else its initials, ringed in the vault's color (user data, pushed as the accent).
void vault_mark(const app::VaultInfo& v, float diameter_units) {
    // Facet takes the initials from the words of the label: keep letters and digits only, so
    // "Work (demo)" gives "WD" and not "W(".
    std::string words;
    for (const char c : v.name) {
        const auto u = static_cast<unsigned char>(c);
        words += (std::isalnum(u) != 0 || u >= 0x80) ? c : ' ';
    }
    facet::PushAccent(kit::data_color(v.color_rgba));
    facet::Avatar((words + "###vault-mark").c_str(), vault_picture(v),
                  {.selected = true, .diameter = diameter_units});
    facet::PopAccent();
}

// Why a list and not a dropdown: with several vaults the user should see them all at a glance,
// each with its own mark, and pick one with the same selection pill as everywhere else; the
// password field sits right under it, so picking and unlocking stay one step.
void draw_vault_list(app::AppState& state) {
    const auto vaults = sorted_vaults(state);
    const int count = static_cast<int>(vaults.size());
    const std::string active = platform::active_vault_id();
    int active_index = -1;
    for (int i = 0; i < count; ++i) {
        if (vaults[static_cast<std::size_t>(i)]->id == active) active_index = i;
    }
    // One vault is selected: the active one. A click, an arrow key or Ctrl+click writes the
    // storage, and the newly selected row becomes the active vault below.
    g.vault_selection.Clear();
    if (active_index >= 0) g.vault_selection.SetItemSelected(static_cast<ImGuiID>(active_index), true);

    int picked = -1;
    bool activated = false;
    const float height = kVaultRowUnits * static_cast<float>(std::min(count, kVaultListMaxRows));
    if (facet::BeginVirtualList("##vaults", count, &g.vault_selection, {.rowHeight = kVaultRowUnits, .height = height})) {
        facet::RowRange rows;
        while (facet::NextVisibleRows(&rows)) {
            for (int i = rows.first; i < rows.last; ++i) {
                const app::VaultInfo& v = *vaults[static_cast<std::size_t>(i)];
                if (facet::VirtualListRow(i)) {
                    picked = i;
                    activated = true;
                }
                ImGui::PushID(i);
                {
                    kit::RowCell cell({.gap = facet::Gap::Loose});
                    vault_mark(v, kVaultMarkUnits);
                    cell.next();
                    if (v.last_opened_unix > 0) {
                        const std::string when = "Opened " + format_relative(v.last_opened_unix);
                        facet::BeginStack("##vault-text", {.gap = facet::Gap::None});
                        facet::StyledText(v.name.c_str(), {.style = facet::TextStyle::Body});
                        facet::StyledText(when.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
                        facet::EndStack();
                    } else {
                        facet::StyledText(v.name.c_str(), {.style = facet::TextStyle::Body});
                    }
                }
                ImGui::PopID();
            }
        }
        facet::EndVirtualList();
    }
    for (int i = 0; i < count && picked < 0; ++i) {
        if (i != active_index && g.vault_selection.Contains(static_cast<ImGuiID>(i))) picked = i;
    }
    if (picked >= 0 && picked != active_index) {
        platform::set_active_vault_id(vaults[static_cast<std::size_t>(picked)]->id);
        reset_for_vault(state);
        g.focus_pending = activated;  // Enter or a double click goes on to the password
    } else if (activated) {
        g.focus_pending = true;
    }
}

void draw_header(const char* title, const char* subtitle) {
    const facet::Theme& t = facet::T();
    const float inner = ImGui::GetContentRegionAvail().x;
    const float size = facet::Px(kAppIconUnits);
    if (auto* icon = reinterpret_cast<ID3D11ShaderResourceView*>(icons::app_icon_for(size))) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((inner - size) * 0.5F));
        facet::Image(icon, ImVec2(size, size), {.showStates = false});
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
    }
    const float inner_units = inner / facet::Scale();
    facet::StyledText(title, {.style = facet::TextStyle::Title1, .align = facet::Align::Center, .width = inner_units});
    if (subtitle != nullptr) {
        facet::StyledText(subtitle, {.style = facet::TextStyle::Callout,
                                     .secondary = true,
                                     .wrap = true,
                                     .align = facet::Align::Center,
                                     .width = inner_units});
    }
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.md)));
}

// The strength meter shows once typing starts; until then its height stays reserved, so the
// fields under it do not move on the first key.
// Returns true when Enter was pressed in the field.
bool master_password_with_meter(const char* label, std::string* password) {
    const bool typed = !password->empty();
    if (kit::master_password_field(label, password, {.show_strength = typed})) g.alert.clear();
    const bool submitted = submitted_from_last_field();
    if (!typed) {
        const facet::Theme& t = facet::T();
        const float meter = std::round(facet::Px(t.spacing.xs)) + facet::PxSnapped(t.metrics.progressHeightSmall);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
        ImGui::Dummy(ImVec2(1.0F, meter));
    }
    return submitted;
}

bool caps_lock_on() {
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

void draw_alert() {
    if (g.alert.empty()) return;
    facet::InlineAlert("##vault-alert", nullptr, g.alert.c_str(), {.tone = facet::Tone::Destructive});
}

// Why this order: what is being unlocked, then the one field that matters, then the action
// under it where the hand goes; notes about the remembered password sit with the field they
// explain.
void draw_unlock_form(app::AppState& state) {
    const bool several = state.vault_registry.vaults.size() > 1;
    const app::VaultInfo* info = active_info(state);
    const char* name = info != nullptr && !info->name.empty() ? info->name.c_str() : "Your vault";
    draw_header(several ? "Unlock a vault" : name,
                several ? nullptr : "Enter the master password to open your accounts.");
    g.transient = !g.field_error.empty() || !g.alert.empty() || caps_lock_on();

    ImGui::BeginDisabled(busy());
    if (several) {
        draw_vault_list(state);
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    }

    field_label("Master password");
    focus_now();
    const char* error = g.field_error.empty() ? nullptr : g.field_error.c_str();
    if (kit::master_password_field("##master-password", &g.password,
                                   {.placeholder = g.remembered ? "Remembered on this PC" : nullptr,
                                    .error = error})) {
        g.field_error.clear();
        g.alert.clear();
    }
    bool submit = submitted_from_last_field();
    if (g.select_pending) {
        if (ImGui::IsItemActive()) {
            if (ImGuiInputTextState* s = ImGui::GetInputTextState(ImGui::GetItemID())) s->ReloadUserBufAndSelectAll();
            g.select_pending = false;
        } else if (--g.select_wait <= 0) {
            g.select_pending = false;
        }
    }
    ImGui::EndDisabled();

    if (!g.cache_note.empty()) {
        facet::InlineAlert("##remembered-note", nullptr, g.cache_note.c_str(), {.tone = facet::Tone::Warning});
    } else if (g.remembered) {
        facet::StyledText("Leave the field empty to use the remembered password.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    } else if (state.settings.remember_master_password) {
        facet::StyledText("No remembered password for this vault on this PC. Unlocking remembers it.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    }
    draw_alert();

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    const bool can_submit = !g.password.empty() || g.remembered;
    const bool loading = g.work == Work::Unlock || g.work == Work::Remembered;
    ImGui::BeginDisabled(!can_submit && !loading);
    if (facet::Button("Unlock vault", {.variant = facet::ButtonVariant::Primary, .width = facet::kFill, .loading = loading})) {
        submit = true;
    }
    ImGui::EndDisabled();
    if (submit && can_submit && !busy()) start(g.password.empty() ? Work::Remembered : Work::Unlock);
}

// Why one card for creation: a name, the password twice and the one warning that matters fit
// in a single honest step; a tour would only add clicks before the user has any accounts.
void draw_create_form(app::AppState& state) {
    const bool first = g.mode == Mode::CreateFirst;
    const app::VaultInfo* info = active_info(state);
    const bool ask_name = !first || info == nullptr;
    const bool several = first && state.vault_registry.vaults.size() > 1;
    const char* title = !first ? "Create a new vault" : info != nullptr ? info->name.c_str() : "Create your vault";
    const char* subtitle = first && info != nullptr ? "This vault has no data file yet. Choose its master password."
                                                    : "Your Steam accounts and authenticators, encrypted on this PC.";
    draw_header(title, subtitle);
    const bool name_missing = ask_name && trimmed(g.name).empty();
    const bool mismatch = !g.confirm.empty() && g.confirm != g.password;
    g.transient = name_missing || mismatch || !g.alert.empty() || caps_lock_on();

    ImGui::BeginDisabled(busy());
    if (several) {
        draw_vault_list(state);
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    }
    bool submit = false;
    if (ask_name) {
        field_label("Vault name");
        focus_now();
        facet::TextField("##vault-name", &g.name,
                         {.maxLength = kVaultNameMax, .error = name_missing ? "Give the vault a name." : nullptr});
        submit |= submitted_from_last_field();
    }

    field_label("Master password");
    focus_now();
    submit |= master_password_with_meter("##new-password", &g.password);

    field_label("Confirm master password");
    kit::master_password_field("##confirm-password", &g.confirm,
                               {.error = mismatch ? "The two passwords do not match." : nullptr});
    submit |= submitted_from_last_field();
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    facet::InlineAlert("##no-recovery", nullptr, "A forgotten master password cannot be recovered.",
                       {.tone = facet::Tone::Warning});
    draw_alert();

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    const bool can_submit =
        !g.password.empty() && g.password == g.confirm && (!ask_name || !trimmed(g.name).empty());
    const bool loading = g.work == Work::CreateFirst || g.work == Work::CreateNew;
    ImGui::BeginDisabled(!can_submit && !loading);
    if (facet::Button("Create vault", {.variant = facet::ButtonVariant::Primary, .width = facet::kFill, .loading = loading})) {
        submit = true;
    }
    ImGui::EndDisabled();
    if (submit && can_submit && !busy()) start(first ? Work::CreateFirst : Work::CreateNew);
}

void open_folder_dialog() {
    g.folder_open = true;
    g.folder_path.clear();
    g.folder_checked = "\x01";  // never a real path: forces a check
    g.folder_error.clear();
}

// The quiet paths under the card: links, so they never compete with the primary button.
void draw_links(app::AppState& state, float width_units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
    ImGui::BeginDisabled(busy());
    // Centred by measuring: a horizontal Facet stack put each Link on its own line here.
    const char* first = g.mode == Mode::Unlock      ? "Create a new vault"
                        : g.mode == Mode::CreateNew ? "Back to unlock"
                                                    : nullptr;
    const char* second = g.mode != Mode::CreateNew ? "Use an existing data folder" : nullptr;
    const float gap = facet::Px(facet::T().spacing.lg);
    float total = 0.0F;
    if (first != nullptr) total += facet::MeasureText(facet::TextStyle::Footnote, first).x;
    if (second != nullptr) total += facet::MeasureText(facet::TextStyle::Footnote, second).x;
    if (first != nullptr && second != nullptr) total += gap;
    const float start_x = ImGui::GetCursorPosX() + std::max(0.0F, (facet::Px(width_units) - total) * 0.5F);
    ImGui::SetCursorPosX(start_x);
    if (g.mode == Mode::Unlock) {
        if (facet::Link("Create a new vault", {.size = facet::ControlSize::Small})) {
            g.mode = Mode::CreateNew;
            clear_secrets();
            g.name = "New vault";
            g.field_error.clear();
            g.alert.clear();
            g.focus_pending = true;
        }
    } else if (g.mode == Mode::CreateNew) {
        if (facet::Link("Back to unlock", {.size = facet::ControlSize::Small})) reset_for_vault(state);
    }
    if (second != nullptr) {
        if (first != nullptr) ImGui::SameLine(0.0F, gap);
        if (facet::Link(second, {.size = facet::ControlSize::Small})) open_folder_dialog();
    }
    ImGui::EndDisabled();
}

std::filesystem::path folder_from_text(const std::string& text) {
    return std::filesystem::path(to_wide(trimmed(text)));
}

void check_folder() {
    if (g.folder_checked == g.folder_path) return;
    g.folder_checked = g.folder_path;
    g.folder_problem = nullptr;
    g.folder_valid = false;
    if (trimmed(g.folder_path).empty()) return;
    const std::filesystem::path dir = folder_from_text(g.folder_path);
    std::error_code ec;
    if (!dir.is_absolute()) {
        g.folder_problem = "Enter the full path of the folder, starting with its drive.";
    } else if (!std::filesystem::is_directory(dir, ec)) {
        g.folder_problem = "That folder does not exist.";
    } else if (std::filesystem::equivalent(dir, platform::data_dir(), ec)) {
        g.folder_problem = "The app already uses this folder.";
    } else if (!std::filesystem::exists(dir / L"vaults.json", ec) && !core::store::vault_exists(dir / L"vault.bin")) {
        g.folder_problem = "This folder holds no vault. Choose the folder that contains vaults.json or vault.bin.";
    } else {
        g.folder_valid = true;
    }
}

// Why a dialog: switching the data folder closes the app, so it gets its own step with the
// folder, the check and the consequences stated before the one button that does it.
void draw_folder_dialog(app::AppState& state) {
    if (!facet::BeginModal("Use an existing data folder", &g.folder_open, {.width = kFolderDialogUnits})) return;
    facet::StyledText("Open the vaults kept in another folder, for example on a USB drive or in a backup. "
                      "The folder must contain vaults.json or vault.bin.",
                      {.wrap = true});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));

    field_label("Data folder");
    check_folder();
    if (facet::PathField("##data-folder", &g.folder_path,
                         {.placeholder = "Choose the folder that holds your vaults", .error = g.folder_problem})) {
        g.folder_error.clear();
    }
    if (facet::PathFieldBrowsePressed()) {
        platform::file_dialog::Options opts;
        opts.parent = state.main_hwnd;
        opts.title = L"Select your steam-account-manager data folder";
        const auto res = platform::file_dialog::pick_folder(opts);
        if (res.ok) {
            g.folder_path = to_utf8(res.path.wstring());
            g.folder_error.clear();
        }
    }

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    facet::StyledText("In use now", {.style = facet::TextStyle::Footnote, .secondary = true});
    const std::string current = to_utf8(platform::data_dir().wstring());
    facet::TruncatedText(current.c_str(), {.mode = facet::TruncateMode::Middle, .focusable = true});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    facet::StyledText("The app remembers the new folder for your Windows user and closes. Start it again to open "
                      "the vaults in that folder. Nothing in the current folder is moved or deleted.",
                      {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    if (!g.folder_error.empty()) {
        facet::InlineAlert("##folder-error", nullptr, g.folder_error.c_str(), {.tone = facet::Tone::Destructive});
    }

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.md)));
    // Trailing buttons, right-aligned with last frame's measured width (a horizontal Facet
    // stack broke this row onto two lines here).
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, avail_w - g.folder_actions_width));
    ImGui::BeginGroup();
    if (facet::Button("Cancel")) g.folder_open = false;
    ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.sm));
    ImGui::BeginDisabled(!g.folder_valid);
    if (facet::Button("Use folder and close", {.variant = facet::ButtonVariant::Primary})) {
        std::string err;
        if (platform::set_custom_data_dir(folder_from_text(g.folder_path), &err)) {
            g.folder_open = false;
            if (state.main_hwnd != nullptr) PostMessageW(state.main_hwnd, WM_CLOSE, 0, 0);
        } else {
            g.folder_error = err.empty() ? "The folder could not be saved." : err;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    g.folder_actions_width = ImGui::GetItemRectSize().x;
    facet::EndModal();
}

// A custom data folder that went missing (an unplugged drive) makes the app fall back to the
// default folder, which can look like a first run: say so on the card.
void draw_data_folder_warning(float width_units) {
    const auto missing = platform::custom_data_dir_unavailable();
    if (!missing) return;
    const std::string text = "The data folder " + to_utf8(missing->wstring()) +
                             " is not available, so the default folder is open. Connect its drive and start "
                             "the app again to use it.";
    facet::InlineAlert("Data folder not found", nullptr, text.c_str(),
                       {.tone = facet::Tone::Warning, .width = width_units});
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
}

}  // namespace

// Why a scroll area holding a centred group: the card sits in the middle of whatever region
// the shell gives, and a short window (720 x 500 at a large UI scale) can still scroll to it.
void draw_locked_card(app::AppState& state) {
    const int frame = ImGui::GetFrameCount();
    if (frame < g.last_frame) g = CardState{};  // a new ImGui context (the capture harness)
    const bool arrived = frame - g.last_frame > 1;
    g.last_frame = frame;
    if (!g.initialized || g.shown_for_vault != platform::active_vault_id()) reset_for_vault(state);
    if (arrived) {
        g.focus_pending = true;
        g.scroll_top_frames = kScrollTopFrames;
    }
    if (g.error_in_frames > 0 && --g.error_in_frames == 0) set_wrong_password();

    run_work(state);
    if (state.unlocked) return;

    const facet::Theme& t = facet::T();
    if (facet::BeginScrollArea("##locked")) {
        // The keyboard focus on the first field would scroll a card taller than the window down
        // to that field; open at the top instead, with the mark and title in view.
        if (g.scroll_top_frames > 0) {
            ImGui::SetScrollY(0.0F);
            --g.scroll_top_frames;
        }
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float pad = facet::Px(t.spacing.lg);
        const float card_w = std::max(1.0F, std::min(facet::Px(kCardUnits), avail.x - pad * 2.0F));
        const float anchor = g.anchor_height > 0.0F ? g.anchor_height : g.block_height;
        const float top = std::max(pad, std::floor((avail.y - anchor) * 0.5F));
        ImGui::SetCursorPos(ImVec2((avail.x - card_w) * 0.5F, ImGui::GetCursorPosY() + top));
        ImGui::BeginGroup();
        draw_data_folder_warning(card_w / facet::Scale());
        facet::BeginCard("##vault-card", {.width = card_w / facet::Scale()});
        if (g.mode == Mode::Unlock) {
            draw_unlock_form(state);
        } else {
            draw_create_form(state);
        }
        facet::EndCard();
        draw_links(state, card_w / facet::Scale());
        ImGui::EndGroup();
        g.block_height = ImGui::GetItemRectSize().y;
        if (!g.transient || g.anchor_height <= 0.0F) g.anchor_height = g.block_height;
        g.layout_ready = true;
        ImGui::Dummy(ImVec2(0.0F, pad));
        facet::EndScrollArea();
    }
    draw_folder_dialog(state);
}

void preselect_vault(app::AppState& state) {
    state.needs_vault_pick = false;
    if (active_info(state) != nullptr) return;
    const app::VaultInfo* pick = nullptr;
    for (const app::VaultInfo* v : sorted_vaults(state)) {
        if (pick == nullptr || v->last_opened_unix > pick->last_opened_unix) pick = v;
    }
    if (pick != nullptr) platform::set_active_vault_id(pick->id);
}

namespace {

// Lock, a vault change and every capture scene close the data folder dialog, leave the create
// mode and forget typed text and errors. Only the measured layout survives, so a vault picked
// in the list does not make the card jump for a frame.
void reset_transient(app::AppState&) {
    const float block = g.block_height;
    const float anchor = g.anchor_height;
    const int last_frame = g.last_frame;
    g = CardState{};
    g.block_height = block;
    g.anchor_height = anchor;
    g.last_frame = last_frame;
    g.layout_ready = block > 0.0F;
}

}  // namespace

SAM_KIT_TRANSIENT_RESET(unlock_card, &reset_transient)

#ifdef SAM_UI_CAPTURE
namespace capture {

namespace {
// A clean card for the active vault that the first frame of the scene keeps (last_frame 0 is
// below any frame number of the new context).
void fresh(const app::AppState& state) {
    g = CardState{};
    g.last_frame = 0;
    g.scroll_top_frames = kScrollTopFrames;
    reset_for_vault(state);
}
}  // namespace

void reset(const app::AppState& state) {
    fresh(state);
}

// The error arrives after the card settled, as it does after a real attempt, so the scene
// shows that the card does not move.
void wrong_password(const app::AppState& state, const char* typed) {
    fresh(state);
    g.password = typed;
    g.error_in_frames = 10;
}

void unlocking(const app::AppState& state, const char* typed) {
    fresh(state);
    g.password = typed;
    g.work = Work::Unlock;
    g.work_frames = kWorkDelayFrames;
    g.hold_work = true;
    g.focus_pending = false;
}

void new_vault(const app::AppState& state) {
    fresh(state);
    g.mode = Mode::CreateNew;
    g.name = "New vault";
}

void data_folder(const app::AppState& state, const char* typed_path) {
    fresh(state);
    g.focus_pending = false;
    open_folder_dialog();
    g.folder_path = typed_path;
}

void remembered_fallback(const app::AppState& state, const char* reason) {
    fresh(state);
    g.remembered = false;
    g.cache_note = reason;
}

}  // namespace capture
#endif

}  // namespace sam::ui::screens::unlock
