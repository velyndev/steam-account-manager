#include "ui/screens/accounts_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
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
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "ui/icons.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/standing.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/screens/settings_screen.hpp"

namespace sam::ui::screens {

namespace {

using accounts::screen;
using accounts::ScreenState;

// The labels of core::SortKey, in its persisted order (settings.accounts_sort indexes them).
const char* const kSortLabels[] = {
    "Name, A to Z",          "Name, Z to A",      "Login, A to Z",      "Last sign-in",
    "Recently created",      "Premier rating",    "Steam level",        "CS2 level",
    "No cooldown first",     "Drop not claimed",  "CS2 XP",             "Spent",
    "Cooldown ending first",
};

void menu_note(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true,
                             .width = facet::T().overlay.menuMinWidth + 60.0F});
}

void progress_note(const char* format, int done, int total) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), format, done, total);
    menu_note(buf);
}

// Why one Refresh button with a menu: the three refresh kinds differ in cost and in what they
// need, so each gets its availability and reason in the menu instead of three equal buttons.
void refresh_menu(app::AppState& state) {
    ScreenState& s = screen();
    if (facet::Button("Refresh", {.icon = facet::Icon("chevron-down")})) s.refresh_menu_open = !s.refresh_menu_open;
    if (!facet::BeginPopover("##refresh-menu", &s.refresh_menu_open,
                             {.placement = facet::PopoverPlacement::Below, .arrow = false})) {
        return;
    }
    const bool no_key = state.settings.web_api_key.empty();
    const int total = state.refresh_all_total.load(std::memory_order_relaxed);
    const int done = state.refresh_all_done.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(no_key);
    if (facet::MenuItem("Refresh all accounts", {.icon = facet::Icon("refresh")})) state.refresh_account_data(false, true);
    ImGui::EndDisabled();
    if (no_key) {
        menu_note("Needs a Steam Web API key.");
        if (facet::MenuItem("Add a Steam Web API key...", {.icon = facet::Icon("settings")})) {
            kit::open_settings(state, SettingsCategory::NetworkData);
        }
    } else if (total > 0 && done < total) {
        progress_note("Refreshing %d of %d accounts.", done, total);
    } else {
        menu_note("Steam data, bans and the CS2 cooldown. Skips what is still up to date.");
    }

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    if (state.gc_autopull.active) {
        if (facet::MenuItem("Stop refreshing CS2 data", {.icon = facet::Icon("stop")})) state.cancel_gc_autopull();
    } else {
        ImGui::BeginDisabled(!state.settings.cs2_gc.enabled);
        if (facet::MenuItem("Refresh CS2 data for all accounts", {.icon = icons::crosshair})) state.refresh_gc_all(true);
        ImGui::EndDisabled();
    }
    const bool validate_busy = state.gc_validate.active && !state.gc_validate.feed_refresh_all;
    const int gc_done = state.gc_autopull.received + (validate_busy ? state.gc_validate.done : 0);
    const int gc_total =
        state.gc_autopull.total + (validate_busy ? static_cast<int>(state.gc_validate.queue.size()) : 0);
    if (!state.settings.cs2_gc.enabled) {
        menu_note("Needs the CS2 connection, which is turned off in Settings.");
        if (facet::MenuItem("Open CS2 settings...", {.icon = facet::Icon("settings")})) {
            kit::open_settings(state, SettingsCategory::Cs2);
        }
    } else if (gc_total > 0 && gc_done < gc_total) {
        progress_note("Pulled %d of %d accounts.", gc_done, gc_total);
    } else {
        menu_note("Level, medals, ranks and cooldown. Skips what is still up to date.");
    }

    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    const bool spend_busy = state.spend_bulk_running.load(std::memory_order_acquire);
    const bool spend_shown = state.settings.info.show_external_funds;
    ImGui::BeginDisabled(spend_busy || !spend_shown);
    if (facet::MenuItem("Refresh spend for all accounts", {.icon = facet::Icon("refresh")})) state.refresh_all_spend(false);
    ImGui::EndDisabled();
    const int sp_total = state.spend_total.load(std::memory_order_relaxed);
    const int sp_done = state.spend_done.load(std::memory_order_relaxed);
    if (!spend_shown) {
        menu_note("Spend is turned off in Settings, so it is not fetched.");
    } else if (spend_busy) {
        if (sp_total > 0) progress_note("Fetched %d of %d accounts.", sp_done, sp_total);
        else menu_note("Fetching spend.");
    } else {
        menu_note("Reads the external funds total from Steam Support. One sign-in per account.");
    }
    facet::EndPopover();
}


void more_menu(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    if (facet::IconButton("More actions", facet::Icon("more-horizontal"), {.selected = s.more_menu_open})) {
        s.more_menu_open = !s.more_menu_open;
    }
    if (!facet::BeginPopover("##more-menu", &s.more_menu_open,
                             {.placement = facet::PopoverPlacement::Below, .arrow = false})) {
        return;
    }
    ImGui::BeginDisabled(visible.size() < 2);
    if (facet::MenuItem("Select all shown accounts", {.icon = facet::Icon("check")})) {
        std::vector<std::string> ids;
        for (std::size_t idx : visible) ids.push_back(state.vault.accounts[idx].id);
        accounts::store_selection(state, ids);
    }
    ImGui::EndDisabled();
    if (!state.settings.safe_mode) {
        const bool cleaning = state.cleaner_busy.load();
        ImGui::BeginDisabled(cleaning);
        if (facet::MenuItem("Clean Steam caches...", {.icon = facet::Icon("trash")})) s.quick_clean_open = true;
        ImGui::EndDisabled();
        menu_note(cleaning ? "Cleaning Steam now." : "Deletes Steam's caches, logs and crash dumps. No account is signed out.");
    }
    facet::EndPopover();
}

// The page context line: how many accounts there are and how many the filters show. Progress
// lives in the sidebar's activity, so it is said once (rule 4).
std::string header_context(const app::AppState& state, const std::vector<std::size_t>& visible) {
    const std::size_t all = state.vault.accounts.size();
    const std::string total = kit::format_count(static_cast<long long>(all));
    if (visible.size() == all) return total + (all == 1 ? " account" : " accounts");
    return kit::format_count(static_cast<long long>(visible.size())) + " of " + total + " accounts shown";
}

// Why a flow: at 900 units or at scale 1.5 the search, three chips, the sort and the view switch
// do not fit one row, and a flow wraps them in order instead of clipping.
void toolbar(app::AppState& state, bool list) {
    auto& q = state.settings.quick_filters;
    facet::BeginFlow("##toolbar", {.gap = facet::Gap::Item});
    if (facet::FlowNext()) {
        facet::SearchField("##search", &state.search_query, {.placeholder = "Search name, login, notes", .width = 260.0F});
    }
    struct FilterChip {
        const char* label;
        bool* value;
        facet::Icon icon;
        const char* tip;
    };
    const FilterChip chips[] = {
        {"Banned", &q.only_banned, icons::ban, "Only accounts with a ban"},
        {"Cooldown", &q.only_cooldown, facet::Icon("clock"), "Only accounts in a CS2 cooldown"},
        {"Prime", &q.only_prime, facet::Icon(), "Only accounts with CS2 Prime"},
    };
    for (const FilterChip& chip : chips) {
        if (!facet::FlowNext()) continue;
        if (facet::Chip(chip.label, {.selected = *chip.value, .icon = chip.icon})) {
            *chip.value = !*chip.value;
            state.save_settings();
        }
        facet::ItemTooltip("%s", chip.tip);
    }
    if (list && facet::FlowNext()) {
        int sort = std::clamp(state.settings.accounts_sort, 0, 12);
        if (facet::Dropdown("##sort", &sort, kSortLabels, {.width = 190.0F})) {
            state.settings.accounts_sort = sort;
            state.save_settings();
        }
        facet::ItemTooltip("Sort the list");
    }
    if (facet::FlowNext()) {
        static const facet::ChoiceItem kViews[] = {
            {.label = "List", .icon = facet::Icon("list")},
            {.label = "Table", .icon = icons::table},
        };
        int view = list ? 0 : 1;
        if (facet::SegmentedControl("##view", &view, kViews, {.iconOnly = true})) {
            state.settings.accounts_view = view == 0 ? app::AccountsViewMode::List : app::AccountsViewMode::Table;
            state.save_settings();
            screen().scrolled_to.clear();
        }
    }
    facet::EndFlow();
}

void warn_missing_api_key(app::AppState& state) {
    if (state.warned_missing_api_key || state.vault.accounts.empty() || !state.settings.web_api_key.empty()) return;
    kit::show_toast(state, "No Steam Web API key is set, so account data is not refreshed. Add one in Settings.",
                    {.tone = widgets::ToastTone::Warning,
                     .id = "missing-api-key",
                     .settings_category = static_cast<int>(SettingsCategory::NetworkData),
                     .seconds = -1});
    state.warned_missing_api_key = true;
}

}  // namespace

// Why this order: the header says how many accounts there are, the toolbar narrows the list,
// and the chosen view fills the rest of the page. An empty vault shows one thing to do.
void draw_accounts(app::AppState& state) {
    accounts::track_context(state);
    warn_missing_api_key(state);
    const bool list = state.settings.accounts_view == app::AccountsViewMode::List;
    const std::vector<std::size_t> visible = accounts::visible_accounts(state, list ? state.settings.accounts_sort : 0);
    const bool any = !state.vault.accounts.empty();

    {
        kit::PageScope page("accounts", {.scroll = false});
        {
            const std::string context = header_context(state, visible);
            kit::PageHeaderScope header("Accounts", {.context = context.c_str()});
            if (any) {
                refresh_menu(state);
                if (facet::Button("New group", {.icon = facet::Icon("plus")})) accounts::request_new_group();
                more_menu(state, visible);
            }
        }

        if (!any) {
            // Centred in the page: a quarter of the free height above the block.
            ImGui::Dummy(ImVec2(0.0F, ImGui::GetContentRegionAvail().y * 0.25F));
            if (kit::empty_state("No accounts yet",
                                 {.message = "Add one by signing in with its password, importing a Steam Guard file, "
                                             "or using the login Steam saved on this PC.",
                                  .icon = facet::Icon("user"),
                                  .action_label = "Add account",
                                  .action_icon = facet::Icon("plus")})) {
                kit::go_to(state, app::Screen::AddAccount);
            }
        } else {
            toolbar(state, list);
            ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
            if (visible.empty()) {
                std::string message = "No account matches";
                if (!state.search_query.empty()) message += " \"" + state.search_query + "\"";
                message += state.search_query.empty() ? " the filters that are on." : " with the filters that are on.";
                if (facet::EmptyState("No matching accounts", {.message = message.c_str(),
                                                               .icon = facet::Icon("search"),
                                                               .actionLabel = "Clear filters"})) {
                    accounts::clear_filters(state);
                }
            } else if (list) {
                accounts::draw_list_view(state, visible);
            } else {
                accounts::draw_table_view(state, visible);
            }
        }
        accounts::draw_dialogs(state);
    }
    accounts::run_pumps(state);
}

}  // namespace sam::ui::screens
