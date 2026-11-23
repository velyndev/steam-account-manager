// Settings > Vaults and storage: every vault as a card (switch, rename, color, icon, open at
// startup, delete), creating a vault, and where the app keeps its data.

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
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
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/pickers.h>
#include <facet/widgets/selection.h>

#include "app/vault_registry.hpp"
#include "core/account_store/store.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/log.hpp"
#include "platform/file_dialog.hpp"
#include "platform/paths.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/secrets.hpp"
#include "ui/screens/settings/capture_hooks.hpp"
#include "ui/screens/settings/categories.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::settings {

namespace {

// The color new vaults get: the registry's default (user data, not a theme color).
constexpr std::uint32_t kNewVaultColor = 0x4F8EF7FFu;

struct VaultsState {
    bool create_open = false;
    std::string name;
    std::string password;
    std::string confirm;
    std::string error;

    bool delete_open = false;
    std::string delete_id;

    std::string menu_id;                   // the vault whose actions menu is open
    std::map<std::string, bool> renaming;  // vault id -> its name is being edited
    std::string color_dirty_id;  // a color was picked and is saved once the picking settles
    int color_frame = 0;

    // Data folder.
    bool move_open = false;
    std::filesystem::path move_to;
    bool move_to_default = false;
    bool restart_pending = false;
    std::string move_error;
};

VaultsState& vaults_state() {
    static VaultsState s;
    return s;
}

}  // namespace

void reset_vaults_transient() {
    VaultsState& vs = vaults_state();
    vs.create_open = false;
    vs.password.clear();
    vs.confirm.clear();
    vs.error.clear();
    vs.delete_open = false;
    vs.delete_id.clear();
    vs.menu_id.clear();
    vs.renaming.clear();
    vs.move_open = false;
}

namespace {

ImVec4 unpack(std::uint32_t rgba) {
    return ImVec4(static_cast<float>((rgba >> 24) & 0xFF) / 255.0F, static_cast<float>((rgba >> 16) & 0xFF) / 255.0F,
                  static_cast<float>((rgba >> 8) & 0xFF) / 255.0F, static_cast<float>(rgba & 0xFF) / 255.0F);
}

std::uint32_t pack(const ImVec4& c) {
    const auto byte = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.0F, 1.0F) * 255.0F + 0.5F); };
    return (byte(c.x) << 24) | (byte(c.y) << 16) | (byte(c.z) << 8) | byte(c.w);
}

void pick_icon(app::AppState& state, const std::string& id) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Choose a vault icon image";
    opts.filters = {{L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif"}};
    const auto res = platform::file_dialog::open_file(opts);
    if (!res.ok) return;
    std::string err;
    if (!app::set_vault_icon(state.vault_registry, id, res.path, &err)) {
        SAM_LOG_WARN("vault icon set failed: {}", err);
        kit::show_toast(state, "The icon could not be set: " + err, {.tone = widgets::ToastTone::Destructive});
    }
}

// One vault as a row of the vault list: color, name (rename from the menu, a double click or F2),
// its state with the switch action, open at startup, and the rare actions in a trailing menu.
void vault_row(app::AppState& state, app::VaultInfo& v, bool active, bool can_delete, std::string* switch_to) {
    VaultsState& vs = vaults_state();
    app::VaultRegistry& reg = state.vault_registry;
    ImGui::PushID(v.id.c_str());
    const float more = facet::ControlHeight(facet::ControlSize::Inherit) / facet::Px(1.0F);

    ImVec4 color = unpack(v.color_rgba);
    if (facet::ColorWell("##vault-color", &color, {.width = 28.0F})) {
        v.color_rgba = pack(color);
        vs.color_dirty_id = v.id;
        vs.color_frame = ImGui::GetFrameCount();
    }
    facet::ItemTooltip("Vault color");
    ImGui::SameLine();
    ImGui::BeginGroup();
    std::string name = v.name;
    bool* editing = &vs.renaming[v.id];
    if (facet::InlineRename("##name", &name, editing,
                            {.maxLength = 64, .width = std::max(80.0F, facet::AvailableWidthUnits() - more - 8.0F)})) {
        if (!name.empty()) app::rename_vault(reg, v.id, name);
    }
    if (active) {
        facet::StatusPill("Open now", {.tone = facet::Tone::Success, .icon = facet::Icon("success"),
                                       .size = facet::ControlSize::Small});
    } else if (fit_button("Switch to this vault", {.size = facet::ControlSize::Small})) {
        *switch_to = v.id;
    }
    ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.lg));
    bool auto_open = reg.auto_open_id == v.id;
    if (facet::Toggle("Open at startup", &auto_open, {.size = facet::ControlSize::Small,
                                                      .width = fit_width("Open at startup", facet::ControlSize::Small) + 64.0F})) {
        app::set_auto_open(reg, auto_open ? v.id : std::string{});
    }
    facet::ItemTooltip("Skips the vault picker when this vault can open without the password. Only one vault can.");
    ImGui::EndGroup();

    ImGui::SameLine();
    align_trailing(more);
    bool menu_open = vs.menu_id == v.id;
    if (facet::IconButton("More actions", facet::Icon("more-horizontal"), {.selected = menu_open})) {
        menu_open = !menu_open;
    }
    if (facet::BeginPopover("##vault-menu", &menu_open, {.placement = facet::PopoverPlacement::Below, .arrow = false})) {
        if (facet::MenuItem("Rename", {.icon = facet::Icon("edit")})) {
            *editing = true;
            menu_open = false;
        }
        if (facet::MenuItem(v.icon_file.empty() ? "Set icon" : "Change icon", {.icon = facet::Icon("image")})) {
            menu_open = false;
            pick_icon(state, v.id);
        }
        if (!v.icon_file.empty() && facet::MenuItem("Remove icon")) {
            menu_open = false;
            app::clear_vault_icon(reg, v.id);
        }
        facet::Separator();
        ImGui::BeginDisabled(!can_delete);
        if (facet::MenuItem("Delete vault", {.icon = facet::Icon("trash"), .destructive = true})) {
            menu_open = false;
            vs.delete_id = v.id;
            vs.delete_open = true;
        }
        ImGui::EndDisabled();
        if (!can_delete) {
            facet::StyledText(active ? "Switch to another vault to delete this one." : "Keep at least one vault.",
                              {.style = facet::TextStyle::Footnote, .secondary = true});
        }
        facet::EndPopover();
    }
    if (menu_open) {
        vs.menu_id = v.id;
    } else if (vs.menu_id == v.id) {
        vs.menu_id.clear();
    }
    ImGui::PopID();
}

void draw_create_dialog(app::AppState& state) {
    VaultsState& vs = vaults_state();
    if (!facet::BeginModal("Create a vault", &vs.create_open, {.width = 440.0F})) return;
    facet::StyledText("A vault has its own accounts, settings and master password.",
                      {.secondary = true, .wrap = true});
    ImGui::Spacing();
    facet::TextField("Name", &vs.name, {.placeholder = "For example Work", .maxLength = 64});
    kit::master_password_field("Master password", &vs.password, {.show_strength = true});
    const bool mismatch = !vs.confirm.empty() && vs.confirm != vs.password;
    kit::master_password_field("Confirm password", &vs.confirm,
                               {.error = mismatch ? "The passwords do not match." : nullptr});
    if (!vs.error.empty()) facet::InlineAlert("##create-error", nullptr, vs.error.c_str(), {.tone = facet::Tone::Destructive});
    ImGui::Spacing();
    align_trailing(fit_width("Cancel") + fit_width("Create vault") + facet::T().spacing.sm);
    if (fit_button("Cancel")) vs.create_open = false;
    ImGui::SameLine();
    const bool ok = !vs.name.empty() && !vs.password.empty() && vs.password == vs.confirm;
    ImGui::BeginDisabled(!ok);
    if (fit_button("Create vault", {.variant = facet::ButtonVariant::Primary})) {
        try {
            const auto id = app::new_vault_id();
            std::error_code ec;
            std::filesystem::create_directories(app::vault_dir_for(id), ec);
            core::store::create_new_vault(app::vault_dir_for(id) / L"vault.bin", crypto::make_secure(vs.password));
            app::add_vault_entry(state.vault_registry, id, vs.name, kNewVaultColor);
            kit::show_toast(state, "Vault \"" + vs.name + "\" created. Switch to it from the vault list.",
                            {.tone = widgets::ToastTone::Success});
            vs.create_open = false;
        } catch (const std::exception& ex) {
            vs.error = ex.what();
        }
    }
    ImGui::EndDisabled();
    facet::EndModal();
    if (!vs.create_open) {
        vs.password.clear();
        vs.confirm.clear();
        vs.error.clear();
    }
}

// Why one list card: vaults are equal objects; a uniform list keeps the rare actions in a menu
// and puts the open vault first.
void draw_vaults(Page& p) {
    app::AppState& state = p.state;
    VaultsState& vs = vaults_state();
    app::VaultRegistry& reg = state.vault_registry;
    section(p, "Vaults",
            "Each vault holds its own accounts and settings. Options that say they apply to every vault are shared. "
            "Switching vaults restarts the app on the other vault.");

    std::string switch_to;
    if (block(p, "Vaults", "Switch, rename, recolor or delete a vault.", "vault switch rename delete color icon")) {
        std::vector<app::VaultInfo*> items;
        for (auto& v : reg.vaults) items.push_back(&v);
        const std::string active = platform::active_vault_id();
        // The open vault first, then the registry order.
        std::sort(items.begin(), items.end(), [&active](const app::VaultInfo* a, const app::VaultInfo* b) {
            if ((a->id == active) != (b->id == active)) return a->id == active;
            return a->order != b->order ? a->order < b->order : a->name < b->name;
        });
        facet::BeginCard("##vault-list");
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) facet::Separator();
            const bool is_active = items[i]->id == active;
            vault_row(state, *items[i], is_active, reg.vaults.size() > 1 && !is_active, &switch_to);
        }
        facet::EndCard();
        if (fit_button("Create vault", {.icon = facet::Icon("plus")})) {
            vs.name.clear();
            vs.create_open = true;
        }
    }

    if (!vs.color_dirty_id.empty() && ImGui::GetFrameCount() - vs.color_frame > 30) {
        app::save_registry(reg);
        vs.color_dirty_id.clear();
    }

    draw_create_dialog(state);
    // The dialog names the vault exactly as its row does, read from the registry while it is open.
    const app::VaultInfo* doomed = vs.delete_id.empty() ? nullptr : reg.find(vs.delete_id);
    if (vs.delete_open && doomed == nullptr) vs.delete_open = false;
    const std::string doomed_name = doomed != nullptr ? doomed->name : std::string{};
    const std::string delete_title = "Delete the vault \"" + doomed_name + "\"?###delete-vault-dialog";
    const std::string delete_message = "Deletes \"" + doomed_name +
                                       "\" with all its accounts, authenticators and settings from this PC. This "
                                       "cannot be undone.";
    if (facet::ConfirmDialog(delete_title.c_str(), &vs.delete_open, delete_message.c_str(),
                             {.confirmLabel = "Delete vault", .destructive = true}) == facet::DialogResult::Confirmed) {
        std::string err;
        if (!app::remove_vault(reg, vs.delete_id, &err)) {
            SAM_LOG_WARN("vault delete failed: {}", err);
            kit::show_toast(state, "The vault could not be deleted: " + err, {.tone = widgets::ToastTone::Destructive});
        }
        vs.delete_id.clear();
    }
    if (!switch_to.empty()) kit::switch_vault(state, switch_to);
}

void begin_move(const std::filesystem::path& to, bool to_default) {
    VaultsState& vs = vaults_state();
    vs.move_error.clear();
    vs.move_to = to;
    vs.move_to_default = to_default;
    vs.move_open = true;
}

void draw_move_dialog() {
    VaultsState& vs = vaults_state();
    if (!facet::BeginModal("Move the data folder", &vs.move_open, {.width = 480.0F})) return;
    facet::StyledText(vs.move_to_default ? "Moves all data back to the default location:" : "Copies all data to:",
                      {.wrap = true});
    facet::StyledText(vs.move_to.string().c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    ImGui::Spacing();
    std::error_code ec;
    if (std::filesystem::exists(vs.move_to / "vault.bin", ec)) {
        facet::InlineAlert("##overwrite", nullptr, "That folder already has a vault.bin, and it will be overwritten.",
                           {.tone = facet::Tone::Warning});
    }
    facet::StyledText("The current folder is removed at the next launch. Restart the app to use the new one.",
                      {.secondary = true, .wrap = true});
    ImGui::Spacing();
    align_trailing(fit_width("Cancel") + fit_width("Copy and apply") + facet::T().spacing.sm);
    if (fit_button("Cancel")) vs.move_open = false;
    ImGui::SameLine();
    if (fit_button("Copy and apply", {.variant = facet::ButtonVariant::Primary})) {
        std::string err;
        if (platform::relocate_data_dir(vs.move_to, &err)) {
            if (vs.move_to_default) platform::clear_custom_data_dir(nullptr);
            vs.restart_pending = true;
        } else {
            vs.move_error = err;
        }
        vs.move_open = false;
    }
    facet::EndModal();
}

void draw_storage(Page& p) {
    app::AppState& state = p.state;
    VaultsState& vs = vaults_state();
    section(p, "Storage");
    if (begin_row(p, "Data folder",
                  {.description = "Holds the vaults, settings and logs. Moving it, for example to a USB stick, takes "
                                  "effect after a restart. Applies to every vault.",
                   .keywords = "data folder storage location move usb portable path directory"})) {
        // The path in one line, cut in the middle; the full path on hover.
        const std::string dir = platform::data_dir().string();
        facet::TruncatedText(dir.c_str(), {.mode = facet::TruncateMode::Middle});
        if (const auto bad = platform::custom_data_dir_unavailable()) {
            const std::string msg = "The chosen folder " + bad->string() + " was unavailable, so the default location is used.";
            facet::InlineAlert("##unavailable", nullptr, msg.c_str(), {.tone = facet::Tone::Warning});
        }
        if (vs.restart_pending) {
            facet::InlineAlert("Restart to finish##moved", nullptr,
                               "The data was copied. Restart the app to use the new location; the old folder is "
                               "removed at the next launch.",
                               {.tone = facet::Tone::Accent});
        }
        // The actions wrap onto a second line where the column is narrow.
        facet::BeginFlow("##storage-actions");
        if (!vs.restart_pending) {
            if (facet::FlowNext() && fit_button("Move data folder")) {
                platform::file_dialog::Options opts;
                opts.parent = state.main_hwnd;
                opts.title = L"Choose a folder for the account manager data";
                const auto res = platform::file_dialog::pick_folder(opts);
                if (res.ok) begin_move(res.path, false);
            }
            if (platform::using_custom_data_dir() && facet::FlowNext() && fit_button("Move back to the default location")) {
                begin_move(platform::default_data_dir(), true);
            }
        }
        if (facet::FlowNext() &&
            fit_button("Open data folder", {.variant = facet::ButtonVariant::Plain, .icon = facet::Icon("folder")})) {
            open_folder(platform::data_dir());
        }
        facet::EndFlow();
        if (!vs.move_error.empty()) {
            facet::InlineAlert("The data folder was not moved##move-error", nullptr, vs.move_error.c_str(),
                               {.tone = facet::Tone::Destructive});
        }
        end_row();
    }
    draw_move_dialog();
}

}  // namespace

#ifdef SAM_UI_CAPTURE
namespace capture {
void open_create_vault() {
    vaults_state().name = "Work";
    vaults_state().create_open = true;
}
void open_delete_vault(const std::string& vault_id) {
    VaultsState& vs = vaults_state();
    vs.delete_id = vault_id;
    vs.delete_open = true;
}
void open_vault_menu(const std::string& vault_id) {
    vaults_state().menu_id = vault_id;
}
}  // namespace capture
#endif

void draw_vaults_storage(Page& p) {
    draw_vaults(p);
    draw_storage(p);
}

}  // namespace sam::ui::screens::settings
