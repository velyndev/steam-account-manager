#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "core/cs2_gc/cs2_gc_client.hpp"

namespace sam::ui::screens {

// Where the Game Coordinator connection of the CS2 screen stands.
enum class Cs2Phase {
    Idle,        // no session (never connected, or disconnected by the user)
    Connecting,  // the client is signing in or starting the CS2 session
    Connected,   // "Ready": the snapshot is live and commands are accepted
    Failed,      // the session ended with connection_error
};

// The command the screen is waiting on. The client works through one command at a time and
// answers each with on_action_done, on_error or on_unit_contents, so the screen keeps at most
// one in flight and knows which one an answer belongs to.
enum class Cs2Pending { None, LoadUnit, Move, Claim };

// One item of the move queue. Moves go to the client one at a time so each result lands on
// its own item.
struct Cs2MoveOp {
    enum class Status { Queued, Running, Done, Failed };
    std::uint64_t item_id = 0;
    std::uint64_t unit_id = 0;
    bool into_unit = true;  // false: out of the unit into the inventory
    std::string name;
    Status status = Status::Queued;
    std::string error;
};

// Per-account state of the CS2 screen, and the view model its drawing reads. The live client
// fills it through post_ui_callback lambdas (UI thread only, so no locking); a capture scene
// fills it directly. app/cs2_autopull.cpp reads `account_id` and `client`. `client` is last so
// it is torn down (thread joined) before the rest on destruction.
struct Cs2ScreenState {
    std::string account_id;

    // Connection.
    Cs2Phase phase = Cs2Phase::Idle;
    std::string status;             // the client's last stage text ("Signing in to Steam", ...)
    std::string connection_error;   // why the session failed (phase Failed)
    std::int64_t connected_since = 0;  // unix seconds of "Ready"
    std::uint64_t session = 0;      // bumped per client, so callbacks of a retired one are ignored

    // One-time SteamClient sign-in (acquire_cm_token).
    bool acquiring = false;
    std::string signin_error;

    cs2_gc::Snapshot snapshot;
    bool has_snapshot = false;

    // The command in flight and the errors of commands that are not moves.
    Cs2Pending pending = Cs2Pending::None;
    std::string action_error;  // shown as a dismissible alert on the tab it came from
    std::string claim_error;

    // Storage units.
    std::uint64_t open_unit = 0;
    bool unit_loading = false;
    std::string unit_error;
    std::vector<cs2_gc::DisplayItem> unit_contents;

    // Move queue: the ops of the current or last batch, and the error of every item that failed
    // in it, by item id, so the tile that failed shows it.
    std::vector<Cs2MoveOp> ops;
    std::unordered_map<std::uint64_t, std::string> failed_items;
    bool batch_open = false;  // a batch is queued or running and has not been reported yet

    // View state. Selections are keyed by a hash of the item id (see cs2::item_key), so they
    // survive filtering, sorting and snapshot refreshes.
    int tab = 0;  // index into the visible tabs
    std::string inv_query;
    int inv_sort = 0;
    int inv_rarity = 0;  // 0 all, else a cs2::Rarity filter
    bool inv_stattrak_only = false;
    std::uint64_t inv_selected = 0;  // the item the detail card shows
    ImGuiSelectionBasicStorage inv_selection;
    bool inv_detail_open = false;
    int store_target = -1;  // index into snapshot.units for "Store in"
    std::string unit_query;
    int unit_view = 0;  // 0 the unit's contents, 1 the inventory to add from
    ImGuiSelectionBasicStorage unit_selection;  // contents of the open unit
    ImGuiSelectionBasicStorage add_selection;   // inventory items to move into the open unit
    bool units_detail_open = false;
    ImGuiSelectionBasicStorage reward_selection;
    bool action_alert_open = true;
    bool details_open = false;  // the "Details" disclosure of a failure

    std::unique_ptr<cs2_gc::Cs2GcClient> client;
};

}  // namespace sam::ui::screens
