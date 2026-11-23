// Settings > Cleaner: the Steam tracer cleaner. Profile, automatic triggers, the accounts it
// spares, then a preview of exactly what a run deletes and the run itself.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/text.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/cleaner_runner.hpp"
#include "app/job_pump.hpp"
#include "core/cleaner/profiles.hpp"
#include "core/cleaner/steam_scan.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/screens/settings/categories.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::settings {

namespace {

std::string format_bytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
    return buf;
}

const char* op_kind_label(cleaner::OpKind k) {
    switch (k) {
        case cleaner::OpKind::RemoveFile: return "File";
        case cleaner::OpKind::RemoveTree: return "Folder";
        case cleaner::OpKind::RemoveRegistryValue: return "Registry value";
        case cleaner::OpKind::RemoveRegistryKey: return "Registry key";
        case cleaner::OpKind::WriteRegistryString: return "Registry write";
        case cleaner::OpKind::VdfRemoveChild: return "VDF entry";
        case cleaner::OpKind::VdfSetValue: return "VDF value";
    }
    return "Other";
}

struct KeepRow {
    std::uint64_t steam_id = 0;
    std::string name;
    std::string detail;
};

struct CleanerState {
    bool scanned = false;
    std::vector<cleaner::AccountInfo> disk_accounts;  // Steam logins found on this PC
    bool previewing = false;
    std::string preview_error;
    bool full_wipe_open = false;
    bool run_open = false;
};

CleanerState& cleaner_state() {
    static CleanerState s;
    return s;
}

}  // namespace

void reset_cleaner_transient() {
    cleaner_state().full_wipe_open = false;
    cleaner_state().run_open = false;
}

namespace {

void rescan() {
    CleanerState& c = cleaner_state();
    c.disk_accounts.clear();
    if (auto install = cleaner::discover_install()) c.disk_accounts = cleaner::enumerate_accounts(*install);
    c.scanned = true;
}

// Vault accounts first, then Steam logins on this PC that are not in the vault.
std::vector<KeepRow> keep_rows(const app::AppState& state) {
    std::vector<KeepRow> rows;
    for (const auto& a : state.vault.accounts) {
        KeepRow r;
        r.steam_id = a.steam_id_64;
        r.name = kit::display_name(state, a);
        r.detail = r.steam_id == 0 ? "No Steam ID yet: refresh it to be able to keep it" : kit::login_text(state, a);
        rows.push_back(std::move(r));
    }
    for (const auto& acc : cleaner_state().disk_accounts) {
        const std::string digits = to_utf8(acc.steamid64);
        std::uint64_t sid = 0;
        const auto [_, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), sid);
        if (ec != std::errc{} || sid == 0) continue;
        if (std::any_of(rows.begin(), rows.end(), [&](const KeepRow& r) { return r.steam_id == sid; })) continue;
        KeepRow r;
        r.steam_id = sid;
        r.name = to_utf8(acc.persona_name);
        if (r.name.empty()) r.name = "Unnamed Steam account";
        r.detail = (state.settings.privacy_mode ? std::string(kit::kHiddenLogin) : to_utf8(acc.account_name)) +
                   ", on this PC, not in the vault";
        rows.push_back(std::move(r));
    }
    return rows;
}

bool is_kept(const app::Settings& s, std::uint64_t id) {
    const auto& ids = s.cleaner.preserved_steam_ids;
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void set_kept(app::Settings& s, std::uint64_t id, bool keep) {
    auto& ids = s.cleaner.preserved_steam_ids;
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (keep && it == ids.end()) ids.push_back(id);
    if (!keep && it != ids.end()) ids.erase(it);
}

// Why option cards: the three profiles differ in what they delete, which needs a paragraph
// each, and the cards let that paragraph be read before choosing.
void draw_profile(Page& p) {
    auto& cfg = p.s.cleaner;
    CleanerState& c = cleaner_state();
    section(p, "Profile", "Each profile deletes everything the one before it does, and more.");
    if (!block(p, "Cleanup profile", "Quick clean, account reset or full wipe.", "profile quick clean account reset full wipe")) {
        return;
    }
    // What each profile does to the user, in sentence case; the exact files are behind the
    // disclosure under the cards.
    static const facet::OptionCard kCards[] = {
        {.label = "Quick clean",
         .description = "Deletes caches, logs and crash dumps. No account is signed out, so it is safe before "
                        "every launch."},
        {.label = "Account reset",
         .description = "Quick clean, and signs out every Steam account on this PC that is not on your keep "
                        "list."},
        {.label = "Full wipe",
         .description = "Account reset, and deletes those accounts' game data: cloud saves, game settings, CS2 "
                        "configs and screenshots.",
         .badge = "Not undoable"},
    };
    const auto profiles = cleaner::built_in_profiles();
    int index = static_cast<int>(cfg.profile);
    if (facet::OptionCardGroup("##profile", &index, kCards, {.cardWidth = 220.0F, .readOnly = p.controls_disabled})) {
        const auto slot = static_cast<std::size_t>(index);
        if (slot < profiles.size() && profiles[slot].requires_confirmation) {
            c.full_wipe_open = true;
        } else {
            cfg.profile = static_cast<app::CleanerProfile>(index);
            p.state.cleaner_preview.reset();
        }
    }
    static bool details_open = false;
    if (facet::BeginDisclosure("Exactly what each profile deletes", &details_open)) {
        for (const auto& pr : profiles) {
            const std::string name = to_utf8(pr.name);
            const std::string text = to_utf8(pr.description);
            facet::Text(facet::TextStyle::Headline, "%s", name.c_str());
            facet::StyledText(text.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        }
        facet::EndDisclosure();
    }
}

void draw_triggers(Page& p) {
    auto& cfg = p.s.cleaner;
    const auto& d = p.d.cleaner;
    section(p, "Run automatically", "Run the cleaner below works whatever these say.");
    toggle_row(p, "Before signing an account in", &cfg.run_before_launch, d.run_before_launch,
               "Cleans while the launch already has Steam closed. Costs a few seconds and never gets in the way "
               "of the sign-in, so this is the safe trigger.",
               "trigger before launch sign in automatic");
    toggle_row(p, "When the vault is unlocked", &cfg.run_on_unlock, d.run_on_unlock,
               "Runs once per unlock. Closes Steam, which signs out anyone not on the keep list.",
               "trigger unlock open vault automatic");
    toggle_row(p, "When the app closes", &cfg.run_on_exit, d.run_on_exit,
               "Leaves the PC clean on exit. Closes Steam, which ends any open game.", "trigger exit close quit automatic");
}

// Why a searchable checklist: the keep list can hold every account of the vault plus the Steam
// logins on the PC, and finding one by name is the task.
void draw_keep_list(Page& p) {
    CleanerState& c = cleaner_state();
    section(p, "Accounts to keep");
    if (!block(p, "Accounts to keep", "The accounts every clean spares.", "keep preserve spare accounts list whitelist")) {
        return;
    }
    if (!c.scanned) rescan();
    facet::InlineAlert("##keep-warning", nullptr,
                       p.s.cleaner.profile == app::CleanerProfile::QuickClean
                           ? "Quick Clean never touches account data, so this list only matters for the other "
                             "profiles."
                           : "Only the accounts ticked here survive a clean. Every other Steam account on this PC "
                             "is signed out and loses its saved login, including the one you are about to launch "
                             "and the one signed in now.",
                       {.tone = p.s.cleaner.profile == app::CleanerProfile::QuickClean ? facet::Tone::Neutral
                                                                                        : facet::Tone::Destructive});
    const std::vector<KeepRow> rows = keep_rows(p.state);
    std::vector<facet::ChoiceItem> items;
    std::unique_ptr<bool[]> checked(new bool[rows.size() + 1]);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        items.push_back({.label = rows[i].name.c_str(), .detail = rows[i].detail.c_str(), .disabled = rows[i].steam_id == 0});
        checked[i] = rows[i].steam_id != 0 && is_kept(p.s, rows[i].steam_id);
    }
    if (facet::SearchableChecklist("##keep", std::span<bool>(checked.get(), rows.size()), items,
                                   {.placeholder = "Filter accounts",
                                    .checkedFirst = true,
                                    .visibleRows = 7,
                                    .emptyText = "No accounts in this vault or on this PC.",
                                    .readOnly = p.controls_disabled})) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].steam_id != 0) set_kept(p.s, rows[i].steam_id, checked[i]);
        }
        p.state.cleaner_preview.reset();
    }
    if (fit_button("Rescan this PC", {.icon = facet::Icon("refresh")})) rescan();
    facet::ItemTooltip("Reads Steam's logins again, so accounts signed in since then show up.");
}

void build_preview(app::AppState& state) {
    CleanerState& c = cleaner_state();
    c.previewing = true;
    c.preview_error.clear();
    const app::Settings snapshot = state.settings;
    app::AppState* st = &state;
    app::job_pump::submit([st, snapshot] {
        auto plan = app::cleaner_runner::build(snapshot, true);
        st->post_ui_callback([st, plan = std::move(plan)] {
            CleanerState& cs = cleaner_state();
            cs.previewing = false;
            if (!plan) cs.preview_error = "Steam is not installed on this PC, so there is nothing to clean.";
            st->cleaner_preview = plan;
        });
    });
}

void draw_plan_table(const cleaner::Plan& plan) {
    char summary[160];
    std::snprintf(summary, sizeof(summary), "%zu steps, %s in %llu files", plan.steps.size(),
                  format_bytes(plan.total_bytes).c_str(), static_cast<unsigned long long>(plan.total_file_count));
    facet::Text(facet::TextStyle::Headline, "%s", summary);
    static const facet::DataTableColumn kColumns[] = {
        {.label = "What", .width = 120.0F}, {.label = "Where"}, {.label = "Size", .width = 90.0F, .align = facet::Align::End}};
    const int count = static_cast<int>(plan.steps.size());
    if (facet::BeginDataTable("##plan", kColumns, count, nullptr,
                              {.rowHeight = 32.0F,
                               .emptyText = "Nothing to delete: this PC is already clean for this profile.",
                               .height = count == 0 ? 120.0F : 44.0F + 32.0F * static_cast<float>(std::min(count, 8))})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                const auto& step = plan.steps[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                facet::DataTableRow(i);
                ImGui::TableNextColumn();
                facet::TruncatedText(op_kind_label(step.op.kind));
                ImGui::TableNextColumn();
                std::string where = to_utf8(step.op.target);
                if (!step.op.value_name.empty()) where += " : " + to_utf8(step.op.value_name);
                if (!step.op.payload.empty()) where += " = \"" + to_utf8(step.op.payload) + "\"";
                facet::TruncatedText(where.c_str(), {.mode = facet::TruncateMode::Middle});
                ImGui::TableNextColumn();
                const std::string size = step.op.size_bytes > 0 ? format_bytes(step.op.size_bytes) : std::string{};
                facet::TruncatedText(size.c_str());
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
}

void draw_last_run(const app::AppState& state) {
    if (!state.cleaner_last) return;
    const auto& r = *state.cleaner_last;
    facet::SectionHeader("Last run");
    // The counts a run is judged by, as tiles.
    facet::BeginGrid("##last-run", {.minCellWidth = 140.0F, .maxColumns = 3});
    facet::GridNext();
    facet::StatTile("Removed", static_cast<double>(r.succeeded));
    facet::GridNext();
    facet::StatTile("Failed", static_cast<double>(r.failed));
    facet::EndGrid();
    if (r.bytes_freed > 0) {
        const std::string freed = format_bytes(r.bytes_freed) + " freed";
        facet::StyledText(freed.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    if (r.failure_messages.empty()) return;
    std::vector<std::string> texts;
    for (const auto& m : r.failure_messages) texts.push_back(to_utf8(m));
    std::vector<facet::LogEntry> entries;
    for (const auto& t : texts) entries.push_back({.text = t, .level = facet::LogLevel::Error});
    facet::LogView("##failures", entries, {.autoScroll = false, .timestamps = false, .height = 16.0F + 26.0F * static_cast<float>(std::min<std::size_t>(entries.size(), 6))});
}

// Why two steps: a run deletes without backup, so the preview of exactly what goes comes first
// and the run is only offered once it was looked at.
void draw_preview_run(Page& p) {
    app::AppState& state = p.state;
    CleanerState& c = cleaner_state();
    section(p, "Preview and run", "Nothing is backed up and a clean cannot be undone.");
    if (!block(p, "Preview and run", "See what the profile deletes, then run it.", "preview run now clean delete plan")) {
        return;
    }
    const bool busy = c.previewing || state.cleaner_busy.load();

    facet::Text(facet::TextStyle::Headline, "1. Preview");
    ImGui::BeginDisabled(busy || p.controls_disabled);
    if (fit_button(state.cleaner_preview ? "Build the preview again" : "Preview what will be deleted",
                      {.variant = facet::ButtonVariant::Primary, .loading = c.previewing})) {
        build_preview(state);
    }
    ImGui::EndDisabled();
    if (!c.preview_error.empty()) {
        facet::InlineAlert("##preview-error", nullptr, c.preview_error.c_str(), {.tone = facet::Tone::Destructive});
    }
    if (c.previewing) {
        facet::ProgressBar("Measuring what the profile would delete", facet::kIndeterminate);
    } else if (state.cleaner_preview) {
        draw_plan_table(*state.cleaner_preview);
    } else {
        facet::StyledText("Lists every file, folder and registry entry the profile would touch, with sizes. Nothing "
                          "is deleted.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    }

    ImGui::Spacing();
    facet::Text(facet::TextStyle::Headline, "2. Run");
    // A run uses the saved cleaner settings only: with unsaved cleaner edits it waits for Save.
    const bool pending = cleaner_settings_pending(state);
    ImGui::BeginDisabled(busy || !state.cleaner_preview || pending || p.controls_disabled);
    if (kit::quiet_destructive_button("Run the cleaner", facet::Icon("trash"))) c.run_open = true;
    ImGui::EndDisabled();
    if (state.cleaner_busy.load()) {
        facet::ProgressBar("Cleaning. Steam is closed first.", facet::kIndeterminate, {.tone = facet::Tone::Warning});
    } else if (p.controls_disabled) {
        facet::StyledText("Turn on Use the cleaner first.", {.style = facet::TextStyle::Footnote, .secondary = true});
    } else if (pending) {
        facet::StyledText("Save or discard your cleaner changes first, so the run uses what you see.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    } else if (!state.cleaner_preview) {
        facet::StyledText("Build the preview first.", {.style = facet::TextStyle::Footnote, .secondary = true});
    } else {
        facet::StyledText("Closes Steam, then deletes what the preview lists.",
                          {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    draw_last_run(state);
}

void draw_dialogs(Page& p) {
    CleanerState& c = cleaner_state();
    auto& cfg = p.s.cleaner;
    if (facet::ConfirmDialog("Use the full wipe profile?", &c.full_wipe_open,
                             "Full wipe also deletes the whole userdata folder of every account not on the keep "
                             "list: cloud saves, game settings, CS2 configs and launch options, controller "
                             "bindings, screenshots and non-Steam shortcuts. None of it is backed up.",
                             {.confirmLabel = "Use full wipe", .destructive = true}) == facet::DialogResult::Confirmed) {
        cfg.profile = app::CleanerProfile::FullWipe;
        p.state.cleaner_preview.reset();
    }
    const auto profiles = cleaner::built_in_profiles();
    const auto slot = static_cast<std::size_t>(cfg.profile);
    const std::string name = slot < profiles.size() ? to_utf8(profiles[slot].name) : std::string{"selected"};
    const std::string message = "This closes Steam and runs the " + name +
                                " profile now. Everything in the preview is deleted at once and cannot be restored.";
    if (facet::ConfirmDialog("Run the cleaner?", &c.run_open, message.c_str(),
                             {.confirmLabel = "Close Steam and clean", .destructive = true}) ==
        facet::DialogResult::Confirmed) {
        app::cleaner_runner::run_async(p.state, app::cleaner_runner::Trigger::Manual);
    }
}

}  // namespace

void draw_cleaner(Page& p) {
    auto& cfg = p.s.cleaner;
    ban_risk_banner(p, "The cleaner deletes what Steam leaves on this PC: caches and logs, and with the larger "
                       "profiles saved logins, login tokens and per-account data. Nothing is backed up, and safe "
                       "mode hides it.");
    section(p, nullptr);
    toggle_row(p, "Use the cleaner", &cfg.enabled, p.d.cleaner.enabled,
               "Allows cleaning. Off, nothing below runs, automatically or by hand. Safe mode also stops it.",
               "cleaner tracer enable switch use");
    // Why only the controls are disabled while it is off: the profile texts and triggers are what
    // you read to decide whether to turn it on.
    p.controls_disabled = !cfg.enabled;
    draw_profile(p);
    draw_triggers(p);
    draw_keep_list(p);
    draw_preview_run(p);
    p.controls_disabled = false;
    draw_dialogs(p);
}

}  // namespace sam::ui::screens::settings
