#include "ui/screens/confirmations/view.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/media.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/transient.hpp"
#include "ui/util.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::screens::confirmations_view {

using namespace confirmations_detail;

ViewState& view() {
    static ViewState v;
    return v;
}

void reset_transient(bool account_keyed) {
    ViewState& v = view();
    v.decision = Decision{};
    v.history_open = false;
    v.history_query.clear();
    v.scan_open = false;
    v.selection.Clear();
    v.detail_key = 0;
    v.drawer_open = false;
    v.overview_open = false;
    v.last_selected = 0;
    if (account_keyed) v.account_filter.clear();
}

namespace {

void reset_on_lock(app::AppState&) {
    reset_transient(true);
}

void reset_on_leave(app::AppState&) {
    reset_transient(false);
}

}  // namespace

Kind kind_of(sda::ConfirmationType type) {
    switch (type) {
        case sda::ConfirmationType::Trade: return Kind::Trade;
        case sda::ConfirmationType::MarketListing: return Kind::Market;
        case sda::ConfirmationType::PhoneNumberChange:
        case sda::ConfirmationType::AccountRecovery:
        case sda::ConfirmationType::FeatureOptOut: return Kind::AccountChange;
        case sda::ConfirmationType::Unknown:
        default: return Kind::Other;
    }
}

bool risky(sda::ConfirmationType type) {
    return type == sda::ConfirmationType::AccountRecovery || type == sda::ConfirmationType::PhoneNumberChange;
}

const char* type_title(sda::ConfirmationType type, bool short_form) {
    switch (type) {
        case sda::ConfirmationType::Trade: return "Trade";
        case sda::ConfirmationType::MarketListing: return short_form ? "Listing" : "Market listing";
        case sda::ConfirmationType::PhoneNumberChange: return short_form ? "Phone" : "Phone number change";
        case sda::ConfirmationType::AccountRecovery: return short_form ? "Recovery" : "Account recovery";
        case sda::ConfirmationType::FeatureOptOut: return short_form ? "Opt-out" : "Feature opt-out";
        case sda::ConfirmationType::Unknown:
        default: return "Other";
    }
}

// Icons name the kind at a glance in a column of mixed rows: the app's trade glyph for trades,
// "upload" for putting an item up for sale, "user" and "unlock" for changes to the account.
facet::Icon type_icon(sda::ConfirmationType type) {
    switch (type) {
        case sda::ConfirmationType::Trade: return icons::trade;
        case sda::ConfirmationType::MarketListing: return facet::Icon("upload");
        case sda::ConfirmationType::PhoneNumberChange: return facet::Icon("user");
        case sda::ConfirmationType::AccountRecovery: return facet::Icon("unlock");
        case sda::ConfirmationType::FeatureOptOut: return facet::Icon("settings");
        case sda::ConfirmationType::Unknown:
        default: return facet::Icon("help");
    }
}

ImGuiID row_key(const std::string& account_id, const std::string& conf_id) {
    const std::size_t h = std::hash<std::string>{}(account_id + '\n' + conf_id);
    const auto id = static_cast<ImGuiID>(h ^ (h >> 32));
    return id != 0 ? id : 1;
}

std::string age_text(std::int64_t unix_seconds, std::int64_t now) {
    if (unix_seconds <= 0) return {};
    const std::int64_t d = now - unix_seconds;
    if (d < 60) return "Just now";
    if (d < 3600) return kit::format_count(d / 60) + " min ago";
    if (d < 86400) return kit::format_count(d / 3600) + " h ago";
    if (d < 86400 * 30) {
        const std::int64_t days = d / 86400;
        return kit::format_count(days) + (days == 1 ? " day ago" : " days ago");
    }
    return format_date(unix_seconds);
}

std::string date_time_text(std::int64_t unix_seconds) {
    if (unix_seconds <= 0) return {};
    const auto t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string account_name(const app::AppState& state, const std::string& account_id, const std::string& fallback_login) {
    for (const core::Account& a : state.vault.accounts) {
        if (a.id == account_id) return kit::display_name(state, a);
    }
    if (state.settings.privacy_mode || fallback_login.empty()) return kit::kHiddenLogin;
    return fallback_login;
}

namespace {

struct Row {
    const core::Account* account = nullptr;
    sda::Confirmation conf;
    ImGuiID key = 0;
    std::string name;  // display name, for sorting
};

enum class Fix { None, LinkMafile, SignIn, Retry };

struct Problem {
    const core::Account* account = nullptr;
    facet::Tone tone = facet::Tone::Destructive;
    const char* title = "";
    std::string detail;  // what happened and what to do, in plain words
    std::string raw;     // the worker's own text, for the tooltip; empty for none
    Fix fix = Fix::None;
    bool refreshing = false;
};

// What the workers know right now, copied once per frame so nothing is drawn under the lock.
struct Snapshot {
    std::vector<Row> rows;
    std::vector<Problem> problems;
    std::vector<const core::Account*> refreshing;
    int guarded_accounts = 0;  // accounts with an authenticator
    int checked_accounts = 0;  // of those, accounts with a pending list
    int failed_accounts = 0;   // an error and nothing pending
    std::int64_t last_check = 0;
};

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

std::optional<Problem> problem_of(const app::AppState& state, const core::Account& a, const PendingList* pl, bool paused) {
    Problem p;
    p.account = &a;
    p.refreshing = pl != nullptr && pl->refreshing;
    const std::string error = pl != nullptr ? pl->last_error : std::string();
    p.raw = error;
    if (!error.empty() && needs_mafile_data(a)) {
        p.tone = facet::Tone::Warning;
        p.title = "Needs maFile data";
        p.detail = "The authenticator is missing the secrets confirmations need. Link this account's maFile.";
        p.fix = Fix::LinkMafile;
        return p;
    }
    if (paused) {
        p.tone = facet::Tone::Warning;
        p.title = "Paused";
        p.detail = "Its sign-in failed " + kit::format_count(state.settings.confirmations.permanent_failure_threshold) +
                   " times in a row, so Refresh all accounts skips it. Try it once by hand.";
        p.fix = Fix::Retry;
        return p;
    }
    if (error.empty()) return std::nullopt;
    if (contains(error, "no stored password")) {
        p.title = "Signed out";
        p.detail = "The session ended and no password is stored, so the app cannot sign in again on its own. "
                   "Sign in again.";
        p.fix = Fix::SignIn;
    } else if (error.rfind("auto-relogin failed", 0) == 0) {
        p.title = "Sign-in failed";
        p.detail = "The app could not sign in again on its own. Sign in again with the full login.";
        p.fix = Fix::SignIn;
    } else if (contains(error, "rate-limited") || contains(error, "429")) {
        p.tone = facet::Tone::Warning;
        p.title = "Steam asked to wait";
        p.detail = "Steam is limiting requests from this PC for a moment. Wait a minute, then try again.";
        p.fix = Fix::Retry;
    } else if (error.rfind("maFile", 0) == 0) {
        p.title = "maFile not linked";
        p.detail = "The chosen file could not be used for this account. Pick this account's own maFile.";
        p.fix = Fix::LinkMafile;
    } else {
        p.title = "Could not load";
        p.detail = "Steam did not send this account's confirmations. Try again in a moment.";
        p.fix = Fix::Retry;
    }
    return p;
}

Snapshot take_snapshot(const app::AppState& state) {
    Snapshot s;
    std::unordered_map<std::string, PendingList> lists;
    {
        std::lock_guard lk(g_mtx);
        lists = g_lists;
    }
    for (const core::Account& a : state.vault.accounts) {
        if (!a.sda.has_value()) continue;
        ++s.guarded_accounts;
        if (const auto it = state.conf_last_refresh_unix.find(a.id); it != state.conf_last_refresh_unix.end()) {
            s.last_check = std::max(s.last_check, it->second);
        }
        const bool paused = state.conf_permanent_failure.count(a.id) != 0;
        const auto it = lists.find(a.id);
        const PendingList* pl = it != lists.end() ? &it->second : nullptr;
        if (auto p = problem_of(state, a, pl, paused)) s.problems.push_back(std::move(*p));
        if (pl == nullptr) continue;
        ++s.checked_accounts;
        if (pl->refreshing) s.refreshing.push_back(&a);
        if (!pl->last_error.empty() && pl->items.empty() && !pl->refreshing) ++s.failed_accounts;
        if (pl->items.empty()) continue;
        const std::string name = kit::display_name(state, a);
        for (const auto& c : pl->items) s.rows.push_back({&a, c, row_key(a.id, c.id), name});
    }
    return s;
}

std::string plural(int n, const char* one, const char* many) {
    return kit::format_count(n) + " " + (n == 1 ? one : many);
}

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

bool sort_less(const Row& a, const Row& b, int column) {
    switch (column) {
        case 0:
            if (a.name != b.name) return a.name < b.name;
            break;
        case 1:
            if (a.conf.headline != b.conf.headline) return a.conf.headline < b.conf.headline;
            break;
        default: break;
    }
    return a.conf.creation_unix < b.conf.creation_unix;
}

void sort_rows(std::vector<Row>& rows, int column, bool ascending) {
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        return ascending ? sort_less(a, b, column) : sort_less(b, a, column);
    });
}

std::vector<Pick> picks_of(const std::vector<const Row*>& rows) {
    std::vector<Pick> picks;
    picks.reserve(rows.size());
    for (const Row* r : rows) picks.push_back({r->account->id, r->conf});
    return picks;
}

bool auto_approve_on(const app::AppState& state) {
    const auto& c = state.settings.confirmations;
    return c.auto_approve_enabled && (c.auto_approve_market || c.auto_approve_phone_change);
}

float rest_height_units() {
    return std::max(0.0F, ImGui::GetContentRegionAvail().y / facet::Scale());
}

// --- Header -----------------------------------------------------------------------------------

void refresh_button(app::AppState& state, const ViewState& v, const Snapshot& snap) {
    char label[64];
    bool disabled = false;
    bool loading = false;
    std::string tip;
    const core::Account* only = v.account_filter.empty() ? nullptr : state.find_account(v.account_filter);
    if (only != nullptr) {
        loading = std::find(snap.refreshing.begin(), snap.refreshing.end(), only) != snap.refreshing.end();
        const auto cd = refresh_cooldown_remaining(state, only->id);
        if (cd > 0) {
            std::snprintf(label, sizeof(label), "Refresh in %lld s###refresh", static_cast<long long>(cd));
            disabled = true;
            tip = "Each account can be checked again a few seconds after its last check.";
        } else {
            std::snprintf(label, sizeof(label), "Refresh this account###refresh");
        }
        ImGui::BeginDisabled(disabled);
        if (facet::Button(label, {.icon = facet::Icon("refresh"), .loading = loading})) {
            submit_account_refresh(state, *only);
        }
        ImGui::EndDisabled();
    } else {
        const int total = state.conf_refresh_all_total.load(std::memory_order_relaxed);
        const int done = state.conf_refresh_all_done.load(std::memory_order_relaxed);
        loading = total > 0 && done < total;
        int eligible = 0;
        int ready = 0;
        std::int64_t soonest = 0;
        for (const core::Account& a : state.vault.accounts) {
            if (!a.sda.has_value() || state.conf_permanent_failure.count(a.id) != 0) continue;
            ++eligible;
            const auto cd = refresh_cooldown_remaining(state, a.id);
            if (cd == 0) {
                ++ready;
            } else if (soonest == 0 || cd < soonest) {
                soonest = cd;
            }
        }
        if (eligible > 0 && ready == 0) {
            std::snprintf(label, sizeof(label), "Refresh in %lld s###refresh", static_cast<long long>(soonest));
            disabled = true;
            tip = "Every account was checked moments ago. Each one can be checked again after its cooldown.";
        } else {
            std::snprintf(label, sizeof(label), "Refresh all accounts###refresh");
            if (eligible == 0) {
                disabled = true;
                tip = "Every account is paused after repeated sign-in failures. Try them one by one under Needs "
                      "attention.";
            } else if (ready < eligible) {
                tip = plural(eligible - ready, "account was", "accounts were") +
                      " checked moments ago and will be skipped.";
            }
        }
        ImGui::BeginDisabled(disabled);
        if (facet::Button(label, {.icon = facet::Icon("refresh"), .loading = loading})) refresh_all(state);
        ImGui::EndDisabled();
    }
    if (!tip.empty()) facet::ItemTooltip("%s", tip.c_str());
}

void draw_header(app::AppState& state, ViewState& v, const Snapshot& snap) {
    // The context line says each fact once: the count while something waits, otherwise when the
    // last check finished, and which accounts are being checked right now.
    std::string context;
    if (!snap.rows.empty()) {
        std::unordered_set<const core::Account*> accounts;
        for (const Row& r : snap.rows) accounts.insert(r.account);
        context = kit::format_count(static_cast<std::int64_t>(snap.rows.size())) + " waiting on " +
                  plural(static_cast<int>(accounts.size()), "account", "accounts");
    } else if (snap.last_check > 0 && snap.refreshing.empty()) {
        const std::string age = age_text(snap.last_check, now_seconds());
        context = "Last checked " + (age == "Just now" ? std::string("just now") : age);
    }
    if (!snap.refreshing.empty()) {
        if (!context.empty()) context += ". ";
        context += "Checking " + (snap.refreshing.size() == 1
                                      ? kit::display_name(state, *snap.refreshing.front())
                                      : plural(static_cast<int>(snap.refreshing.size()), "account", "accounts"));
    }

    kit::PageHeaderScope header("Confirmations", {.context = context.empty() ? nullptr : context.c_str()});
    refresh_button(state, v, snap);
    if (facet::Button("History", {.icon = facet::Icon("clock")})) v.history_open = true;
    facet::ItemTooltip("Every approval and denial, including automatic ones");
    const bool scanning = g_scan_in_progress.load();
    if (facet::Button("Scan folder for maFiles", {.icon = facet::Icon("folder"), .loading = scanning})) {
        scan_folder_for_mafiles(state);
    }
    facet::ItemTooltip("Links maFiles from a folder to accounts that are missing their authenticator secrets");
}

// --- Filter row and bulk row ------------------------------------------------------------------

void select_all_checkbox(ViewState& v, const std::vector<Row>& shown, int n_selected) {
    facet::CheckState all = n_selected == 0                                  ? facet::CheckState::Off
                            : n_selected == static_cast<int>(shown.size()) ? facet::CheckState::On
                                                                             : facet::CheckState::Mixed;
    ImGui::BeginDisabled(shown.empty());
    if (facet::Checkbox("Select all", &all)) {
        if (all == facet::CheckState::On) {
            for (const Row& r : shown) v.selection.SetItemSelected(r.key, true);
        } else {
            v.selection.Clear();
        }
    }
    ImGui::EndDisabled();
    facet::ItemTooltip("Select rows with their checkboxes, Ctrl or Shift, or Ctrl+A in the table");
}

// The narrow layout's way to the overview: a button that says what needs a look.
void overview_button(app::AppState& state, ViewState& v, const Snapshot& snap, float width_units);

// Why one row: the filters narrow the table right under them, and selecting acts on what they
// leave. In the narrow layout the overview hangs off its end.
void draw_filter_bar(app::AppState& state, ViewState& v, const Snapshot& snap, const std::vector<Row>& account_rows,
                     const std::vector<Row>& shown, int n_selected, bool wide, float row_w) {
    const facet::Theme& t = facet::T();
    const float start_x = ImGui::GetCursorPosX();
    select_all_checkbox(v, shown, n_selected);
    ImGui::SameLine(0.0F, facet::Px(t.spacing.lg));
    if (state.settings.confirmations.show_account_search) {
        kit::account_picker("##account", state, &v.account_filter,
                            {.filter = &kit::has_authenticator,
                             .placeholder = "All accounts",
                             .empty_text = "No account has an authenticator",
                             .width = 200.0F});
        if (!v.account_filter.empty()) {
            ImGui::SameLine(0.0F, facet::Px(t.spacing.xs));
            if (facet::IconButton("Show all accounts", facet::Icon("close"))) v.account_filter.clear();
        }
        ImGui::SameLine(0.0F, facet::Px(t.spacing.md));
    }

    // Chips with nothing behind them are left out; a chip still selected stays so it can be undone.
    int counts[kKindCount] = {};
    for (const Row& r : account_rows) ++counts[static_cast<int>(kind_of(r.conf.type))];
    static const char* const kNames[kKindCount] = {"Trades", "Market listings", "Account changes", "Other"};
    std::string details[kKindCount];
    facet::ChoiceItem items[kKindCount];
    bool* flags[kKindCount] = {};
    bool shown_flags[kKindCount] = {};
    int n_items = 0;
    for (int i = 0; i < kKindCount; ++i) {
        if (counts[i] == 0 && !v.kinds[i]) continue;
        details[i] = kit::format_count(counts[i]);
        items[n_items] = {.label = kNames[i], .detail = details[i].c_str()};
        flags[n_items] = &v.kinds[i];
        shown_flags[n_items] = v.kinds[i];
        ++n_items;
    }

    const bool problems = !snap.problems.empty() || auto_approve_on(state);
    const float button_w = (!wide && problems) ? 176.0F : 0.0F;
    const float used_w = (ImGui::GetCursorPosX() - start_x) / facet::Scale();
    const float chips_w = row_w - used_w - button_w - (button_w > 0.0F ? t.spacing.sm : 0.0F);
    if (n_items > 0 && chips_w > 0.0F) {
        if (facet::ChipGroup("##kinds", std::span<bool>(shown_flags, static_cast<std::size_t>(n_items)),
                             std::span<const facet::ChoiceItem>(items, static_cast<std::size_t>(n_items)),
                             {.singleLine = true, .width = chips_w})) {
            for (int i = 0; i < n_items; ++i) *flags[i] = shown_flags[i];
        }
    }
    if (button_w > 0.0F) {
        ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
        overview_button(state, v, snap, button_w);
    }
}

// Why the bulk row takes the filter row's place in the narrow layout: there is no side column
// to hold the decision, and the row keeps the filter row's height so the table does not move.
void draw_bulk_row(app::AppState& state, ViewState& v, const std::vector<const Row*>& selected) {
    const int n = static_cast<int>(selected.size());
    facet::BeginStack("##bulk", {.orientation = facet::Orientation::Horizontal,
                                 .align = facet::LayoutAlign::Center,
                                 .width = facet::AvailableWidthUnits()});
    const std::string clear = "Clear selection (" + kit::format_count(n) + ")###clear-selection";
    if (facet::Button(clear.c_str(), {.variant = facet::ButtonVariant::Plain, .icon = facet::Icon("close")})) {
        v.selection.Clear();
    }
    facet::StackSpacer();
    const std::string deny = "Deny selected (" + kit::format_count(n) + ")###deny-selected";
    const std::string approve = "Approve selected (" + kit::format_count(n) + ")###approve-selected";
    if (kit::quiet_destructive_button(deny.c_str(), facet::Icon("close"))) ask_decision(state, picks_of(selected), false);
    if (facet::Button(approve.c_str(), {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("check")})) {
        ask_decision(state, picks_of(selected), true);
    }
    facet::EndStack();
}

// --- The table --------------------------------------------------------------------------------

const facet::DataTableColumn kColumns[] = {
    {.label = "Account", .width = 176.0F},
    {.label = "Details"},
    {.label = "Created",
     .width = 80.0F,
     .flags = ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending},
    {.label = "##actions", .width = 124.0F, .flags = ImGuiTableColumnFlags_NoSort},
};

float table_row_units() {
    const facet::Theme& t = facet::T();
    return t.list.rowHeightLarge + t.spacing.sm;
}

// Why a sortable multi-select table: decisions are made across accounts, in bulk when they are
// routine, so every pending confirmation is one row with its account, what it is and its age.
// The kind leads the second line of the description, so the description keeps the width.
void draw_table(app::AppState& state, ViewState& v, std::vector<Row>& shown, facet::DataState data_state,
                float width_units, float height_units) {
    v.selection.UserData = &shown;
    v.selection.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self, int index) -> ImGuiID {
        const auto* rows = static_cast<const std::vector<Row>*>(self->UserData);
        if (rows == nullptr || index < 0 || index >= static_cast<int>(rows->size())) return 0;
        return (*rows)[static_cast<std::size_t>(index)].key;
    };
    const std::int64_t now = now_seconds();
    if (facet::BeginDataTable("##pending", kColumns, static_cast<int>(shown.size()), &v.selection,
                              {.sortable = true, .rowHeight = table_row_units(), .state = data_state,
                               .width = width_units, .height = height_units})) {
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                v.sort_column = specs->Specs[0].ColumnIndex;
                v.sort_ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
            specs->SpecsDirty = false;
            sort_rows(shown, v.sort_column, v.sort_ascending);
        }
        facet::RowRange rows;
        while (facet::NextVisibleRows(&rows)) {
            for (int i = rows.first; i < rows.last; ++i) {
                const Row& r = shown[static_cast<std::size_t>(i)];
                ImGui::PushID(static_cast<int>(r.key));
                if (facet::DataTableRow(i)) v.detail_key = r.key;

                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    bool picked = v.selection.Contains(r.key);
                    if (facet::Checkbox("##pick", &picked)) v.selection.SetItemSelected(r.key, picked);
                    facet::ItemTooltip("Select");
                    cell.next();
                    kit::account_row(state, *r.account);
                }

                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    std::string second = type_title(r.conf.type);
                    if (!r.conf.summary.empty()) second += " \xC2\xB7 " + r.conf.summary;
                    facet::BeginStack("##what", {.gap = facet::Gap::None});
                    facet::TruncatedText(r.conf.headline.c_str());
                    facet::TruncatedText(second.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
                    facet::EndStack();
                }

                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    const std::string age = age_text(r.conf.creation_unix, now);
                    facet::StyledText(age.c_str(), {.secondary = true});
                    facet::ItemTooltip("%s", date_time_text(r.conf.creation_unix).c_str());
                }

                ImGui::TableNextColumn();
                {
                    kit::RowCell cell({.gap = facet::Gap::Loose});
                    if (facet::Button("Approve", {.icon = facet::Icon("check"), .size = facet::ControlSize::Small})) {
                        if (risky(r.conf.type)) {
                            ask_decision(state, {{r.account->id, r.conf}}, true);
                        } else {
                            respond(state, {{r.account->id, r.conf}}, true);
                        }
                    }
                    cell.next();
                    if (facet::IconButton("Deny", facet::Icon("close"),
                                          {.variant = facet::ButtonVariant::Secondary,
                                           .size = facet::ControlSize::Small})) {
                        ask_decision(state, {{r.account->id, r.conf}}, false);
                    }
                }
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
    v.selection.UserData = nullptr;
}

// --- Context: overview, one confirmation, a selection -----------------------------------------

void fix_button(app::AppState& state, const Problem& p) {
    const core::Account& a = *p.account;
    switch (p.fix) {
        case Fix::LinkMafile:
            if (facet::Button("Link maFile", {.size = facet::ControlSize::Small, .loading = p.refreshing})) {
                link_mafile_for_account(state, a);
            }
            break;
        case Fix::SignIn:
            if (facet::Button("Sign in again", {.size = facet::ControlSize::Small})) {
                kit::go_to(state, app::Screen::AddAccount);
            }
            facet::ItemTooltip("Opens Add account, where the full login stores a new session");
            break;
        case Fix::Retry: {
            const auto cd = refresh_cooldown_remaining(state, a.id);
            char label[48];
            if (cd > 0) {
                std::snprintf(label, sizeof(label), "Try again in %lld s###retry", static_cast<long long>(cd));
            } else {
                std::snprintf(label, sizeof(label), "Try again###retry");
            }
            ImGui::BeginDisabled(cd > 0);
            if (facet::Button(label, {.size = facet::ControlSize::Small, .loading = p.refreshing})) {
                submit_account_refresh(state, a);
            }
            ImGui::EndDisabled();
            break;
        }
        case Fix::None: break;
    }
}

// Why a hint first and then the problems: the column tells a first-time user how to use the
// table, and each problem is an account with its own fix, grouped under one header.
void draw_overview(app::AppState& state, ViewState& v, const Snapshot& snap, bool with_hint) {
    const facet::Theme& t = facet::T();
    if (with_hint) {
        facet::StyledText("Select a confirmation to see what it is, or tick several to approve or deny them "
                          "together.",
                          {.secondary = true, .wrap = true});
    }
    if (!snap.problems.empty()) {
        if (with_hint) gap(t.spacing.lg);
        const std::string count =
            plural(static_cast<int>(snap.problems.size()), "account needs attention", "accounts need attention");
        (void)facet::SectionHeader("Needs attention", {.description = count.c_str(), .divider = true});
        for (std::size_t i = 0; i < snap.problems.size(); ++i) {
            const Problem& p = snap.problems[i];
            ImGui::PushID(p.account->id.c_str());
            if (i > 0) {
                gap(t.spacing.xs);
                facet::Separator();
                gap(t.spacing.xs);
            }
            kit::account_row(state, *p.account,
                             {.layout = kit::AccountRowOptions::Layout::OneLine, .show_login = false});
            facet::StatusPill(p.title, {.tone = p.tone, .size = facet::ControlSize::Small});
            facet::StyledText(p.detail.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
            if (!p.raw.empty()) facet::ItemTooltip("Details: %s", p.raw.c_str());
            fix_button(state, p);
            ImGui::PopID();
        }
    }
    if (auto_approve_on(state)) {
        const auto& c = state.settings.confirmations;
        gap(t.spacing.lg);
        (void)facet::SectionHeader("Auto-approve is on", {.divider = true});
        const char* message = c.auto_approve_market && c.auto_approve_phone_change
                                  ? "Market listings and phone number changes are approved automatically as soon as "
                                    "they are found. Each one is recorded in history."
                              : c.auto_approve_market
                                  ? "Market listings are approved automatically as soon as they are found. Each one "
                                    "is recorded in history."
                                  : "Phone number changes are approved automatically as soon as they are found. Each "
                                    "one is recorded in history.";
        facet::StyledText(message, {.wrap = true});
        gap(t.spacing.sm);
        if (facet::Link("Change in settings")) kit::open_settings(state, SettingsCategory::SteamGuard);
        ImGui::SameLine(0.0F, facet::Px(t.spacing.lg));
        if (facet::Link("Show history")) v.history_open = true;
    }
}

void overview_button(app::AppState& state, ViewState& v, const Snapshot& snap, float width_units) {
    std::string label;
    if (!snap.problems.empty()) {
        label = plural(static_cast<int>(snap.problems.size()), "needs attention", "need attention");
    } else {
        label = "Auto-approve is on";
    }
    label += "###overview";
    if (facet::Button(label.c_str(), {.icon = facet::Icon("warning"), .width = width_units})) {
        v.overview_open = !v.overview_open;
    }
    if (facet::BeginPopover("##overview-popover", &v.overview_open,
                            {.placement = facet::PopoverPlacement::Below, .width = 320.0F})) {
        draw_overview(state, v, snap, false);
        facet::EndPopover();
    }
}

// Why the decision for a selection reads as one block: the count, what is included and the two
// outcomes, with Approve as the view's one filled button and Deny quiet beside it.
void draw_selection(app::AppState& state, ViewState& v, const std::vector<const Row*>& selected) {
    const facet::Theme& t = facet::T();
    const int n = static_cast<int>(selected.size());
    std::unordered_set<const core::Account*> accounts;
    int counts[kKindCount] = {};
    int risky_n = 0;
    for (const Row* r : selected) {
        accounts.insert(r->account);
        ++counts[static_cast<int>(kind_of(r->conf.type))];
        if (risky(r->conf.type)) ++risky_n;
    }
    facet::Text(facet::TextStyle::Title3, "%s selected", kit::format_count(n).c_str());
    static const char* const kOne[kKindCount] = {"trade", "market listing", "account change", "other"};
    static const char* const kMany[kKindCount] = {"trades", "market listings", "account changes", "others"};
    std::vector<std::string> parts;
    for (int i = 0; i < kKindCount; ++i) {
        if (counts[i] > 0) parts.push_back(plural(counts[i], kOne[i], kMany[i]));
    }
    std::string what;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) what += i + 1 == parts.size() ? " and " : ", ";
        what += parts[i];
    }
    what += " on " + plural(static_cast<int>(accounts.size()), "account.", "accounts.");
    facet::StyledText(what.c_str(), {.secondary = true, .wrap = true});
    if (risky_n > 0) {
        gap(t.spacing.sm);
        facet::StyledText("Account recoveries and phone number changes are left out of a bulk approve unless you "
                          "include them in the next step.",
                          {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    }
    gap(t.spacing.lg);
    const std::string approve = "Approve selected (" + kit::format_count(n) + ")###approve-selected";
    const std::string deny = "Deny selected (" + kit::format_count(n) + ")###deny-selected";
    if (facet::Button(approve.c_str(), {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("check")})) {
        ask_decision(state, picks_of(selected), true);
    }
    gap(t.spacing.sm);
    if (kit::quiet_destructive_button(deny.c_str(), facet::Icon("close"))) ask_decision(state, picks_of(selected), false);
    gap(t.spacing.sm);
    if (facet::Button("Clear selection", {.variant = facet::ButtonVariant::Plain})) v.selection.Clear();
}

// Why the decision sits right under the description: the column answers "what am I approving"
// and lets the user act before the facts of where it came from.
void draw_detail(app::AppState& state, const Row& r) {
    const facet::Theme& t = facet::T();
    const sda::Confirmation& c = r.conf;
    if (!c.icon_url.empty()) {
        if (ID3D11ShaderResourceView* tex = widgets::texture_for(c.icon_url)) {
            facet::ImageView("Item picture", tex,
                             {.fit = facet::ImageFit::Contain, .corners = facet::Radius::Element, .showStates = false,
                              .width = 64.0F, .height = 64.0F});
        }
    }
    facet::Badge(type_title(c.type), {.icon = type_icon(c.type)});
    facet::StyledText(c.headline.c_str(), {.style = facet::TextStyle::Title3, .wrap = true});
    if (!c.summary.empty()) facet::StyledText(c.summary.c_str(), {.secondary = true, .wrap = true});
    gap(t.spacing.md);
    if (facet::Button("Approve", {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("check")})) {
        if (risky(c.type)) {
            ask_decision(state, {{r.account->id, c}}, true);
        } else {
            respond(state, {{r.account->id, c}}, true);
        }
    }
    ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
    if (kit::quiet_destructive_button("Deny", facet::Icon("close"))) ask_decision(state, {{r.account->id, c}}, false);
    gap(t.spacing.lg);

    const std::string name = kit::display_name(state, *r.account);
    const std::string login = kit::login_text(state, *r.account);
    const std::string created = date_time_text(c.creation_unix) + " (" + age_text(c.creation_unix, now_seconds()) + ")";
    (void)facet::SectionHeader("Where it came from", {.divider = true});
    {
        facet::KeyValueListScope list("##facts-list", {.labelWidth = 88.0F, .copyable = true});
        facet::KeyValue("Account", name.c_str());
        if (login != name) facet::KeyValue("Login", login.c_str());
        if (c.creation_unix > 0) facet::KeyValue("Created", created.c_str());
        facet::KeyValue("Confirmation", c.id.c_str());
        if (!c.creator_id.empty()) {
            const char* what = c.type == sda::ConfirmationType::Trade           ? "Trade offer"
                               : c.type == sda::ConfirmationType::MarketListing ? "Listing"
                                                                                : "Source";
            facet::KeyValue(what, c.creator_id.c_str());
        }
    }
    if (c.type == sda::ConfirmationType::Trade) {
        gap(t.spacing.sm);
        if (facet::Link("See both sides in Trade offers")) kit::go_to(state, app::Screen::TradeOffers);
    }
}

// The main area: the table, or the state that replaces it. `width_units` 0 fills.
void draw_main(app::AppState& state, ViewState& v, const Snapshot& snap, std::vector<Row>& shown, float width_units) {
    const bool loaded = state.pending_confirmations_loaded.load(std::memory_order_relaxed);
    const float h = rest_height_units();
    if (!snap.rows.empty() && shown.empty()) {
        if (kit::empty_state("No confirmations match the filters",
                             {.message = (plural(static_cast<int>(snap.rows.size()), "confirmation is",
                                                 "confirmations are") +
                                          " hidden by the account or type filter.")
                                             .c_str(),
                              .icon = facet::Icon("filter"),
                              .action_label = "Clear filters",
                              .width = width_units,
                              .height = h})) {
            v.account_filter.clear();
            std::fill(std::begin(v.kinds), std::end(v.kinds), false);
        }
    } else if (snap.rows.empty() && (!snap.refreshing.empty() || (!loaded && snap.checked_accounts > 0))) {
        draw_table(state, v, shown, facet::DataState::Loading, width_units, h);
    } else if (snap.rows.empty() && snap.checked_accounts > 0 && snap.failed_accounts == snap.checked_accounts) {
        if (kit::empty_state("No account could be checked",
                             {.message = "Each account's problem and its fix are listed under Needs attention.",
                              .icon = facet::Icon("error"),
                              .action_label = "Try again",
                              .action_icon = facet::Icon("refresh"),
                              .tone = facet::Tone::Destructive,
                              .width = width_units,
                              .height = h})) {
            refresh_all(state);
        }
    } else if (snap.rows.empty()) {
        (void)kit::empty_state("Nothing is waiting for confirmation",
                               {.message = "New trades, listings and account changes show up here when an account "
                                           "is checked.",
                                .icon = icons::confirmations,
                                .width = width_units,
                                .height = h});
    } else {
        draw_table(state, v, shown, facet::DataState::Ready, width_units, h);
    }
}

}  // namespace

// Why a table with a context column beside it: everything pending across accounts is one list
// to decide on, so the table is the hero and gets the page; what belongs to the current
// selection (an overview, one confirmation, or a bulk decision) sits beside it on its own card,
// top-aligned with the table. Below about 960 units the column would squeeze the descriptions,
// so the detail becomes a drawer, the bulk decision takes the filter row's place and the
// overview hangs off a button.
void draw(app::AppState& state) {
    ViewState& v = view();
    {
        std::lock_guard lk(g_mtx);
        if (g_show_scan_modal) {
            g_show_scan_modal = false;
            v.scan_open = true;
        }
    }
    Snapshot snap = take_snapshot(state);

    if (!v.account_filter.empty()) {
        const core::Account* a = state.find_account(v.account_filter);
        if (a == nullptr || !a->sda.has_value() || !state.settings.confirmations.show_account_search) {
            v.account_filter.clear();
        }
    }
    std::vector<Row> account_rows;
    for (const Row& r : snap.rows) {
        if (v.account_filter.empty() || r.account->id == v.account_filter) account_rows.push_back(r);
    }
    const bool any_kind = std::any_of(std::begin(v.kinds), std::end(v.kinds), [](bool b) { return b; });
    std::vector<Row> shown;
    for (const Row& r : account_rows) {
        if (!any_kind || v.kinds[static_cast<int>(kind_of(r.conf.type))]) shown.push_back(r);
    }
    sort_rows(shown, v.sort_column, v.sort_ascending);

    // The selection holds shown rows only, so a bulk action never reaches a hidden one.
    std::vector<const Row*> selected;
    for (const Row& r : shown) {
        if (v.selection.Contains(r.key)) selected.push_back(&r);
    }
    if (static_cast<int>(selected.size()) != v.selection.Size) {
        v.selection.Clear();
        for (const Row* r : selected) v.selection.SetItemSelected(r->key, true);
    }
    const int n_selected = static_cast<int>(selected.size());
    if (n_selected == 1) {
        v.detail_key = selected.front()->key;
    } else if (n_selected == 0 && v.last_selected > 0) {
        v.detail_key = 0;
    }
    v.last_selected = n_selected;
    const Row* detail_row = nullptr;
    for (const Row& r : snap.rows) {
        if (r.key == v.detail_key) detail_row = &r;
    }
    if (detail_row == nullptr) v.detail_key = 0;

    {
        kit::PageScope page("confirmations", {.scroll = false});
        draw_header(state, v, snap);

        if (snap.guarded_accounts == 0) {
            if (kit::empty_state("No account has an authenticator",
                                 {.message = "Confirmations need a linked Steam Guard authenticator. Link one on the "
                                             "Authenticator screen.",
                                  .icon = icons::authenticator,
                                  .action_label = "Open Authenticator",
                                  .height = rest_height_units()})) {
                kit::go_to(state, app::Screen::Authenticator);
            }
        } else {
            const facet::Theme& t = facet::T();
            const float page_w = facet::AvailableWidthUnits();
            const bool wide = page_w >= 960.0F;
            const float column_w = 300.0F;
            const float main_w = wide ? page_w - column_w - t.spacing.lg : page_w;

            // The filter row spans the table's width only, so the context card beside the table
            // starts on the table's top edge.
            if (!wide && n_selected >= 2) {
                draw_bulk_row(state, v, selected);
                gap(t.spacing.md);
            } else if (!snap.rows.empty()) {
                draw_filter_bar(state, v, snap, account_rows, shown, n_selected, wide, main_w);
                gap(t.spacing.md);
            }

            if (wide) {
                const float h = rest_height_units();
                ImGui::BeginGroup();
                draw_main(state, v, snap, shown, main_w);
                ImGui::EndGroup();
                ImGui::SameLine(0.0F, facet::Px(t.spacing.lg));
                const char* title = n_selected >= 2      ? "Selection###context"
                                    : detail_row != nullptr ? "Details###context"
                                                            : "Overview###context";
                facet::BeginCard(title, {.width = column_w, .height = h});
                // The card adds its header and padding around the scroll area, so the scroll area
                // leaves the overhang measured last frame free and the card ends on the table's
                // bottom edge.
                if (facet::BeginScrollArea("##context-scroll", {.height = -std::max(1.0F, v.card_trim)})) {
                    if (n_selected >= 2) {
                        draw_selection(state, v, selected);
                    } else if (detail_row != nullptr) {
                        draw_detail(state, *detail_row);
                    } else {
                        draw_overview(state, v, snap, !snap.rows.empty());
                    }
                    facet::EndScrollArea();
                }
                facet::EndCard();
                const float over = ImGui::GetItemRectSize().y / facet::Scale() - h;
                if (over > 0.5F || over < -0.5F) v.card_trim = std::max(1.0F, v.card_trim + over);
            } else {
                draw_main(state, v, snap, shown, 0.0F);
                const bool want = detail_row != nullptr && n_selected < 2;
                v.drawer_open = want;
                if (facet::BeginDrawer("Details###conf-drawer", &v.drawer_open,
                                       {.side = facet::DrawerSide::End, .extent = 340.0F, .modal = false})) {
                    if (detail_row != nullptr) draw_detail(state, *detail_row);
                    facet::EndDrawer();
                }
                if (want && !v.drawer_open) {
                    v.detail_key = 0;
                    v.selection.Clear();
                }
            }
        }

        draw_decision(state, v);
        draw_history(state, v);
        draw_scan_result(state, v);
    }
}

}  // namespace sam::ui::screens::confirmations_view

SAM_KIT_TRANSIENT_RESET(confirmations_reset, &sam::ui::screens::confirmations_view::reset_on_lock)
SAM_KIT_SCREEN_LEAVE(confirmations_leave, sam::app::Screen::Confirmations,
                     &sam::ui::screens::confirmations_view::reset_on_leave)
