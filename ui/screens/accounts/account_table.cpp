// The table view: every account in one sortable, multi-select table, with the selected
// account's detail in an inspector pane. Why a table: it answers "which of my accounts..." by
// sorting a column.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/overlays.h>

#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/standing.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::accounts {

namespace {

enum Column {
    kAccount,
    kStanding,
    kCooldown,
    kPremier,
    kWingman,
    kCs2Level,
    kSteamLevel,
    kGames,
    kPrime,
    kDrop,
    kLastSignIn,
    kSpent,
    kTrust,
    kColumnCount
};

struct TableRows {
    std::vector<std::size_t> accounts;  // indices into state.vault.accounts, in display order
    std::vector<ImGuiID> keys;
};

ImGuiID table_key(ImGuiSelectionBasicStorage* storage, int index) {
    const auto* rows = static_cast<const TableRows*>(storage->UserData);
    return rows->keys[static_cast<std::size_t>(index)];
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int ban_rank(const app::AppState& state, const core::Account& a) {
    switch (kit::ban_tone(state, a)) {
        case facet::Tone::Destructive: return 2;
        case facet::Tone::Warning: return 1;
        default: return 0;
    }
}

long long sort_value(const app::AppState& state, const core::Account& a, int column, std::int64_t now) {
    switch (column) {
        case kStanding: return ban_rank(state, a);
        case kCooldown:
            if (!kit::cooldown_active(a, now)) return 0;
            return a.cs2.cooldown_expires_unix == core::kCooldownNever ? INT64_MAX : a.cs2.cooldown_expires_unix - now;
        case kPremier: return a.cs2.premier_rating;
        case kWingman: return a.cs2.wingman_rank;
        case kCs2Level: return a.cs2.cs2_player_level;
        case kSteamLevel: return a.web.steam_level;
        case kGames: return a.web.owned_games_count;
        case kPrime: return a.cs2.prime_status ? 1 : 0;
        case kDrop: return kit::weekly_drop_claimed(a, now) ? 1 : 0;
        case kLastSignIn: return a.last_login_unix;
        case kSpent: return a.funds.total_spend_usd_cents;
        case kTrust: return static_cast<long long>(a.trust);
        default: return 0;
    }
}

void sort_rows(const app::AppState& state, std::vector<std::size_t>& rows, int column, bool descending) {
    const std::int64_t now = now_unix();
    std::unordered_map<std::size_t, std::string> names;
    for (std::size_t idx : rows) names[idx] = lower(kit::display_name(state, state.vault.accounts[idx]));
    std::stable_sort(rows.begin(), rows.end(), [&](std::size_t l, std::size_t r) {
        if (column != kAccount) {
            const long long lv = sort_value(state, state.vault.accounts[l], column, now);
            const long long rv = sort_value(state, state.vault.accounts[r], column, now);
            if (lv != rv) return descending ? lv > rv : lv < rv;
            return names[l] < names[r];
        }
        return descending ? names[l] > names[r] : names[l] < names[r];
    });
}

// A number cell, right-aligned like its header so the digits line up; empty while unknown, so
// a healthy or unread column is quiet.
void number_cell(long long value) {
    if (value < 0) return;
    const std::string text = kit::format_count(value);
    const float w = facet::MeasureText(facet::TextStyle::Body, text.c_str()).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    kit::RowCell cell;
    facet::StyledText(text.c_str());
}

void text_cell(const char* text, bool secondary = false) {
    kit::RowCell cell;
    facet::StyledText(text, {.secondary = secondary});
}

std::string spend_cell(const core::Account& a) {
    const long long cents = a.funds.total_spend_usd_cents;
    if (!a.funds.currency_is_usd) return kit::format_count(cents / 100) + " " + a.funds.currency;
    char tail[8];
    std::snprintf(tail, sizeof(tail), ".%02lld", cents % 100);
    return "$" + kit::format_count(cents / 100) + tail;
}

void cells(app::AppState& state, core::Account& a, std::int64_t now) {
    if (ImGui::TableNextColumn()) {
        kit::RowCell cell;
        if (a.trust != core::TrustLabel::Unset) {
            kit::trust_dot(a.trust, 0.0F);
        } else {
            const float d = facet::Px(facet::T().metrics.statusDotSize);
            ImGui::Dummy(ImVec2(d, d));
        }
        cell.next();
        kit::account_row(state, a, {.layout = kit::AccountRowOptions::Layout::OneLine,
                                    .show_login = !state.settings.list_view.hide_account_name,
                                    .detail = state.refreshing_ids.count(a.id) != 0 ? "refreshing" : nullptr});
    }
    if (ImGui::TableNextColumn() && kit::any_ban(state, a)) {
        kit::RowCell cell;
        const std::string text = kit::ban_summary(state, a) + "###standing";
        facet::StatusPill(text.c_str(), {.tone = kit::ban_tone(state, a), .icon = icons::ban,
                                         .size = facet::ControlSize::Small});
    }
    if (ImGui::TableNextColumn() && kit::cooldown_active(a, now)) {
        kit::RowCell cell;
        std::string text = kit::cooldown_text(a, now);
        if (text.rfind("Cooldown ", 0) == 0) text = text.substr(9);
        text += "###cooldown";
        facet::StatusPill(text.c_str(), {.tone = facet::Tone::Warning, .icon = facet::Icon("clock"),
                                         .size = facet::ControlSize::Small});
        if (!a.cs2.cooldown_reason.empty()) facet::ItemTooltip("Reason: %s", a.cs2.cooldown_reason.c_str());
    }
    if (ImGui::TableNextColumn()) number_cell(a.cs2.premier_rating);
    if (ImGui::TableNextColumn()) number_cell(a.cs2.wingman_rank);
    if (ImGui::TableNextColumn()) number_cell(a.cs2.cs2_player_level);
    if (ImGui::TableNextColumn()) number_cell(a.web.steam_level);
    if (ImGui::TableNextColumn()) number_cell(a.web.owned_games_count);
    if (ImGui::TableNextColumn() && a.cs2.prime_status) text_cell("Prime");
    if (ImGui::TableNextColumn() && kit::weekly_drop_claimed(a, now)) {
        kit::RowCell cell;
        facet::Badge("Claimed", {.tone = facet::Tone::Success, .icon = icons::weekly_drop});
    }
    if (ImGui::TableNextColumn()) {
        if (a.last_login_unix > 0) text_cell(format_relative(a.last_login_unix).c_str());
        else text_cell("Never", true);
    }
    if (ImGui::TableNextColumn() && a.funds.total_spend_usd_cents >= 0) {
        const std::string text = spend_cell(a);
        const float w = facet::MeasureText(facet::TextStyle::Body, text.c_str()).x;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
        text_cell(text.c_str());
    }
    if (ImGui::TableNextColumn() && a.trust != core::TrustLabel::Unset) {
        switch (a.trust) {
            case core::TrustLabel::Green: text_cell("Green"); break;
            case core::TrustLabel::Yellow: text_cell("Yellow"); break;
            case core::TrustLabel::Red: text_cell("Red"); break;
            case core::TrustLabel::Unset: break;
        }
    }
}

// Column widths in units and the order in which they give way when the table is narrow (lower
// priority goes first). Account always stays.
struct ColumnSpec {
    const char* label;
    float width;
    facet::Align align;
    int priority;
};

constexpr ColumnSpec kColumns[kColumnCount] = {
    {"Account", 220.0F, facet::Align::Start, 100},
    {"Standing", 132.0F, facet::Align::Start, 90},
    {"Cooldown", 112.0F, facet::Align::Start, 85},
    {"Premier", 76.0F, facet::Align::End, 80},
    {"Wingman", 76.0F, facet::Align::End, 40},
    {"CS2 level", 76.0F, facet::Align::End, 50},
    {"Steam level", 84.0F, facet::Align::End, 45},
    {"Games", 64.0F, facet::Align::End, 20},
    {"Prime", 64.0F, facet::Align::Start, 60},
    {"Weekly drop", 100.0F, facet::Align::Start, 55},
    {"Last sign-in", 108.0F, facet::Align::Start, 70},
    {"Spent", 88.0F, facet::Align::End, 30},
    {"Trust", 72.0F, facet::Align::Start, 25},
};

ImGuiTableColumnFlags shown_if(bool on) {
    return on ? ImGuiTableColumnFlags_None : ImGuiTableColumnFlags_Disabled;
}

// Why columns give way instead of scrolling sideways: the inspector takes part of the width, and
// a table whose columns run under it hides what the user came to compare. The columns that do not
// fit drop out lowest priority first; the header menu can still show any of them.
void draw_table(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    const auto& info = state.settings.info;
    const bool any_ban = info.show_vac || info.show_game_ban || info.show_trade_ban || info.show_community_ban ||
                         info.show_vac_live;
    bool enabled[kColumnCount] = {true,
                                  any_ban,
                                  info.show_cooldown,
                                  info.show_premier,
                                  info.show_wingman,
                                  true,
                                  info.show_steam_level,
                                  info.show_owned_games,
                                  info.show_prime,
                                  info.show_weekly_drop,
                                  true,
                                  info.show_external_funds,
                                  true};
    float used = 0.0F;
    for (int i = 0; i < kColumnCount; ++i) {
        if (enabled[i]) used += kColumns[i].width + 12.0F;
    }
    const float room = facet::AvailableWidthUnits() - 8.0F;
    while (used > room) {
        int drop = -1;
        for (int i = 1; i < kColumnCount; ++i) {
            if (enabled[i] && (drop < 0 || kColumns[i].priority < kColumns[drop].priority)) drop = i;
        }
        if (drop < 0) break;
        enabled[drop] = false;
        used -= kColumns[drop].width + 12.0F;
    }
    facet::DataTableColumn columns[kColumnCount];
    for (int i = 0; i < kColumnCount; ++i) {
        columns[i].label = kColumns[i].label;
        columns[i].width = kColumns[i].width;
        columns[i].align = kColumns[i].align;
        columns[i].flags = i == 0 ? (ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide)
                                  : shown_if(enabled[i]);
    }
    columns[0].width = 0.0F;  // Account stretches into the room the others leave

    TableRows rows;
    rows.accounts = visible;
    sort_rows(state, rows.accounts, s.table_sort_column, s.table_sort_descending);
    auto rebuild_keys = [&] {
        rows.keys.clear();
        for (std::size_t idx : rows.accounts) rows.keys.push_back(account_key(state.vault.accounts[idx].id));
    };
    rebuild_keys();

    load_selection(s.table_selection, state);
    s.table_selection.UserData = &rows;
    s.table_selection.AdapterIndexToStorageId = &table_key;

    const std::int64_t now = now_unix();
    if (facet::BeginDataTable("##accounts-table", columns, static_cast<int>(rows.accounts.size()), &s.table_selection,
                              {.sortable = true, .hideable = true, .frozenColumns = 1})) {
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                s.table_sort_column = specs->Specs[0].ColumnIndex;
                s.table_sort_descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            sort_rows(state, rows.accounts, s.table_sort_column, s.table_sort_descending);
            rebuild_keys();
            specs->SpecsDirty = false;
        }
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                core::Account& a = state.vault.accounts[rows.accounts[static_cast<std::size_t>(i)]];
                ImGui::PushID(a.id.c_str());
                if (facet::DataTableRow(i)) s.inspector_open = true;
                if (facet::BeginContextMenu("##account-menu")) {
                    account_menu_items(state, a);
                    facet::EndContextMenu();
                }
                cells(state, a, now);
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
    s.table_selection.UserData = nullptr;

    std::unordered_map<ImGuiID, std::size_t> account_of;
    for (std::size_t i = 0; i < state.vault.accounts.size(); ++i) account_of[account_key(state.vault.accounts[i].id)] = i;
    std::vector<std::string> ids;
    void* it = nullptr;
    ImGuiID id = 0;
    while (s.table_selection.GetNextSelectedItem(&it, &id)) {
        if (const auto found = account_of.find(id); found != account_of.end()) {
            ids.push_back(state.vault.accounts[found->second].id);
        }
    }
    if (store_selection(state, ids)) s.inspector_open = true;
}

}  // namespace

// Why an inspector pane and not a list-detail split: the table needs the width for its
// columns, and the detail is a side panel the user can close or resize.
void draw_table_view(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    facet::BeginInspectorPane("Account###inspector", &s.inspector_open, {.paneWidth = 380.0F, .minPaneWidth = 300.0F});
    draw_table(state, visible);
    if (facet::InspectorPaneNext()) {
        if (facet::BeginScrollArea("##inspector-scroll")) {
            begin_padded("##inspector-body");
            draw_detail(state, visible);
            end_padded();
            facet::EndScrollArea();
        }
    }
    facet::EndInspectorPane();
}

}  // namespace sam::ui::screens::accounts
