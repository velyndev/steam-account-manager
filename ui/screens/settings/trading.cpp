// Settings > Trading: confirming sent offers, the rate limits of the trade endpoints, and the
// saved destination trade links.

#include <algorithm>
#include <array>
#include <string>

#include <imgui.h>

#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>

#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

void draw_offers(Page& p) {
    auto& t = p.s.trade;
    const auto& d = p.d.trade;
    section(p, "Offers");
    toggle_row(p, "Confirm sent offers automatically", &t.auto_confirm_sent, d.auto_confirm_sent,
               "Approves the mobile confirmation of offers you send from this app. Incoming offers always wait "
               "for you.",
               "auto confirm sent offers mobile confirmation trade");
    number_row(p, "Wait between refreshes of one account##trade", &t.per_account_cooldown_seconds,
               d.per_account_cooldown_seconds, 0, 300, "s", "The shortest gap between two offer refreshes of one account.",
               "trade cooldown refresh rate limit");
    number_row(p, "Wait between inventory loads", &t.inventory_cooldown_seconds, d.inventory_cooldown_seconds, 0, 600,
               "s", "Steam limits inventory loads the most, so they get their own, longer wait.",
               "inventory cooldown load rate limit");
    number_row(p, "Delay between accounts when trading", &t.refresh_stagger_ms, d.refresh_stagger_ms, 1500, 10000, "ms",
               "Spreads work over several accounts out, so Steam does not rate limit your IP.",
               "stagger delay accounts milliseconds rate limit");
}

// Units: the header plus up to six rows, so a short list does not leave a tall empty table.
float table_height(int rows) {
    return rows == 0 ? 120.0F : 40.0F + 36.0F * static_cast<float>(std::min(rows, 6));
}

// Why a table for saved links: each link has a name, a URL and two actions, which is a row.
void draw_links(Page& p) {
    auto& t = p.s.trade;
    section(p, "Trade links", "Destinations for offers you send, shared with Trade offers.");
    if (begin_row(p, "Default destination",
                  {.description = "Filled in when you start a new offer.",
                   .keywords = "trade link url destination default partner",
                   .immediate = true})) {
        facet::TextField("##value", &t.default_destination_trade_url,
                         {.placeholder = "https://steamcommunity.com/tradeoffer/new/?partner=...", .clearButton = true});
        if (ImGui::IsItemDeactivatedAfterEdit() || (!ImGui::IsItemActive() && ImGui::IsItemEdited())) {
            commit_now(p.state);
        }
        end_row();
    }

    if (!block(p, "Saved trade links", "The links you saved while sending offers.", "saved trade links list")) return;
    static const facet::DataTableColumn kColumns[] = {
        {.label = "Name", .width = 160.0F}, {.label = "Link"}, {.label = "##actions", .width = 220.0F}};
    int remove = -1;
    int make_default = -1;
    const int rows = static_cast<int>(t.saved_trade_urls.size());
    const float height = table_height(rows);
    if (facet::BeginDataTable("##saved-links", kColumns, rows, nullptr,
                              {.emptyText = "No saved links yet. Save one while sending an offer on Trade offers.",
                               .height = height})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                const app::SavedTradeLink& link = t.saved_trade_urls[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                facet::DataTableRow(i);
                ImGui::TableNextColumn();
                facet::TruncatedText(link.name.empty() ? "Unnamed" : link.name.c_str());
                ImGui::TableNextColumn();
                facet::TruncatedText(link.url.c_str());
                ImGui::TableNextColumn();
                const bool is_default = link.url == t.default_destination_trade_url;
                ImGui::BeginDisabled(is_default);
                if (fit_button(is_default ? "Default" : "Make default", {.size = facet::ControlSize::Small})) {
                    make_default = i;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (fit_button("Remove", {.variant = facet::ButtonVariant::Plain, .size = facet::ControlSize::Small})) {
                    remove = i;
                }
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
    if (make_default >= 0) {
        t.default_destination_trade_url = t.saved_trade_urls[static_cast<std::size_t>(make_default)].url;
        commit_now(p.state);
    }
    if (remove >= 0) {
        t.saved_trade_urls.erase(t.saved_trade_urls.begin() + remove);
        commit_now(p.state);
    }
}

}  // namespace

void draw_trading(Page& p) {
    draw_offers(p);
    draw_links(p);
}

}  // namespace sam::ui::screens::settings
