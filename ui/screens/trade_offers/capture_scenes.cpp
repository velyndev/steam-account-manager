// Capture scenes of the Trade offers screen. Debug builds only; see app/ui_capture.hpp.

#ifdef SAM_UI_CAPTURE

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "core/trade/trade_audit.hpp"
#include "ui/screens/trade_offers/trade_offers_ui.hpp"
#include "ui/screens/trade_offers_detail.hpp"

namespace {

using sam::app::AppState;
using sam::app::ui_capture::demo_account_id;
namespace td = sam::ui::screens::trade_offers_detail;
namespace tu = sam::ui::screens::trade_offers_ui;
namespace trade = sam::core::trade;
using S = trade::TradeOfferState;

std::int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

trade::InventoryItem item(std::uint64_t asset, const char* name, const char* type, const char* color,
                          bool tradable = true) {
    trade::InventoryItem it;
    it.asset_id = asset;
    it.class_id = asset * 7;
    it.name = name;
    it.market_hash_name = name;
    it.type = type;
    it.name_color = color;
    it.icon_url = "capture-demo-icon";
    it.tradable = tradable;
    it.marketable = tradable;
    return it;
}

// Fresh UI state for every scene: the scenes run one after another in one process.
void reset(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::TradeOffers;
    tu::g_ui = tu::UiState{};
    td::g_send = td::SendFlow{};
    td::g_offer_errors.clear();
    td::g_confirm_accept_open = false;
    {
        std::lock_guard lk(td::g_mtx);
        td::g_send_run = td::SendRun{};
    }
    td::recount_incoming(state);
}

// An offer on demo-acc-03 that gives two items and receives nothing, and a gift on demo-acc-07.
void add_flagged_offers(AppState& state) {
    const std::int64_t now = now_s();
    {
    std::lock_guard lk(td::g_mtx);
    trade::TradeOffer give;
    give.offer_id = "6100000010";
    give.partner_account_id = 4100;
    give.partner_steam_id_64 = 76561197960265728ULL + 4100;
    give.state = S::Active;
    give.created_unix = now - 900;
    give.expiration_unix = now + 14 * 86400LL;
    give.message = "Quick favour?";
    give.items_to_give = {item(32001, "Demo Rifle | Placeholder", "Classified Rifle", "D32CE6"),
                          item(32002, "Demo Pistol | Sample", "Restricted Pistol", "8847FF")};
    auto& a3 = td::g_states[demo_account_id(3)];
    a3.received.push_back(give);
    a3.last_refresh_unix = now - 240;

    trade::TradeOffer gift;
    gift.offer_id = "6100000011";
    gift.partner_account_id = 4200;
    gift.partner_steam_id_64 = 76561197960265728ULL + 4200;
    gift.state = S::Active;
    gift.created_unix = now - 7200;
    gift.expiration_unix = now + 13 * 86400LL;
    gift.items_to_receive = {item(32003, "Demo Case", "Base Grade Container", "D2D2D2"),
                             item(32004, "Demo Case", "Base Grade Container", "D2D2D2"),
                             item(32005, "Demo Sticker | Lorem", "High Grade Sticker", "4B69FF"),
                             item(32006, "Demo Sticker | Ipsum", "High Grade Sticker", "4B69FF"),
                             item(32007, "Demo Patch | Dolor", "High Grade Patch", "4B69FF"),
                             item(32008, "Demo Pin | Amet", "Remarkable Collectible", "8847FF"),
                             item(32009, "Demo Graffiti | Sit", "Base Grade Graffiti", "D2D2D2")};
    auto& a7 = td::g_states[demo_account_id(7)];
    a7.received.push_back(gift);
    a7.last_refresh_unix = now - 400;
    }
    // The sidebar badge and the screen count the same set.
    td::recount_incoming(state);
}

void seed_inventory(const std::string& aid) {
    std::lock_guard lk(td::g_mtx);
    auto& st = td::g_states[aid];
    st.inventory.clear();
    const char* names[][3] = {
        {"Demo Case", "Base Grade Container", "D2D2D2"},
        {"Demo Sticker | Lorem", "High Grade Sticker", "4B69FF"},
        {"Demo Agent | Test Pilot", "Exceptional Agent", "8847FF"},
        {"Demo Rifle | Placeholder", "Classified Rifle", "D32CE6"},
        {"Demo Knife | Mockup", "Covert Knife", "EB4B4B"},
        {"Demo Graffiti | Sit", "Base Grade Graffiti", "D2D2D2"},
    };
    std::uint64_t asset = 40000;
    for (int i = 0; i < 26; ++i) {
        const auto& n = names[i % 6 == 4 ? (i % 2) : i % 6];
        st.inventory.push_back(item(++asset, n[0], n[1], n[2], i % 9 != 8));
    }
    st.inv_loaded_unix = now_s() - 5;
}

void setup_sent(AppState& state) {
    reset(state);
    tu::g_ui.tab = tu::kSent;
}

void setup_detail(AppState& state) {
    reset(state);
    add_flagged_offers(state);
    tu::g_ui.selected_offer = "6100000010";
    tu::g_ui.detail_open = true;
    tu::g_ui.selection.SetItemSelected(ImHashStr("6100000010"), true);
}

void setup_accept_all(AppState& state) {
    reset(state);
    add_flagged_offers(state);
    td::g_bulk_accept_count = 3;
    td::g_bulk_accept_gifts = 1;
    td::g_bulk_accept_giveaways = 2;
    td::g_confirm_accept_open = true;
}

void setup_cancel(AppState& state) {
    reset(state);
    tu::g_ui.tab = tu::kSent;
    tu::g_ui.confirm_cancel_id = "6100000002";
    tu::g_ui.confirm_account_id = demo_account_id(0);
    tu::g_ui.confirm_cancel_open = true;
}

void setup_history(AppState& state) {
    reset(state);
    tu::g_ui.tab = tu::kHistory;
    const std::int64_t now = now_s();
    const struct {
        int account;
        int items;
        trade::TradeAuditOutcome outcome;
        const char* offer;
        const char* detail;
        bool bulk;
        int ago;
    } rows[] = {
        {0, 12, trade::TradeAuditOutcome::SentAndConfirmed, "6200000001", "", true, 86400},
        {3, 4, trade::TradeAuditOutcome::SentAndConfirmed, "6200000002", "", true, 86390},
        {7, 0, trade::TradeAuditOutcome::Failed, "", "inventory fetch failed: rate limited by Steam (429)", true, 86380},
        {12, 1, trade::TradeAuditOutcome::NeedsConfirmation, "6200000003", "confirmation not found yet", false, 7200},
        {16, 3, trade::TradeAuditOutcome::Sent, "6200000004", "", false, 3600},
        {20, 2, trade::TradeAuditOutcome::Failed, "", "There was an error sending your trade offer. (15)", false, 600},
    };
    for (const auto& r : rows) {
        trade::TradeAuditEntry e;
        e.unix_time = now - r.ago;
        e.account_id = demo_account_id(r.account);
        e.account_login = "demo-login-" + std::to_string(r.account);
        e.item_count = r.items;
        e.outcome = r.outcome;
        e.offer_id = r.offer;
        e.detail = r.detail;
        e.source = r.bulk ? trade::TradeAuditSource::UserBulk : trade::TradeAuditSource::UserSingle;
        state.trade_audit.record(std::move(e));
    }
}

void setup_empty(AppState& state) {
    reset(state);
    const std::int64_t now = now_s();
    std::lock_guard lk(td::g_mtx);
    td::g_states.clear();
    for (const auto& a : state.vault.accounts) {
        if (td::account_can_trade(a)) td::g_states[a.id].last_refresh_unix = now - 180;
    }
    state.pending_trade_offers_count.store(0, std::memory_order_relaxed);
}

void setup_first_load(AppState& state) {
    reset(state);
    std::lock_guard lk(td::g_mtx);
    td::g_states.clear();
    state.pending_trade_offers_count.store(0, std::memory_order_relaxed);
}

void seed_links(AppState& state) {
    auto& t = state.settings.trade;
    t.saved_trade_urls = {
        {"https://steamcommunity.com/tradeoffer/new/?partner=90000001&token=DemoStore", "Storage account"},
        {"https://steamcommunity.com/tradeoffer/new/?partner=90000002&token=DemoFriend", "Friend (demo)"},
        {"https://steamcommunity.com/tradeoffer/new/?partner=90000003&token=DemoOld", ""},
    };
    t.default_destination_trade_url = t.saved_trade_urls[0].url;
}

void setup_send_accounts(AppState& state) {
    reset(state);
    tu::open_send_flow(state, {});
    td::g_send.accounts.insert(demo_account_id(0));
    td::g_send.accounts.insert(demo_account_id(3));
    td::g_send.accounts.insert(demo_account_id(12));
}

void setup_send_destination(AppState& state) {
    reset(state);
    seed_links(state);
    tu::open_send_flow(state, demo_account_id(0));
    td::g_send.accounts.insert(demo_account_id(3));
    td::g_send.step = 1;
}

void setup_send_links(AppState& state) {
    reset(state);
    seed_links(state);
    tu::open_send_flow(state, demo_account_id(0));
    td::g_send.step = 1;
    td::g_send.renaming = true;
    td::g_send.rename_url = state.settings.trade.saved_trade_urls[1].url;
    td::g_send.rename_text = "Friend (demo)";
    td::g_send.new_link = "https://steamcommunity.com/id/not-a-trade-link";
}

void setup_send_items(AppState& state) {
    reset(state);
    seed_links(state);
    seed_inventory(demo_account_id(0));
    tu::open_send_flow(state, demo_account_id(0));
    td::g_send.accounts.insert(demo_account_id(3));
    td::g_send.accounts.insert(demo_account_id(12));
    td::g_send.picks[demo_account_id(3)].mode = td::SendMode::AllTradable;
    td::g_send.picks[demo_account_id(12)].mode = td::SendMode::Pick;
    td::g_send.items_account = demo_account_id(0);
    td::g_send.step = 2;
    auto& pick = td::g_send.picks[demo_account_id(0)];
    pick.mode = td::SendMode::Pick;
    pick.selected = {40001, 40007, 40013, 40004};
    td::g_send.last_kind = "Demo Case";
}

void setup_send_review(AppState& state) {
    reset(state);
    seed_links(state);
    tu::open_send_flow(state, demo_account_id(0));
    td::g_send.accounts.insert(demo_account_id(3));
    td::g_send.accounts.insert(demo_account_id(12));
    auto& p0 = td::g_send.picks[demo_account_id(0)];
    p0.mode = td::SendMode::Pick;
    p0.selected = {40001, 40002, 40003};
    td::g_send.picks[demo_account_id(3)].mode = td::SendMode::AllTradable;
    td::g_send.picks[demo_account_id(12)].mode = td::SendMode::AllTradable;
    td::g_send.step = 3;
}

void setup_send_progress(AppState& state) {
    reset(state);
    seed_links(state);
    tu::open_send_flow(state, demo_account_id(0));
    td::g_send.accounts.insert(demo_account_id(3));
    td::g_send.accounts.insert(demo_account_id(12));
    td::g_send.accounts.insert(demo_account_id(16));
    td::g_send.step = 3;
    td::g_send.run_id = 999;
    std::lock_guard lk(td::g_mtx);
    td::g_send_run.id = 999;
    td::g_send_run.rows = {
        {demo_account_id(0), td::SendStatus::SentConfirmed, 3, "6200000101", ""},
        {demo_account_id(3), td::SendStatus::Failed, 0, "", "inventory fetch failed: inventory unavailable (private?)"},
        {demo_account_id(12), td::SendStatus::Sending, 14, "", ""},
        {demo_account_id(16), td::SendStatus::Queued, 0, "", ""},
    };
}

}  // namespace

SAM_UI_CAPTURE_SCENE(trade_offers_sent, "trade-offers-sent", &setup_sent, 30)
SAM_UI_CAPTURE_SCENE(trade_offers_detail, "trade-offers-detail", &setup_detail, 40)
SAM_UI_CAPTURE_SCENE(trade_offers_accept_all, "trade-offers-accept-all", &setup_accept_all, 40)
SAM_UI_CAPTURE_SCENE(trade_offers_cancel, "trade-offers-cancel", &setup_cancel, 40)
SAM_UI_CAPTURE_SCENE(trade_offers_history, "trade-offers-history", &setup_history, 30)
SAM_UI_CAPTURE_SCENE(trade_offers_empty, "trade-offers-empty", &setup_empty, 30)
SAM_UI_CAPTURE_SCENE(trade_offers_first_load, "trade-offers-first-load", &setup_first_load, 30)
SAM_UI_CAPTURE_SCENE(trade_offers_send_accounts, "trade-offers-send-accounts", &setup_send_accounts, 60)
SAM_UI_CAPTURE_SCENE(trade_offers_send_destination, "trade-offers-send-destination", &setup_send_destination, 60)
SAM_UI_CAPTURE_SCENE(trade_offers_send_links, "trade-offers-send-links", &setup_send_links, 60)
SAM_UI_CAPTURE_SCENE(trade_offers_send_items, "trade-offers-send-items", &setup_send_items, 60)
SAM_UI_CAPTURE_SCENE(trade_offers_send_review, "trade-offers-send-review", &setup_send_review, 60)
SAM_UI_CAPTURE_SCENE(trade_offers_send_progress, "trade-offers-send-progress", &setup_send_progress, 60)

#endif  // SAM_UI_CAPTURE
