#pragma once

// Shared pieces of the CS2 screen: item facts parsed from the resolved item name, the item tile
// grid, the session commands, and the three tabs. Everything here reads and writes the
// Cs2ScreenState view model; only the session functions touch the network client.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/widgets/common.h>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/cs2_gc/cs2_gc_client.hpp"
#include "ui/screens/cs2_screen_state.hpp"

namespace sam::ui::screens::cs2 {

// --- Items -------------------------------------------------------------------------------------

// The rarity tiers, named after the weapon skin grades. The Game Coordinator gives only the
// grade color (cs2_gc::DisplayItem::border_rgb), so the tier is read from that color.
enum class Rarity { Unknown, Consumer, Industrial, MilSpec, Restricted, Classified, Covert, Contraband };
inline constexpr int kRarityCount = 8;

Rarity rarity_of(std::uint32_t rgb);
const char* rarity_name(Rarity rarity);  // "Mil-Spec", "Covert", "Unknown rarity"

// What the resolved name says about an item: "StatTrak™ AK-47 | Redline (Field-Tested)  "tag"".
struct ItemFacts {
    std::string title;     // the name without StatTrak, wear and name tag: "AK-47 | Redline"
    std::string weapon;    // "AK-47" (the whole title when it has no skin)
    std::string skin;      // "Redline", or empty
    std::string wear;      // "Field-Tested", or empty
    std::string name_tag;  // the custom name, or empty
    bool stattrak = false;
    bool souvenir = false;
    Rarity rarity = Rarity::Unknown;
};
ItemFacts item_facts(const cs2_gc::DisplayItem& item);

// The selection key of an item: its 64-bit id folded to an ImGuiID.
ImGuiID item_key(std::uint64_t id);

// A tile marker for an item of the move queue.
enum class TileMark { None, Queued, Failed };

struct ItemTilesOptions {
    ImGuiSelectionBasicStorage* selection = nullptr;  // null: tiles are not selectable
    // Called per visible tile for its marker and the error text of a failed one; null for none.
    TileMark (*mark)(const Cs2ScreenState& s, std::uint64_t id, const std::string** error) = nullptr;
    const Cs2ScreenState* state = nullptr;  // passed to `mark`
    facet::DataState data = facet::DataState::Ready;
    const char* empty_text = nullptr;
    const char* error_text = nullptr;
    float height = 0.0F;  // units; 0 fills the available height
    float tile_width = 104.0F;  // units; smallest tile width (tiles stretch to fill the row)
};

// A scrolling grid of item tiles (Facet ItemGrid): picture, a rarity bar in the grade color
// (data), the title and the wear, the full name as tooltip. Selection by click, Ctrl, Shift,
// box and arrow keys through `selection`, keyed by item_key. Returns the index of the tile
// activated this frame (double click, Enter), or -1.
int item_tiles(const char* id, std::span<const cs2_gc::DisplayItem* const> items, const ItemTilesOptions& options);

// The items of `items` whose key is in `selection`, in list order.
std::vector<const cs2_gc::DisplayItem*> selected_items(std::span<const cs2_gc::DisplayItem* const> items,
                                                       const ImGuiSelectionBasicStorage& selection);
int selected_count(std::span<const cs2_gc::DisplayItem* const> items, const ImGuiSelectionBasicStorage& selection);

// Search, filters and sort shared by the inventory and the unit views.
struct ItemFilter {
    const std::string* query = nullptr;
    int rarity = 0;  // 0 all, else a Rarity value
    bool stattrak_only = false;
    int sort = 0;  // 0 name, 1 rarity high to low, 2 rarity low to high
};
std::vector<const cs2_gc::DisplayItem*> filter_items(const std::vector<cs2_gc::DisplayItem>& items,
                                                     const ItemFilter& filter);

// Draws the filter row (search with the StatTrak chip, rarity and sort dropdowns) and writes
// the filter fields. `id` keys the widgets; `result_count` is shown after the search.
void filter_bar(const char* id, std::string* query, int* rarity, bool* stattrak_only, int* sort, int result_count);

// --- Session -----------------------------------------------------------------------------------

// Whether the account has a client-audience token the CM accepts, and why not.
enum class TokenState { Ready, NeedsSetup, NoPassword, Expired, Revoked };
TokenState token_state(const core::Account& account, std::int64_t now);

void connect(app::AppState& state, const core::Account& account);
void disconnect(Cs2ScreenState& s);
void retire_client(Cs2ScreenState& s);
void start_signin(app::AppState& state, const core::Account& account);
void refresh(Cs2ScreenState& s);
void open_unit(Cs2ScreenState& s, std::uint64_t unit_id);
void claim(Cs2ScreenState& s, std::vector<std::uint64_t> ids);
// Queues one move per item. Ignored while a batch runs.
void enqueue_moves(Cs2ScreenState& s, std::uint64_t unit_id, bool into_unit,
                   std::span<const cs2_gc::DisplayItem* const> items);
// Drops the queued ops that have not started.
void cancel_queued(Cs2ScreenState& s);
// Sends the next queued op when nothing is in flight; finishes the batch when all are done.
// Called once per frame.
void pump(app::AppState& state, Cs2ScreenState& s);

bool batch_running(const Cs2ScreenState& s);
int ops_finished(const Cs2ScreenState& s);
int ops_failed(const Cs2ScreenState& s);
TileMark op_mark(const Cs2ScreenState& s, std::uint64_t id, const std::string** error);

// The unit in the snapshot with this id, or null.
const cs2_gc::DisplayItem* find_unit(const Cs2ScreenState& s, std::uint64_t id);
// "3 items" / "1 item".
std::string item_count_text(int count);

// Secondary metadata text: Footnote in labelSecondary, printf formatted. `wrap` wraps at the
// available width.
void note(const char* fmt, ...) IM_FMTARGS(1);
// An error text from the client as a sentence: capital first letter, full stop.
std::string sentence(const std::string& text);
void note_wrapped(const char* fmt, ...) IM_FMTARGS(1);

// --- Tabs --------------------------------------------------------------------------------------

void draw_overview(app::AppState& state, core::Account& account, Cs2ScreenState& s);
void draw_inventory(app::AppState& state, core::Account& account, Cs2ScreenState& s);
void draw_storage(app::AppState& state, core::Account& account, Cs2ScreenState& s);

}  // namespace sam::ui::screens::cs2
