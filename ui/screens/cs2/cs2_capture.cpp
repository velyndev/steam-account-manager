// Capture-only seam of the CS2 screen. The harness cannot reach the Game Coordinator, so these
// scenes hand the screen a ready-made Cs2ScreenState (the view model the live client fills):
// a phase, a snapshot, unit contents and a move queue, with no client. Every command then has
// nothing to send to, so the screen draws exactly the state the scene set. Nothing here is
// compiled outside SAM_UI_CAPTURE builds. All names below are invented for the capture.

#ifdef SAM_UI_CAPTURE

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/screens/cs2_screen_state.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::cs2 {

namespace {

using app::AppState;

constexpr std::uint32_t kGrades[] = {0xB0C3D9, 0x5E98D9, 0x4B69FF, 0x8847FF, 0xD32CE6, 0xEB4B4B, 0xE4AE39};
constexpr const char* kWeapons[] = {"AK-47",  "M4A1-S", "AWP",    "Glock-18", "USP-S",  "Desert Eagle",
                                    "MP9",    "P250",   "FAMAS",  "Galil AR", "MAC-10", "Five-SeveN"};
constexpr const char* kSkins[] = {"Harbor Fog",  "Copper Lantern", "Quiet Orchard", "Paper Kite",  "Slate Tide",
                                  "Night Ferry", "Amber Static",   "Glass Meadow",  "Tin Soldier", "Velvet Signal"};
constexpr const char* kWears[] = {"Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"};

cs2_gc::DisplayItem demo_item(int i, std::uint64_t base) {
    cs2_gc::DisplayItem d;
    d.id = base + static_cast<std::uint64_t>(i) * 7919U;
    std::string name;
    if (i % 9 == 4) name += "StatTrak\xE2\x84\xA2 ";
    name += kWeapons[i % 12];
    name += " | ";
    name += kSkins[(i * 3) % 10];
    name += " (";
    name += kWears[(i * 2) % 5];
    name += ")";
    if (i % 17 == 6) name += "  \"Lucky number\"";
    d.name = name;
    // Lower grades are more common, as in a real inventory.
    const int roll = (i * 37) % 100;
    const int grade = roll < 30 ? 2 : roll < 55 ? 1 : roll < 70 ? 0 : roll < 84 ? 3 : roll < 93 ? 4 : roll < 98 ? 5 : 6;
    d.border_rgb = kGrades[grade];
    return d;
}

cs2_gc::Snapshot demo_snapshot(bool reward_available) {
    cs2_gc::Snapshot snap;
    for (int i = 0; i < 60; ++i) snap.items.push_back(demo_item(i, 30000000000ULL));
    const char* units[] = {"Armory overflow", "Sticker binder", "Trade-up stock"};
    const int counts[] = {26, 38, 991};
    for (int i = 0; i < 3; ++i) {
        cs2_gc::DisplayItem u;
        u.id = 20000000000ULL + static_cast<std::uint64_t>(i);
        u.name = units[i];
        u.is_storage_unit = true;
        u.item_count = counts[i];
        snap.units.push_back(u);
    }

    const std::int64_t now = now_seconds();
    snap.reward.loaded = true;
    snap.reward.available = reward_available;
    snap.reward.redeemable_balance = reward_available ? 2 : 0;
    snap.reward.generation_time = static_cast<std::uint32_t>(next_weekly_reset(now) - 7 * 86400 + 3600);
    if (reward_available) {
        for (int i = 0; i < 4; ++i) {
            cs2_gc::DisplayItem d = demo_item(70 + i, 40000000000ULL);
            snap.reward.item_ids.push_back(d.id);
            snap.reward.items.push_back(d);
        }
    } else {
        snap.reward.claimed_names = {"MP9 | Glass Meadow (Minimal Wear)", "Sticker | Paper Kite"};
    }
    snap.progress = {.valid = true, .level = 23, .xp_in_level = 3240, .xp_per_level = 5000};
    snap.mission = {.valid = true, .progress = 7, .target = 12, .xp = 1500, .name = "Win rounds in Premier"};
    for (int i = 0; i < 5; ++i) {
        cs2_gc::DisplayItem m;
        m.id = static_cast<std::uint64_t>(4500 + i);
        m.name = "Service medal";
        snap.medals.push_back(m);
    }
    snap.ranks.premier_rating = 14250;
    snap.ranks.premier_wins = 48;
    snap.ranks.wingman_rank = 9;
    return snap;
}

// The seam: opens the demo vault on the CS2 screen for account 0 and installs a view model in
// the given phase. Connected phases get the demo snapshot.
Cs2ScreenState& preview(AppState& state, Cs2Phase phase, int tab, bool reward_available = true) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::Cs2;
    state.selected_account_id = app::ui_capture::demo_account_id(0);
    state.cs2_screen = std::make_unique<Cs2ScreenState>();
    Cs2ScreenState& s = *state.cs2_screen;
    s.account_id = state.selected_account_id;
    s.phase = phase;
    s.tab = tab;
    if (phase == Cs2Phase::Connected) {
        s.status = "Ready";
        s.connected_since = now_seconds() - 14 * 60;
        s.snapshot = demo_snapshot(reward_available);
        s.has_snapshot = true;
    }
    return s;
}

void open_demo_unit(Cs2ScreenState& s, int unit) {
    s.open_unit = s.snapshot.units[static_cast<std::size_t>(unit)].id;
    s.units_detail_open = true;
    for (int i = 0; i < 26; ++i) s.unit_contents.push_back(demo_item(100 + i, 50000000000ULL));
}

void setup_not_connected(AppState& state) {
    (void)preview(state, Cs2Phase::Idle, 0);
}

void setup_connecting(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connecting, 0);
    s.status = "Connecting to CS2 (taking over the game session, up to a minute)";
}

void setup_failed(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Failed, 0);
    s.connection_error =
        "CS2 unavailable: CS2's Game Coordinator did not respond. The account may not own CS2, or the GC is busy; "
        "try again.";
}

void setup_setup_signin(AppState& state) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::Cs2;
    state.selected_account_id = app::ui_capture::demo_account_id(1);  // password, no client token
}

void setup_overview_unclaimed(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connected, 0, true);
    s.reward_selection.SetItemSelected(item_key(s.snapshot.reward.items[1].id), true);
}

void setup_overview_claimed(AppState& state) {
    (void)preview(state, Cs2Phase::Connected, 0, false);
}

void setup_inventory(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connected, 1);
    s.inv_selection.SetItemSelected(item_key(s.snapshot.items[4].id), true);
}

void setup_inventory_empty(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connected, 1);
    s.snapshot.items.clear();
}

void setup_inventory_loading(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connecting, 1);
    s.status = "Signing in to Steam";
}

void setup_storage(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connected, 2);
    open_demo_unit(s, 0);
    for (int i : {1, 2, 5}) s.unit_selection.SetItemSelected(item_key(s.unit_contents[static_cast<std::size_t>(i)].id), true);
}

void setup_storage_moving(AppState& state) {
    Cs2ScreenState& s = preview(state, Cs2Phase::Connected, 2);
    open_demo_unit(s, 0);
    // Six items on their way out: two moved, one failed, one in flight, two waiting.
    std::vector<const cs2_gc::DisplayItem*> moving;
    for (int i = 0; i < 6; ++i) moving.push_back(&s.unit_contents[static_cast<std::size_t>(i)]);
    enqueue_moves(s, s.open_unit, false, moving);
    s.ops[0].status = Cs2MoveOp::Status::Done;
    s.ops[1].status = Cs2MoveOp::Status::Done;
    s.ops[2].status = Cs2MoveOp::Status::Failed;
    s.ops[2].error = "your inventory is full";
    s.failed_items[s.ops[2].item_id] = s.ops[2].error;
    s.ops[3].status = Cs2MoveOp::Status::Running;
    s.pending = Cs2Pending::Move;
    // The moved ones have left the unit.
    s.unit_contents.erase(s.unit_contents.begin(), s.unit_contents.begin() + 2);
    s.snapshot.units[0].item_count = static_cast<int>(s.unit_contents.size());
}

}  // namespace

}  // namespace sam::ui::screens::cs2

SAM_UI_CAPTURE_SCENE(cs2_not_connected, "cs2-not-connected", &sam::ui::screens::cs2::setup_not_connected, 30)
SAM_UI_CAPTURE_SCENE(cs2_connecting, "cs2-connecting", &sam::ui::screens::cs2::setup_connecting, 30)
SAM_UI_CAPTURE_SCENE(cs2_failed, "cs2-failed", &sam::ui::screens::cs2::setup_failed, 30)
SAM_UI_CAPTURE_SCENE(cs2_signin, "cs2-signin", &sam::ui::screens::cs2::setup_setup_signin, 30)
SAM_UI_CAPTURE_SCENE(cs2_overview, "cs2-overview", &sam::ui::screens::cs2::setup_overview_unclaimed, 30)
SAM_UI_CAPTURE_SCENE(cs2_overview_claimed, "cs2-overview-claimed", &sam::ui::screens::cs2::setup_overview_claimed, 30)
SAM_UI_CAPTURE_SCENE(cs2_inventory, "cs2-inventory", &sam::ui::screens::cs2::setup_inventory, 30)
SAM_UI_CAPTURE_SCENE(cs2_inventory_empty, "cs2-inventory-empty", &sam::ui::screens::cs2::setup_inventory_empty, 30)
SAM_UI_CAPTURE_SCENE(cs2_inventory_loading, "cs2-inventory-loading", &sam::ui::screens::cs2::setup_inventory_loading, 30)
SAM_UI_CAPTURE_SCENE(cs2_storage, "cs2-storage", &sam::ui::screens::cs2::setup_storage, 30)
SAM_UI_CAPTURE_SCENE(cs2_storage_moving, "cs2-storage-moving", &sam::ui::screens::cs2::setup_storage_moving, 30)

#endif  // SAM_UI_CAPTURE
