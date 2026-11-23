// The "Send items" flow: a drawer with a four step wizard (source accounts, destination, items,
// review) that ends in the send run with per-account progress.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
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
#include <facet/widgets/lists.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "core/strings.hpp"
#include "core/trade/trade_url.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"
#include "ui/screens/trade_offers_detail.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::trade_offers_ui {

namespace {

namespace trade = core::trade;
using namespace trade_offers_detail;

// Item tiles in the inventory step: large enough for most item names under the picture.
constexpr float kItemTile = 88.0F;

enum Step { kAccounts = 0, kDestination = 1, kItems = 2, kReview = 3 };

void note(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

// The source accounts in vault order.
std::vector<const core::Account*> source_accounts(const app::AppState& state) {
    std::vector<const core::Account*> out;
    for (const auto& a : state.vault.accounts) {
        if (g_send.accounts.count(a.id) != 0 && account_can_trade(a)) out.push_back(&a);
    }
    return out;
}

SendAccountPick& pick_of(const std::string& aid) {
    auto [it, inserted] = g_send.picks.try_emplace(aid);
    if (inserted) it->second.mode = g_send.accounts.size() > 1 ? SendMode::AllTradable : SendMode::Pick;
    return it->second;
}

std::uint32_t account_id32(const core::Account& a) {
    return static_cast<std::uint32_t>(a.steam_id_64 & 0xFFFFFFFFULL);
}

// The saved links remembered on send: added when new and made the default.
void remember_trade_link(app::AppState& state, const std::string& url) {
    auto& saved = state.settings.trade.saved_trade_urls;
    if (std::none_of(saved.begin(), saved.end(), [&](const auto& l) { return l.url == url; })) {
        saved.push_back({url, {}});
    }
    state.settings.trade.default_destination_trade_url = url;
    state.save_settings();
}

// --- Destinations ----------------------------------------------------------------------------

struct Destination {
    std::string url;
    std::string name;
    bool saved = false;     // a saved link (renamable, deletable)
    bool own = false;       // the trade link of an account in this vault
    bool is_default = false;
};

std::vector<Destination> destinations(const app::AppState& state) {
    std::vector<Destination> out;
    for (const auto& l : state.settings.trade.saved_trade_urls) {
        out.push_back({l.url, l.name, true, false, l.url == state.settings.trade.default_destination_trade_url});
    }
    for (const auto& a : state.vault.accounts) {
        if (!a.trade_url || a.trade_url->empty() || g_send.accounts.count(a.id) != 0) continue;
        if (std::any_of(out.begin(), out.end(), [&](const Destination& d) { return d.url == *a.trade_url; })) continue;
        out.push_back({*a.trade_url, kit::display_name(state, a), false, true,
                       *a.trade_url == state.settings.trade.default_destination_trade_url});
    }
    return out;
}

// The label of the chosen destination for the review.
std::string destination_label(const app::AppState& state) {
    for (const auto& d : destinations(state)) {
        if (d.url == g_send.url) return d.name.empty() ? std::string("Unnamed trade link") : d.name;
    }
    return "Trade link";
}

// Why the chosen destination cannot be used, or "".
std::string destination_problem(const app::AppState& state) {
    if (g_send.url.empty()) return "Choose where the items go.";
    const trade::TradeUrl tu = trade::parse_trade_url(g_send.url);
    if (!tu.ok) return "This trade link is not valid.";
    for (const core::Account* a : source_accounts(state)) {
        if (account_id32(*a) == tu.partner_account_id) {
            return "This link belongs to " + kit::display_name(state, *a) +
                   ", which is one of the sending accounts. Remove it in step 1 or choose another destination.";
        }
    }
    return {};
}

// --- Step 1: accounts ------------------------------------------------------------------------

// Why a search field over checkbox rows: a vault holds dozens of accounts, and choosing several
// of them is a search plus checks, with select-all for the shown ones. Accounts that can't trade
// stay visible at the end with the reason, so a missing account is explained.
void step_accounts(app::AppState& state) {
    const facet::Theme& t = facet::T();
    note("Each account sends one offer with its items.");
    gap(t.spacing.sm);

    const std::string query = core::to_lower(g_send.account_search);
    std::vector<const core::Account*> shown;
    std::vector<const core::Account*> blocked;
    for (const auto& a : state.vault.accounts) {
        const std::string name = kit::display_name(state, a);
        if (!query.empty() && core::to_lower(name).find(query) == std::string::npos &&
            core::to_lower(kit::login_text(state, a)).find(query) == std::string::npos) {
            continue;
        }
        (account_can_trade(a) ? shown : blocked).push_back(&a);
    }

    facet::BeginStack("##account-tools", {.orientation = facet::Orientation::Horizontal,
                                          .align = facet::LayoutAlign::Center,
                                          .width = facet::AvailableWidthUnits()});
    facet::SearchField("##account-search", &g_send.account_search, {.placeholder = "Search accounts", .width = 240.0F});
    const bool all_on = !shown.empty() && std::all_of(shown.begin(), shown.end(), [](const core::Account* a) {
        return g_send.accounts.count(a->id) != 0;
    });
    ImGui::BeginDisabled(shown.empty());
    if (facet::Button(all_on ? "Clear the shown accounts###all" : "Select all shown###all",
                      {.size = facet::ControlSize::Small})) {
        for (const core::Account* a : shown) {
            if (all_on) {
                g_send.accounts.erase(a->id);
                g_send.picks.erase(a->id);
            } else {
                g_send.accounts.insert(a->id);
            }
        }
    }
    ImGui::EndDisabled();
    facet::StackSpacer();
    const std::string count = plural(g_send.accounts.size(), "account", "accounts") + " chosen";
    facet::StyledText(count.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
    facet::EndStack();
    gap(t.spacing.sm);

    if (shown.empty() && blocked.empty()) {
        if (facet::EmptyState("No accounts match", {.message = "No account name or login contains the search.",
                                                    .icon = facet::Icon("search"),
                                                    .actionLabel = "Clear the search",
                                                    .compact = true})) {
            g_send.account_search.clear();
        }
        return;
    }
    for (const core::Account* a : shown) {
        ImGui::PushID(a->id.c_str());
        bool on = g_send.accounts.count(a->id) != 0;
        std::string label = kit::display_name(state, *a);
        if (!a->web.persona_name.empty() && !kit::login_hidden(state, *a)) label += "  (" + a->login + ")";
        label += "###pick";
        if (facet::Checkbox(label.c_str(), &on)) {
            if (on) {
                g_send.accounts.insert(a->id);
            } else {
                g_send.accounts.erase(a->id);
                g_send.picks.erase(a->id);
            }
        }
        ImGui::PopID();
    }
    if (!blocked.empty()) {
        gap(t.spacing.md);
        facet::SectionHeader("Can't trade", {.description = "These accounts can't send offers until the reason is fixed."});
        facet::BeginKeyValueList("##blocked");
        for (const core::Account* a : blocked) {
            ImGui::PushID(a->id.c_str());
            const std::string name = kit::display_name(state, *a) + "###name";
            facet::KeyValue(name.c_str(), cannot_trade_reason(*a));
            ImGui::PopID();
        }
        facet::EndKeyValueList();
    }
}
// --- Step 2: destination ---------------------------------------------------------------------

ImGuiID dest_storage_id(ImGuiSelectionBasicStorage* self, int index) {
    const auto* list = static_cast<const std::vector<Destination>*>(self->UserData);
    if (list == nullptr || index < 0 || index >= static_cast<int>(list->size())) return 0;
    return ImHashStr((*list)[static_cast<std::size_t>(index)].url.c_str());
}

ImGuiSelectionBasicStorage& dest_selection() {
    static ImGuiSelectionBasicStorage s;
    return s;
}

// Why one table for saved links and the vault's own trade links: both are places items can
// go, picked the same way. The chosen destination is summed up first, because it is the answer
// of this step; rename, default and delete sit in one menu per row, away from a click on the row.
void step_destination(app::AppState& state) {
    const facet::Theme& t = facet::T();
    std::vector<Destination> list = destinations(state);
    auto& saved = state.settings.trade.saved_trade_urls;

    const std::string problem = destination_problem(state);
    if (g_send.url.empty()) {
        note("Choose where the items go: a saved link, one of your accounts, or a new trade link below.");
    } else if (!problem.empty()) {
        facet::InlineAlert("##dest-problem", nullptr, problem.c_str(), {.tone = facet::Tone::Destructive});
    } else {
        const trade::TradeUrl tu = trade::parse_trade_url(g_send.url);
        const std::string head = "Items go to " + destination_label(state);
        facet::StyledText(head.c_str(), {.style = facet::TextStyle::Headline});
        const std::string sub = "Steam account " + std::to_string(tu.partner_account_id);
        note(sub.c_str());
    }
    gap(t.spacing.md);

    if (list.empty()) {
        facet::EmptyState("No saved trade links",
                          {.message = "Paste the destination's trade link below. Saved links come back here next "
                                      "time.",
                           .icon = facet::Icon("link"),
                           .compact = true});
    } else {
        const facet::DataTableColumn cols[] = {
            {.label = "Destination"},
            {.label = "Steam account", .width = 132.0F},
            {.label = "##menu", .width = t.metrics.controlHeight + t.spacing.md},
        };
        ImGuiSelectionBasicStorage& sel = dest_selection();
        sel.UserData = &list;
        sel.AdapterIndexToStorageId = &dest_storage_id;
        sel.Clear();
        if (!g_send.url.empty()) sel.SetItemSelected(ImHashStr(g_send.url.c_str()), true);
        const float row_h = t.list.rowHeight;
        const float visible = static_cast<float>(std::min<std::size_t>(list.size(), 6));
        const float height = (visible + 1.0F) * row_h + t.spacing.xs;
        std::string to_delete;
        if (facet::BeginDataTable("##destinations", cols, static_cast<int>(list.size()), &sel,
                                  {.resizable = false, .singleSelect = true, .height = height})) {
            facet::RowRange range;
            while (facet::NextVisibleRows(&range)) {
                for (int i = range.first; i < range.last; ++i) {
                    Destination& d = list[static_cast<std::size_t>(i)];
                    ImGui::PushID(d.url.c_str());
                    facet::DataTableRow(i);
                    ImGui::TableNextColumn();
                    const bool editing = g_send.renaming && g_send.rename_url == d.url;
                    {
                        kit::RowCell cell({.gap = facet::Gap::Tight});
                        if (editing) {
                            bool still = true;
                            if (facet::InlineRename("##rename", &g_send.rename_text, &still, {.maxLength = 64})) {
                                for (auto& l : saved) {
                                    if (l.url == d.url) l.name = g_send.rename_text;
                                }
                                state.save_settings();
                            }
                            if (!still) g_send.renaming = false;
                        } else {
                            if (d.name.empty()) {
                                facet::StyledText("Unnamed link", {.secondary = true});
                            } else {
                                facet::TruncatedText(d.name.c_str(), {.width = 200.0F});
                            }
                            if (d.is_default) {
                                cell.next();
                                facet::Badge("Default", {.tone = facet::Tone::Accent});
                            }
                            if (d.own) {
                                cell.next();
                                facet::Badge("Your account");
                            }
                        }
                    }
                    ImGui::TableNextColumn();
                    {
                        kit::RowCell cell;
                        const std::string id = std::to_string(trade::parse_trade_url(d.url).partner_account_id);
                        facet::StyledText(id.c_str(), {.secondary = true});
                        facet::ItemTooltip("%s", d.url.c_str());
                    }
                    ImGui::TableNextColumn();
                    if (d.saved || !d.is_default) {
                        kit::RowCell cell;
                        if (facet::IconButton("Actions for this destination", facet::Icon("more-horizontal"),
                                              {.size = facet::ControlSize::Small})) {
                            g_send.menu_url = d.url;
                            g_send.menu_open = true;
                        }
                        bool open = g_send.menu_open && g_send.menu_url == d.url;
                        if (facet::BeginPopover("##link-menu", &open, {.placement = facet::PopoverPlacement::Below,
                                                                       .width = 220.0F})) {
                            if (d.saved && facet::Button("Rename", {.variant = facet::ButtonVariant::Plain,
                                                                    .icon = facet::Icon("edit"),
                                                                    .width = facet::kFill})) {
                                g_send.rename_url = d.url;
                                g_send.rename_text = d.name;
                                g_send.renaming = true;
                                open = false;
                            }
                            if (!d.is_default && facet::Button("Make default", {.variant = facet::ButtonVariant::Plain,
                                                                                .icon = facet::Icon("star"),
                                                                                .width = facet::kFill})) {
                                state.settings.trade.default_destination_trade_url = d.url;
                                state.save_settings();
                                open = false;
                            }
                            if (d.saved) {
                                facet::Separator();
                                if (kit::quiet_destructive_button("Delete link")) {
                                    to_delete = d.url;
                                    open = false;
                                }
                            }
                            facet::EndPopover();
                        }
                        if (g_send.menu_url == d.url) g_send.menu_open = open;
                    }
                    ImGui::PopID();
                }
            }
            facet::EndDataTable();
        }
        for (const auto& d : list) {
            if (sel.Contains(ImHashStr(d.url.c_str()))) {
                if (d.url != g_send.url) g_send.url = d.url;
                break;
            }
        }
        if (!to_delete.empty()) {
            g_send.delete_url = to_delete;
            g_send.delete_open = true;
        }
    }

    std::string delete_msg = "The link is removed from the saved trade links. Offers already sent stay as they are.";
    if (kit::destructive_dialog("Delete this saved link?", &g_send.delete_open, delete_msg.c_str(), "Delete link") ==
        facet::DialogResult::Confirmed) {
        saved.erase(std::remove_if(saved.begin(), saved.end(),
                                   [&](const app::SavedTradeLink& l) { return l.url == g_send.delete_url; }),
                    saved.end());
        if (state.settings.trade.default_destination_trade_url == g_send.delete_url) {
            state.settings.trade.default_destination_trade_url.clear();
        }
        if (g_send.url == g_send.delete_url) g_send.url.clear();
        state.save_settings();
    }

    gap(t.spacing.lg);
    facet::SectionHeader("Add a trade link");
    const bool typed = !g_send.new_link.empty();
    const trade::TradeUrl parsed = trade::parse_trade_url(g_send.new_link);
    const bool duplicate = std::any_of(saved.begin(), saved.end(), [&](const auto& l) { return l.url == g_send.new_link; });
    const char* error = nullptr;
    if (typed && !parsed.ok) {
        error = "This is not a trade link. It looks like https://steamcommunity.com/tradeoffer/new/?partner=...&token=...";
    } else if (typed && duplicate) {
        error = "This link is saved already.";
    }
    facet::BeginStack("##add-link", {.orientation = facet::Orientation::Horizontal,
                                     .align = facet::LayoutAlign::Start,
                                     .width = facet::AvailableWidthUnits()});
    const float button_w = 150.0F;
    facet::TextField("##new-link", &g_send.new_link,
                     {.placeholder = "https://steamcommunity.com/tradeoffer/new/?partner=...&token=...",
                      .clearButton = true,
                      .width = facet::AvailableWidthUnits() - button_w - t.spacing.sm,
                      .error = error});
    ImGui::BeginDisabled(!typed || !parsed.ok || duplicate);
    if (facet::Button("Save and use", {.icon = facet::Icon("plus"), .width = button_w})) {
        saved.push_back({g_send.new_link, {}});
        g_send.url = g_send.new_link;
        g_send.new_link.clear();
        state.save_settings();
    }
    ImGui::EndDisabled();
    facet::EndStack();
}
// --- Step 3: items ---------------------------------------------------------------------------

void rebuild_inventory_cache(const std::string& aid) {
    std::lock_guard lk(g_mtx);
    auto& st = g_states[aid];
    if (st.inv_loaded_unix == g_send.inv_cache_unix && g_send.inv_cache_aid == aid) return;
    g_send.inv_cache.clear();
    g_send.inv_cache.reserve(st.inventory.size());
    g_send.inv_untradable = 0;
    for (const auto& it : st.inventory) {
        if (!it.tradable) {
            ++g_send.inv_untradable;
            continue;
        }
        g_send.inv_cache.push_back({it, core::to_lower(it.market_hash_name)});
    }
    g_send.inv_cache_unix = st.inv_loaded_unix;
    g_send.inv_cache_aid = aid;
}

struct InventoryFacts {
    bool loading = false;
    std::string error;
    std::int64_t loaded = 0;
};

InventoryFacts inventory_facts(const std::string& aid) {
    std::lock_guard lk(g_mtx);
    InventoryFacts f;
    if (auto it = g_states.find(aid); it != g_states.end()) {
        f.loading = it->second.inv_loading;
        f.error = it->second.inv_error;
        f.loaded = it->second.inv_loaded_unix;
    }
    return f;
}

int count_kind(const std::string& kind) {
    return static_cast<int>(std::count_if(g_send.inv_cache.begin(), g_send.inv_cache.end(),
                                          [&](const PickItem& p) { return p.item.market_hash_name == kind; }));
}

void select_kind(SendAccountPick& pick, const std::string& kind, bool on) {
    for (const auto& p : g_send.inv_cache) {
        if (p.item.market_hash_name != kind) continue;
        if (on) pick.selected.insert(p.item.asset_id);
        else pick.selected.erase(p.item.asset_id);
    }
}

// The load button with the inventory cooldown made visible.
void load_button(app::AppState& state, const core::Account& acc, const InventoryFacts& inv) {
    const auto cd = cooldown_remaining(inv.loaded, state.settings.trade.inventory_cooldown_seconds);
    if (cd > 0 && !inv.loading) {
        // While Steam's throttle runs, the countdown is a caption, not a dead button.
        const std::string caption = "Loaded " + ui::format_relative(inv.loaded) + ". You can load it again in " +
                                    kit::format_count(cd) + " s.";
        facet::StyledText(caption.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
        facet::ItemTooltip("Steam throttles inventory reads: an inventory can be loaded again %d s after the last load.",
                           state.settings.trade.inventory_cooldown_seconds);
        return;
    }
    const char* label = inv.loaded > 0 ? "Load again###load" : "Load inventory###load";
    if (facet::Button(label, {.icon = facet::Icon("refresh"), .loading = inv.loading})) {
        submit_inventory_load(state, acc);
    }
}

// Why a grid of inventory tiles: items are recognised by their picture first, and a click
// toggles one, so picking many is a series of clicks with the selection visible on each tile.
void item_grid(SendAccountPick& pick, float height_units) {
    const facet::Theme& t = facet::T();
    const std::string filter = core::to_lower(g_send.item_search);
    std::vector<int> shown;
    shown.reserve(g_send.inv_cache.size());
    for (int i = 0; i < static_cast<int>(g_send.inv_cache.size()); ++i) {
        if (filter.empty() || g_send.inv_cache[static_cast<std::size_t>(i)].lname.find(filter) != std::string::npos) {
            shown.push_back(i);
        }
    }
    if (shown.empty()) {
        if (facet::EmptyState("No items match", {.message = "No tradable item has that name.",
                                                 .icon = facet::Icon("search"),
                                                 .actionLabel = "Clear the search",
                                                 .compact = true})) {
            g_send.item_search.clear();
        }
        return;
    }
    if (!facet::BeginScrollArea("##items", {.height = height_units})) return;
    const float tile = kItemTile;
    const float gap_u = t.spacing.sm;
    const float avail = ImGui::GetContentRegionAvail().x / facet::Scale();
    const int cols = std::max(1, static_cast<int>((avail + gap_u) / (tile + gap_u)));
    const int rows = (static_cast<int>(shown.size()) + cols - 1) / cols;
    ImGuiListClipper clipper;
    clipper.Begin(rows, facet::Px(tile + gap_u));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int c = 0; c < cols; ++c) {
                const int k = row * cols + c;
                if (k >= static_cast<int>(shown.size())) break;
                const PickItem& p = g_send.inv_cache[static_cast<std::size_t>(shown[static_cast<std::size_t>(k)])];
                if (c > 0) ImGui::SameLine(0.0F, facet::Px(gap_u));
                ImGui::PushID(static_cast<int>(p.item.asset_id & 0x7FFFFFFF));
                ImGui::PushID(static_cast<int>(p.item.asset_id >> 31));
                const bool on = pick.selected.count(p.item.asset_id) != 0;
                if (item_tile("##tile", p.item, tile, on)) {
                    if (on) pick.selected.erase(p.item.asset_id);
                    else pick.selected.insert(p.item.asset_id);
                    g_send.last_kind = p.item.market_hash_name;
                }
                if (facet::BeginContextMenu("##kind")) {
                    const int n = count_kind(p.item.market_hash_name);
                    const std::string sel = "Select all " + std::to_string(n) + " of this kind";
                    const std::string clr = "Clear all " + std::to_string(n) + " of this kind";
                    if (facet::MenuItem(sel.c_str(), {.icon = facet::Icon("check")})) {
                        select_kind(pick, p.item.market_hash_name, true);
                    }
                    if (facet::MenuItem(clr.c_str())) select_kind(pick, p.item.market_hash_name, false);
                    facet::EndContextMenu();
                }
                ImGui::PopID();
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0F, facet::Px(gap_u) - ImGui::GetStyle().ItemSpacing.y));
        }
    }
    clipper.End();
    facet::EndScrollArea();
}

// Why per-account choice between "all tradable" and picking: emptying many accounts to one
// destination is the common case and needs no picking at all; picking is for one account.
void step_items(app::AppState& state, float body_units) {
    const facet::Theme& t = facet::T();
    const std::vector<const core::Account*> sources = source_accounts(state);
    if (sources.empty()) {
        facet::EmptyState("No source account", {.message = "Go back and choose at least one account."});
        return;
    }
    if (std::none_of(sources.begin(), sources.end(), [](const core::Account* a) { return a->id == g_send.items_account; })) {
        g_send.items_account = sources.front()->id;
    }

    const float top = ImGui::GetCursorPosY();
    // Whose inventory is on screen comes first: with several accounts, one chip per account with
    // its current choice, so the way to the next account is in view.
    if (sources.size() > 1) {
        std::vector<std::string> labels(sources.size());
        std::vector<facet::ChoiceItem> items(sources.size());
        int current = 0;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const SendAccountPick& p = pick_of(sources[i]->id);
            labels[i] = kit::display_name(state, *sources[i]) + ": " +
                        (p.mode == SendMode::AllTradable ? std::string("all tradable")
                                                         : plural(p.selected.size(), "item", "items"));
            items[i] = {.label = labels[i].c_str()};
            if (sources[i]->id == g_send.items_account) current = static_cast<int>(i);
        }
        if (facet::ChipGroup("##items-account", &current, items)) {
            g_send.items_account = sources[static_cast<std::size_t>(current)]->id;
            g_send.item_search.clear();
            g_send.last_kind.clear();
        }
    } else {
        kit::account_row(state, *sources.front(), {.layout = kit::AccountRowOptions::Layout::OneLine});
    }
    gap(t.spacing.md);

    const core::Account& acc = *state.find_account(g_send.items_account);
    SendAccountPick& pick = pick_of(acc.id);
    static const char* const kModes[] = {"All tradable items", "Choose items"};
    int mode = pick.mode == SendMode::AllTradable ? 0 : 1;
    if (facet::SegmentedControl("Send##mode", &mode, kModes)) {
        pick.mode = mode == 0 ? SendMode::AllTradable : SendMode::Pick;
    }
    gap(t.spacing.sm);

    rebuild_inventory_cache(acc.id);
    const InventoryFacts inv = inventory_facts(acc.id);

    if (pick.mode == SendMode::AllTradable) {
        std::string msg = "When you send, the app loads this account's CS2 inventory and puts every tradable item "
                          "into one offer.";
        if (inv.loaded > 0 && inv.error.empty()) {
            msg += " Right now that is " + plural(g_send.inv_cache.size(), "item", "items") + ".";
        }
        facet::InlineAlert("##all-note", nullptr, msg.c_str(), {.tone = facet::Tone::Neutral, .icon = facet::Icon("info")});
        return;
    }

    // Choose items: every tool on one line that wraps, so the grid keeps the height.
    facet::BeginFlow("##item-tools", {.gap = facet::Gap::Item});
    if (facet::FlowNext()) load_button(state, acc, inv);
    if (!g_send.inv_cache.empty()) {
        if (facet::FlowNext()) {
            facet::SearchField("##item-search", &g_send.item_search,
                               {.placeholder = "Search items", .size = facet::ControlSize::Small, .width = 180.0F});
        }
        if (facet::FlowNext()) {
            const std::string filter = core::to_lower(g_send.item_search);
            const std::string label = filter.empty() ? "Select all" : "Select all shown";
            if (facet::Button(label.c_str(), {.size = facet::ControlSize::Small})) {
                for (const auto& p : g_send.inv_cache) {
                    if (filter.empty() || p.lname.find(filter) != std::string::npos) pick.selected.insert(p.item.asset_id);
                }
            }
        }
        if (facet::FlowNext()) {
            // Every copy of one item in one step: the kinds with their counts.
            std::vector<std::string> kinds;
            std::vector<std::string> labels;
            for (const auto& p : g_send.inv_cache) {
                if (std::find(kinds.begin(), kinds.end(), p.item.market_hash_name) == kinds.end()) {
                    kinds.push_back(p.item.market_hash_name);
                }
            }
            std::vector<facet::ChoiceItem> items;
            labels.reserve(kinds.size());
            for (const auto& k : kinds) labels.push_back(k + " (" + kit::format_count(count_kind(k)) + ")");
            for (const auto& l : labels) items.push_back({.label = l.c_str()});
            int picked = -1;
            if (facet::Dropdown("##kind", &picked, items,
                                {.placeholder = "Choose all of a kind", .searchable = kinds.size() > 8,
                                 .size = facet::ControlSize::Small, .width = 200.0F}) &&
                picked >= 0) {
                select_kind(pick, kinds[static_cast<std::size_t>(picked)], true);
            }
        }
        if (!pick.selected.empty() && facet::FlowNext()) {
            const std::string clear = "Clear " + plural(pick.selected.size(), "chosen item", "chosen items") + "###clear";
            if (facet::Button(clear.c_str(), {.variant = facet::ButtonVariant::Plain, .size = facet::ControlSize::Small})) {
                pick.selected.clear();
            }
        }
    }
    facet::EndFlow();    gap(t.spacing.sm);

    const float used = (ImGui::GetCursorPosY() - top) / facet::Scale();
    const float grid_h = std::max(3.0F * t.metrics.slotSize, body_units - used - t.spacing.lg);

    if (!inv.error.empty()) {
        facet::InlineAlert("Could not load the inventory", nullptr, human_error(inv.error).c_str(),
                           {.tone = facet::Tone::Destructive});
        facet::ItemTooltip("%s", inv.error.c_str());
        gap(t.spacing.sm);
    }
    if (inv.loading && g_send.inv_cache.empty()) {
        if (facet::BeginDataStateView("##inv-loading", facet::DataState::Loading, 0, {.skeletonRows = 4})) {
            facet::EndDataStateView();
        }
        return;
    }
    if (inv.loaded == 0) {
        if (facet::EmptyState("Inventory not loaded",
                              {.message = "Load the CS2 inventory of this account to choose items from it.",
                               .icon = facet::Icon("download"),
                               .actionLabel = inv.loading ? nullptr : "Load inventory",
                               .height = 200.0F})) {
            submit_inventory_load(state, acc);
        }
        return;
    }
    if (g_send.inv_cache.empty() && inv.error.empty()) {
        std::string msg = "This account has no CS2 item that can be traded right now.";
        if (g_send.inv_untradable > 0) {
            msg += " " + plural(static_cast<std::size_t>(g_send.inv_untradable), "item is", "items are") +
                   " on a trade hold or can't be traded.";
        }
        facet::EmptyState("No tradable items", {.message = msg.c_str(), .icon = facet::Icon("image-off"), .height = 200.0F});
        return;
    }
    if (g_send.inv_cache.empty()) return;
    if (g_send.inv_untradable > 0) {
        const std::string msg = plural(static_cast<std::size_t>(g_send.inv_untradable), "item is", "items are") +
                                " not tradable right now and not shown. Click items to choose them.";
        note(msg.c_str());
    } else {
        note("Click items to choose them, or choose all copies of one item with the list above.");
    }
    gap(t.spacing.xs);
    item_grid(pick, grid_h - 2.0F * t.spacing.lg);
}

// --- Step 4: review and progress -------------------------------------------------------------

const char* status_text(SendStatus s) {
    switch (s) {
        case SendStatus::Queued: return "Waiting";
        case SendStatus::LoadingInventory: return "Loading inventory";
        case SendStatus::Sending: return "Sending";
        case SendStatus::Confirming: return "Confirming";
        case SendStatus::Sent: return "Sent";
        case SendStatus::SentConfirmed: return "Sent and confirmed";
        case SendStatus::NeedsConfirmation: return "Needs confirmation";
        case SendStatus::Skipped: return "Nothing to send";
        case SendStatus::Failed: return "Failed";
    }
    return "";
}

facet::Tone status_tone(SendStatus s) {
    switch (s) {
        case SendStatus::Sent:
        case SendStatus::SentConfirmed: return facet::Tone::Success;
        case SendStatus::NeedsConfirmation: return facet::Tone::Warning;
        case SendStatus::Failed: return facet::Tone::Destructive;
        default: return facet::Tone::Neutral;
    }
}

bool status_busy(SendStatus s) {
    return s == SendStatus::LoadingInventory || s == SendStatus::Sending || s == SendStatus::Confirming;
}

bool status_done(SendStatus s) {
    return !status_busy(s) && s != SendStatus::Queued;
}

// Why rows of identity and a status pill: while offers go out one account at a time, each
// account's line says where it is, and a failed one says why and what fixes it.
void draw_progress(app::AppState& state, const SendRun& run) {
    const facet::Theme& t = facet::T();
    int done = 0;
    int ok = 0;
    for (const auto& r : run.rows) {
        if (status_done(r.status)) ++done;
        if (r.status == SendStatus::Sent || r.status == SendStatus::SentConfirmed ||
            r.status == SendStatus::NeedsConfirmation) {
            ++ok;
        }
    }
    const int total = static_cast<int>(run.rows.size());
    const std::string head = run.finished ? "Sent offers from " + std::to_string(ok) + " of " +
                                                plural(static_cast<std::size_t>(total), "account", "accounts")
                                          : "Sending: " + std::to_string(done) + " of " + std::to_string(total) + " done";
    facet::ProgressBar((head + "###run-progress").c_str(),
                       total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0F,
                       {.tone = run.finished && ok < total ? facet::Tone::Warning
                                : run.finished              ? facet::Tone::Success
                                                            : facet::Tone::Accent});
    gap(t.spacing.md);
    for (std::size_t i = 0; i < run.rows.size(); ++i) {
        const SendResult& r = run.rows[i];
        const core::Account* a = state.find_account(r.account_id);
        if (a == nullptr) continue;
        ImGui::PushID(static_cast<int>(i));
        facet::BeginStack("##row", {.orientation = facet::Orientation::Horizontal,
                                    .align = facet::LayoutAlign::Center,
                                    .width = facet::AvailableWidthUnits()});
        kit::account_row(state, *a, {.layout = kit::AccountRowOptions::Layout::OneLine, .show_login = false});
        facet::StackSpacer();
        if (r.item_count > 0) {
            const std::string items = plural(static_cast<std::size_t>(r.item_count), "item", "items");
            facet::StyledText(items.c_str(), {.secondary = true});
        }
        if (status_busy(r.status)) {
            facet::Spinner(status_text(r.status), {.size = facet::ControlSize::Small});
        } else {
            const std::string pill = std::string(status_text(r.status)) + "###pill";
            facet::StatusPill(pill.c_str(), {.tone = status_tone(r.status), .size = facet::ControlSize::Small});
        }
        facet::EndStack();
        if (!r.detail.empty()) {
            // The pill carries the tone; the sentence says what happened and what to do.
            facet::StyledText(human_error(r.detail).c_str(),
                              {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
            facet::ItemTooltip("%s", r.detail.c_str());
        }
        gap(t.spacing.sm);
        ImGui::PopID();
    }
    if (run.finished && std::any_of(run.rows.begin(), run.rows.end(),
                                    [](const SendResult& r) { return r.status == SendStatus::NeedsConfirmation; })) {
        facet::InlineAlert("Some offers wait for a mobile confirmation", nullptr,
                           "Approve them on the Confirmations screen or in the Steam app.",
                           {.tone = facet::Tone::Warning});
    }
}

bool needs_ack(const std::vector<const core::Account*>& sources) {
    return std::any_of(sources.begin(), sources.end(),
                       [](const core::Account* a) { return pick_of(a->id).mode == SendMode::AllTradable; });
}

void step_review(app::AppState& state) {
    const facet::Theme& t = facet::T();
    const std::vector<const core::Account*> sources = source_accounts(state);
    const trade::TradeUrl tu = trade::parse_trade_url(g_send.url);

    facet::BeginKeyValueList("##review");
    const std::string to = destination_label(state) + " (Steam account " + std::to_string(tu.partner_account_id) + ")";
    facet::KeyValue("To", to.c_str());
    const std::string from = plural(sources.size(), "account", "accounts") + ", one offer each";
    facet::KeyValue("From", from.c_str());
    facet::EndKeyValueList();
    gap(t.spacing.sm);

    for (const core::Account* a : sources) {
        ImGui::PushID(a->id.c_str());
        const SendAccountPick& p = pick_of(a->id);
        const std::string what =
            p.mode == SendMode::AllTradable ? std::string("All tradable items") : plural(p.selected.size(), "item", "items");
        facet::BeginStack("##src", {.orientation = facet::Orientation::Horizontal,
                                    .align = facet::LayoutAlign::Center,
                                    .width = facet::AvailableWidthUnits()});
        kit::account_row(state, *a, {.layout = kit::AccountRowOptions::Layout::OneLine, .show_login = false});
        facet::StackSpacer();
        facet::StyledText(what.c_str(), {.secondary = true});
        facet::EndStack();
        ImGui::PopID();
    }
    gap(t.spacing.md);

    facet::TextField("Message##message", &g_send.message,
                     {.placeholder = "Optional, shown to the partner", .maxLength = 128});
    bool auto_conf = state.settings.trade.auto_confirm_sent;
    if (facet::Toggle("Confirm the offers with the authenticator", &auto_conf,
                      {.helpText = "Approves the mobile confirmation of each sent offer right away. Off: approve "
                                   "them on the Confirmations screen."})) {
        state.settings.trade.auto_confirm_sent = auto_conf;
        state.save_settings();
    }
    gap(t.spacing.sm);

    if (needs_ack(sources)) {
        facet::InlineAlert("Whole inventories are sent", nullptr,
                           "Accounts set to \"All tradable items\" send every tradable CS2 item they hold. An "
                           "accepted offer cannot be undone.",
                           {.tone = facet::Tone::Warning});
        facet::Checkbox("I checked the destination and want to send these inventories", &g_send.bulk_ack);
        gap(t.spacing.sm);
    }
    if (sources.size() > 1) {
        const int stagger_s = std::max(2000, state.settings.trade.refresh_stagger_ms) / 1000;
        const std::string msg = "Offers go out one account at a time, " + std::to_string(stagger_s) +
                                " s or more apart, so Steam does not rate limit them. You can close this panel: "
                                "the sending goes on and toasts report the result.";
        note(msg.c_str());
    }
}

bool step_complete(const app::AppState& state, int step) {
    switch (step) {
        case kAccounts: return !source_accounts(state).empty();
        case kDestination: return destination_problem(state).empty();
        case kItems: {
            const auto sources = source_accounts(state);
            if (sources.empty()) return false;
            return std::all_of(sources.begin(), sources.end(), [](const core::Account* a) {
                const SendAccountPick& p = pick_of(a->id);
                return p.mode == SendMode::AllTradable || !p.selected.empty();
            });
        }
        default: {
            const auto sources = source_accounts(state);
            return !sources.empty() && destination_problem(state).empty() && (!needs_ack(sources) || g_send.bulk_ack);
        }
    }
}

void start_send(app::AppState& state) {
    const auto sources = source_accounts(state);
    const trade::TradeUrl tu = trade::parse_trade_url(g_send.url);
    std::vector<SendJob> jobs;
    for (const core::Account* a : sources) {
        const SendAccountPick& p = pick_of(a->id);
        SendJob j;
        j.account = *a;
        j.all_tradable = p.mode == SendMode::AllTradable;
        if (!j.all_tradable) {
            for (const auto asset : p.selected) {
                trade::TradeAssetRef r;
                r.asset_id = asset;
                j.give.push_back(r);
            }
        }
        jobs.push_back(std::move(j));
    }
    remember_trade_link(state, g_send.url);
    g_send.run_id = start_send_run(state, std::move(jobs), tu, g_send.message);
}

}  // namespace

void open_send_flow(app::AppState& state, const std::string& account_id) {
    g_send = SendFlow{};
    if (const core::Account* a = state.find_account(account_id); a != nullptr && account_can_trade(*a)) {
        g_send.accounts.insert(account_id);
    }
    g_send.url = state.settings.trade.default_destination_trade_url;
    g_send.step = g_send.accounts.empty() ? kAccounts : kDestination;
    g_send.open = true;
}

// Why a drawer with a wizard: sending is a short sequence of decisions that each need the room
// of a panel, while the offer list stays visible behind it.
void draw_send_flow(app::AppState& state) {
    const facet::Theme& t = facet::T();
    // Wide, so the inventory grid and the link table use the room; Facet caps it at nine tenths of the window.
    if (!facet::BeginDrawer("Send items###send-items", &g_send.open, {.extent = 960.0F})) return;

    SendRun run;
    bool has_run = false;
    if (g_send.run_id != 0) {
        std::lock_guard lk(g_mtx);
        if (g_send_run.id == g_send.run_id) {
            run = g_send_run;
            has_run = true;
        }
    }

    static const facet::ChoiceItem kSteps[] = {
        {.label = "Accounts"}, {.label = "Destination"}, {.label = "Items"}, {.label = "Review"}};
    if (has_run) g_send.step = kReview;
    // A step indicator over the step's card and a footer of Cancel, Back and Next: the shape of
    // Facet's wizard, built from its parts because BeginWizard drew nothing on the first step in
    // the captures (see the report). A done step can be clicked to go back to it.
    facet::StepIndicator("##send-steps", &g_send.step, kSteps);
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));

    const float footer = t.metrics.controlHeight + t.spacing.md;
    const float body = std::max(160.0F, ImGui::GetContentRegionAvail().y / facet::Scale() - footer);
    // Each step is one object, a question with its answer, so it sits on a card; the wizard
    // footer under it carries the step's actions.
    static const char* const kTitles[] = {"Which accounts send items?", "Where do the items go?",
                                          "What does each account send?", "Check and send"};
    const char* title = has_run ? "Sending offers" : kTitles[std::clamp(g_send.step, 0, 3)];
    if (g_send.step == kItems) {
        ImGui::BeginChild("##items-step", ImVec2(0.0F, facet::Px(body)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoBackground);
        facet::BeginCard(title, {.height = body - t.spacing.xs});
        step_items(state, body - 2.0F * t.spacing.xxl);
        facet::EndCard();
        ImGui::EndChild();
    } else if (facet::BeginScrollArea("##step", {.height = body})) {
        facet::BeginCard(title);
        switch (g_send.step) {
            case kAccounts: step_accounts(state); break;
            case kDestination: step_destination(state); break;
            default:
                if (has_run) draw_progress(state, run);
                else step_review(state);
                break;
        }
        facet::EndCard();
        facet::EndScrollArea();
    }

    if (has_run) {
        facet::BeginStack("##run-footer", {.orientation = facet::Orientation::Horizontal,
                                           .width = facet::AvailableWidthUnits()});
        ImGui::BeginDisabled(!run.finished);
        if (facet::Button("Send more items")) {
            const std::string url = g_send.url;
            open_send_flow(state, {});
            g_send.url = url;
        }
        ImGui::EndDisabled();
        facet::StackSpacer();
        if (facet::Button(run.finished ? "Close###close" : "Close and keep sending###close",
                          {.variant = facet::ButtonVariant::Primary})) {
            g_send.open = false;
        }
        facet::EndStack();
    } else {
        const std::size_t n = source_accounts(state).size();
        const bool last = g_send.step == kReview;
        const std::string next = !last ? std::string("Next###next")
                                 : n > 1 ? "Send " + std::to_string(n) + " offers###next"
                                         : std::string("Send offer###next");
        facet::BeginStack("##footer", {.orientation = facet::Orientation::Horizontal,
                                       .width = facet::AvailableWidthUnits()});
        if (facet::Button("Cancel")) g_send.open = false;
        facet::StackSpacer();
        if (g_send.step == kItems) {
            // Why Next waits, beside it: the accounts set to "Choose items" with nothing chosen.
            std::vector<std::string> empty;
            for (const core::Account* a : source_accounts(state)) {
                const SendAccountPick& p = pick_of(a->id);
                if (p.mode == SendMode::Pick && p.selected.empty()) empty.push_back(kit::display_name(state, *a));
            }
            if (!empty.empty()) {
                const std::string caption = empty.size() == 1   ? empty[0] + " has no items chosen yet"
                                            : empty.size() == 2 ? empty[0] + " and " + empty[1] + " have no items chosen yet"
                                                                : plural(empty.size(), "account", "accounts") +
                                                                      " have no items chosen yet";
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0.0F, (facet::ControlHeight(facet::ControlSize::Regular) -
                                            facet::MeasureText(facet::TextStyle::Footnote, "Ag").y) * 0.5F));
                facet::StyledText(caption.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
                ImGui::EndGroup();
            }
        }
        ImGui::BeginDisabled(g_send.step == kAccounts);
        if (facet::Button("Back")) g_send.step = std::max(0, g_send.step - 1);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!step_complete(state, g_send.step));
        if (facet::Button(next.c_str(), {.variant = facet::ButtonVariant::Primary})) {
            if (last) start_send(state);
            else ++g_send.step;
        }
        ImGui::EndDisabled();
        facet::EndStack();
    }
    facet::EndDrawer();
}

}  // namespace sam::ui::screens::trade_offers_ui
