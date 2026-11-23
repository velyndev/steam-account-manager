#include <ctime>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/overlays.h>

#include "core/strings.hpp"
#include "core/trade/trade_audit.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"

namespace sam::ui::screens::trade_offers_ui {

namespace {

namespace trade = core::trade;

struct HistoryRow {
    const trade::TradeAuditEntry* entry = nullptr;
    std::string account;  // as shown: the display name, privacy aware
};

const char* outcome_text(trade::TradeAuditOutcome o) {
    switch (o) {
        case trade::TradeAuditOutcome::Sent: return "Sent";
        case trade::TradeAuditOutcome::SentAndConfirmed: return "Sent and confirmed";
        case trade::TradeAuditOutcome::NeedsConfirmation: return "Needs confirmation";
        case trade::TradeAuditOutcome::Failed: return "Failed";
    }
    return "Unknown";
}

facet::Tone outcome_tone(trade::TradeAuditOutcome o) {
    switch (o) {
        case trade::TradeAuditOutcome::Sent:
        case trade::TradeAuditOutcome::SentAndConfirmed: return facet::Tone::Success;
        case trade::TradeAuditOutcome::NeedsConfirmation: return facet::Tone::Warning;
        case trade::TradeAuditOutcome::Failed: return facet::Tone::Destructive;
    }
    return facet::Tone::Neutral;
}

bool result_matches(int filter, trade::TradeAuditOutcome o) {
    switch (filter) {
        case 1: return o == trade::TradeAuditOutcome::Sent || o == trade::TradeAuditOutcome::SentAndConfirmed;
        case 2: return o == trade::TradeAuditOutcome::NeedsConfirmation;
        case 3: return o == trade::TradeAuditOutcome::Failed;
        default: return true;
    }
}

std::string local_time(std::int64_t unix_s) {
    char buf[24];
    const auto t = static_cast<std::time_t>(unix_s);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string shown_account(const app::AppState& state, const trade::TradeAuditEntry& e) {
    for (const auto& a : state.vault.accounts) {
        if (a.id == e.account_id) return kit::display_name(state, a);
    }
    return state.settings.privacy_mode ? std::string(kit::kHiddenLogin) : e.account_login;
}

}  // namespace

// Why a data table: the audit log is uniform records read by comparing columns, newest first.
void draw_history_tab(app::AppState& state) {
    const facet::Theme& t = facet::T();
    const auto& entries = state.trade_audit.entries();

    if (entries.empty()) {
        kit::empty_state("No offers sent yet",
                         {.message = "Every offer you send from this app is listed here with its result, including "
                                     "the ones that failed.",
                          .icon = facet::Icon("clock"),
                          .height = 260.0F});
        return;
    }

    static const char* const kResults[] = {"All results", "Sent", "Needs confirmation", "Failed"};
    facet::BeginStack("##history-filters", {.orientation = facet::Orientation::Horizontal,
                                            .align = facet::LayoutAlign::Center,
                                            .width = facet::AvailableWidthUnits()});
    facet::SearchField("##history-search", &g_ui.history_query,
                       {.placeholder = "Search account, offer or error", .width = 280.0F});
    facet::Dropdown("##history-result", &g_ui.history_result, kResults, {.width = 200.0F});
    facet::StackSpacer();
    facet::EndStack();
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));

    const std::string query = core::to_lower(g_ui.history_query);
    std::vector<HistoryRow> rows;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const trade::TradeAuditEntry& e = *it;
        if (!g_ui.scope.empty() && e.account_id != g_ui.scope) continue;
        if (!result_matches(g_ui.history_result, e.outcome)) continue;
        HistoryRow row{&e, shown_account(state, e)};
        if (!query.empty() && core::to_lower(row.account).find(query) == std::string::npos &&
            core::to_lower(e.detail).find(query) == std::string::npos && e.offer_id.find(query) == std::string::npos) {
            continue;
        }
        rows.push_back(std::move(row));
    }

    if (rows.empty()) {
        if (kit::empty_state("No entries match",
                             {.message = "Nothing in the history matches the search, the result filter or the "
                                         "account chosen above.",
                              .icon = facet::Icon("filter"),
                              .action_label = "Clear the filters",
                              .height = 220.0F})) {
            g_ui.history_query.clear();
            g_ui.history_result = 0;
            g_ui.scope.clear();
        }
        return;
    }

    // The detail is the column people read, so it takes the flexible width.
    const facet::DataTableColumn cols[] = {
        {.label = "Time", .width = 116.0F},
        {.label = "Account", .width = 150.0F},
        {.label = "Items", .width = 48.0F, .align = facet::Align::End},
        {.label = "Result", .width = 140.0F},
        {.label = "Type", .width = 64.0F},
        {.label = "Detail"},
    };
    if (!facet::BeginDataTable("##history", cols, static_cast<int>(rows.size()), nullptr)) return;
    facet::RowRange range;
    while (facet::NextVisibleRows(&range)) {
        for (int i = range.first; i < range.last; ++i) {
            const HistoryRow& r = rows[static_cast<std::size_t>(i)];
            const trade::TradeAuditEntry& e = *r.entry;
            ImGui::PushID(i);
            facet::DataTableRow(i);
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::StyledText(local_time(e.unix_time).c_str());
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::TruncatedText(r.account.c_str());
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                const std::string items = kit::format_count(e.item_count);
                facet::StyledText(items.c_str(), {.align = facet::Align::End, .width = facet::AvailableWidthUnits()});
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                const std::string result = std::string(outcome_text(e.outcome)) + "###result";
                facet::StatusPill(result.c_str(), {.tone = outcome_tone(e.outcome), .size = facet::ControlSize::Small});
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::StyledText(e.source == trade::TradeAuditSource::UserBulk ? "Bulk" : "Single", {.secondary = true});
                facet::ItemTooltip(e.source == trade::TradeAuditSource::UserBulk
                                       ? "Sent together with other accounts in one run"
                                       : "Sent from one account");
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                std::string detail;
                if (!e.detail.empty()) {
                    detail = human_error(e.detail);
                } else if (!e.offer_id.empty()) {
                    detail = "Offer " + e.offer_id;
                } else {
                    detail = "No detail";
                }
                facet::TruncatedText(detail.c_str(), {.secondary = e.detail.empty()});
                if (!e.detail.empty()) {
                    if (!e.offer_id.empty()) facet::ItemTooltip("Offer %s: %s", e.offer_id.c_str(), e.detail.c_str());
                    else facet::ItemTooltip("%s", e.detail.c_str());
                }
            }
            ImGui::PopID();
        }
    }
    facet::EndDataTable();
}
}  // namespace sam::ui::screens::trade_offers_ui
