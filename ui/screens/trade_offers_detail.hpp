#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/trade/inventory.hpp"
#include "core/trade/models.hpp"
#include "core/trade/offers.hpp"
#include "core/trade/trade_url.hpp"

namespace sam::ui::screens {
namespace trade_offers_detail {

// Per account, written by the worker threads under g_mtx.
struct AccountTradeState {
    std::vector<core::trade::TradeOffer> sent;
    std::vector<core::trade::TradeOffer> received;
    bool refreshing = false;
    std::string last_error;
    std::int64_t last_refresh_unix = 0;

    std::vector<core::trade::InventoryItem> inventory;
    bool inv_loading = false;
    std::string inv_error;
    std::int64_t inv_loaded_unix = 0;
};

struct PickItem {
    core::trade::InventoryItem item;
    std::string lname;  // pre-lowercased for filtering
};

// What the send flow does with one source account.
enum class SendMode {
    AllTradable,  // the worker loads the inventory and sends every tradable item
    Pick,         // only the items chosen in the item step
};

struct SendAccountPick {
    SendMode mode = SendMode::AllTradable;
    std::unordered_set<std::uint64_t> selected;  // asset ids (Pick)
};

// The "Send items" flow. UI-thread only.
struct SendFlow {
    bool open = false;
    int step = 0;  // 0 accounts, 1 destination, 2 items, 3 review
    std::unordered_set<std::string> accounts;
    std::unordered_map<std::string, SendAccountPick> picks;  // keyed by account id
    std::string account_search;

    std::string url;       // the chosen destination trade link
    std::string new_link;  // the "add a trade link" field
    std::string message;   // optional message for every offer
    bool bulk_ack = false;  // acknowledgment for sending whole inventories

    // Saved link management: keyed by url so list edits don't invalidate the target.
    std::string rename_url;
    std::string rename_text;
    bool renaming = false;
    std::string delete_url;
    bool delete_open = false;
    std::string menu_url;  // the saved link whose action menu is open
    bool menu_open = false;

    // Item step.
    std::string items_account;  // whose inventory the item step shows
    std::string item_search;
    std::string last_kind;  // market_hash_name of the item toggled last

    // Rebuilt only when the underlying inventory changes, not every frame.
    std::vector<PickItem> inv_cache;
    std::int64_t inv_cache_unix = -1;
    std::string inv_cache_aid;
    int inv_untradable = 0;

    std::uint64_t run_id = 0;  // the send run this flow started; 0 for none
};

// Progress of one account in a send run.
enum class SendStatus {
    Queued,
    LoadingInventory,
    Sending,
    Confirming,
    Sent,
    SentConfirmed,
    NeedsConfirmation,
    Skipped,
    Failed,
};

struct SendResult {
    std::string account_id;
    SendStatus status = SendStatus::Queued;
    int item_count = 0;  // known once the items are fixed
    std::string offer_id;
    std::string detail;  // error or confirmation note
};

// The send run of the flow; written by the send worker under g_mtx.
struct SendRun {
    std::uint64_t id = 0;
    std::vector<SendResult> rows;
    bool finished = false;
};

// One account's part of a send, as the flow hands it to the worker.
struct SendJob {
    core::Account account;
    bool all_tradable = true;
    std::vector<core::trade::TradeAssetRef> give;
};

// Shared persistent state: exactly one instance across the translation units. g_states,
// g_in_flight and g_send_run are guarded by g_mtx; the rest is UI-thread only.
inline std::mutex g_mtx;
inline std::unordered_map<std::string, AccountTradeState> g_states;
inline std::unordered_set<std::string> g_in_flight;  // offer ids with an action running
inline SendRun g_send_run;
inline SendFlow g_send;
inline bool g_confirm_accept_open = false;
inline int g_bulk_accept_count = 0;
inline int g_bulk_accept_gifts = 0;      // offers that give nothing away
inline int g_bulk_accept_giveaways = 0;  // offers that give at least one item
inline std::unordered_map<std::string, std::string> g_offer_errors;  // offer id -> last action error

// Shared helpers, defined in trade_offers_screen.cpp.
std::int64_t now_unix();
bool account_can_trade(const core::Account& a);
// Why an account cannot trade, or null when it can.
const char* cannot_trade_reason(const core::Account& a);
std::string economy_image_url(const std::string& icon_url);
std::string format_day(std::int64_t unix_s);
void apply_refreshed_tokens(app::AppState& state, const core::Account& creds);
bool tokens_changed(const core::Account& before, const core::Account& after);
void recount_incoming(app::AppState& state);
void erase_offer(app::AppState& state, const std::string& account_id, const std::string& offer_id,
                 bool from_received);
std::int64_t cooldown_remaining(std::int64_t last, int window);
std::int64_t refresh_cooldown_remaining(const app::AppState& state, const std::string& aid);
void submit_account_refresh(app::AppState& state, const core::Account& seed);
void submit_inventory_load(app::AppState& state, const core::Account& seed);
// Accept ('a'), decline ('d') or cancel ('c') one offer on a worker.
void submit_action(app::AppState& state, const core::Account& acc, const core::trade::TradeOffer& offer,
                   char kind);
// Refreshes every tradable account in `scope` (empty: all) that is off cooldown, staggered.
void refresh_all(app::AppState& state, const std::string& scope);

// Workers of the bulk actions, defined in trade_offers_modals.cpp.
// Accepts every active incoming offer of the accounts in `scope` (empty: all), staggered.
void bulk_accept_all(app::AppState& state, const std::string& scope);
// Confirms a sent offer that waits for the mobile confirmation, with the authenticator.
void submit_confirm_sent(app::AppState& state, const core::Account& acc, const std::string& offer_id);
// Sends one offer per job to `tu`, one account at a time; records the trade audit, raises the
// toasts and fills g_send_run. Returns the run id.
std::uint64_t start_send_run(app::AppState& state, std::vector<SendJob> jobs, core::trade::TradeUrl tu,
                             std::string message);

}  // namespace trade_offers_detail
}  // namespace sam::ui::screens
