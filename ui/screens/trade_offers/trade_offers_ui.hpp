#pragma once

// UI state and drawing pieces of the Trade offers screen, shared by its translation units.
// Everything here is UI-thread only.

#include <string>
#include <vector>

#include <imgui.h>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/trade/models.hpp"

namespace sam::ui::screens::trade_offers_ui {

enum Tab { kIncoming = 0, kSent = 1, kHistory = 2 };

struct UiState {
    int tab = kIncoming;
    std::string scope;  // account id the page is filtered to; empty for all accounts

    // The offer whose detail is open.
    std::string selected_offer;
    bool detail_open = false;
    ImGuiSelectionBasicStorage selection;

    // Accept of an offer that gives items and receives nothing waits for this confirmation.
    bool confirm_offer_open = false;
    // Cancel of a sent offer waits for this confirmation (confirm_account_id names its account).
    bool confirm_cancel_open = false;
    std::string confirm_cancel_id;
    std::string confirm_offer_id;
    std::string confirm_account_id;

    bool cant_trade_open = false;
    bool failures_open = false;

    std::string history_query;
    int history_result = 0;  // 0 all, 1 sent, 2 needs confirmation, 3 failed
};
inline UiState g_ui;

// --- Items (trade_offers/items.cpp) ---------------------------------------------------------

// The Steam rarity color of an item ("D2D2D2", data) as a packed color, 0 when unknown.
ImU32 rarity_color(const std::string& hex);

// One item as a Facet inventory tile of `size` units: the picture from the image cache drawn
// into the tile once it is downloaded, the picture glyph while it loads and the missing-picture
// glyph when it does not arrive, the rarity color as the tile's category bar, the name as
// tooltip. With `tab_stop` false the tile is left out of Tab order (thumbnails inside a row
// whose row is the keyboard target). Returns true when the tile was activated.
bool item_tile(const char* id, const core::trade::InventoryItem& item, float size, bool selected,
               bool tab_stop = true);

// A strip of item tiles that fits `width` units: as many tiles as fit, the last replaced by a
// "+N" count when there are more. "Nothing" in labelSecondary for an empty list. Returns true
// when a tile was activated.
bool item_strip(const char* id, const std::vector<core::trade::InventoryItem>& items, float width,
                float size);

// One item as a list row for the detail pane: tile, name, and its type line (which names the
// rarity) in labelSecondary.
void item_row(const char* id, const core::trade::InventoryItem& item);

// --- Text ------------------------------------------------------------------------------------

// A Steam or app error as a sentence for people: what happened and what to do (brief rule 5).
// The raw text belongs in a tooltip.
std::string human_error(const std::string& error);
// True when the error means the saved sign-in of the account must be renewed.
bool error_needs_sign_in(const std::string& error);

// How the trade partner is named: the vault account with that Steam ID, else "Steam user N".
std::string partner_name(const app::AppState& state, const core::trade::TradeOffer& offer);
const core::Account* partner_account(const app::AppState& state, const core::trade::TradeOffer& offer);

// --- Views -----------------------------------------------------------------------------------

void draw_offers_tab(app::AppState& state, bool incoming);    // trade_offers_screen.cpp
void draw_history_tab(app::AppState& state);                  // trade_offers/history.cpp
void open_send_flow(app::AppState& state, const std::string& account_id);  // trade_offers/send_flow.cpp
void draw_send_flow(app::AppState& state);

}  // namespace sam::ui::screens::trade_offers_ui
