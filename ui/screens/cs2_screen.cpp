#include "ui/screens/cs2_screen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "platform/registry.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/screens/cs2_screen_state.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens {

namespace {

using cs2::TokenState;

constexpr float kBlockWidth = 520.0F;   // units; the width of the centered empty states
constexpr float kPickerWidth = 260.0F;  // units; the account picker in the connection card
constexpr float kStateWidth = 280.0F;   // units; the fixed state column of the connection card

// Moves the cursor so a block of `width` units that follows is centered in the page. An item
// is always submitted right after it, so the window bounds stay correct.
void center_next(float width) {
    const float pad = std::max(0.0F, (ImGui::GetContentRegionAvail().x - facet::Px(width)) * 0.5F);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
}

// "under a minute", "12 min", "2 h 5 min".
std::string duration_text(std::int64_t seconds) {
    if (seconds < 60) return "under a minute";
    const std::int64_t minutes = seconds / 60;
    char buf[48];
    if (minutes < 60) {
        std::snprintf(buf, sizeof(buf), "%lld min", static_cast<long long>(minutes));
    } else {
        std::snprintf(buf, sizeof(buf), "%lld h %lld min", static_cast<long long>(minutes / 60),
                      static_cast<long long>(minutes % 60));
    }
    return buf;
}

// The step of the connect the client's stage text belongs to (see Cs2GcClient::run).
const char* connect_step_text(const Cs2ScreenState& s) {
    if (s.has_snapshot) return "Step 3 of 3: loading the inventory";
    if (s.status.starts_with("Connecting to CS2")) return "Step 2 of 3: starting the CS2 session";
    if (s.status.starts_with("CS2 busy")) return "Step 1 of 3: CS2 is busy elsewhere, retrying";
    return "Step 1 of 3: signing in to Steam";
}

// What a failed connect means for the user, from the client's error text: a heading, one
// plain sentence with the likely cause and what to do, and whether renewing the sign-in helps.
struct FailureText {
    const char* title;
    const char* hint;
    bool token_problem;
};
FailureText describe_failure(const std::string& err) {
    if (err.find("signed in elsewhere") != std::string::npos)
        return {"The account is playing elsewhere",
                "Steam or an hour booster is using this account's CS2 session. Stop it there, then try again.",
                false};
    if (err.find("logon rejected") != std::string::npos || err.find("no refresh token") != std::string::npos)
        return {"Steam did not accept the CS2 sign-in",
                "The sign-in has probably expired. Renew it, then connect again.", true};
    if (err.find("did not respond") != std::string::npos || err.find("timed out") != std::string::npos)
        return {"CS2's servers did not answer",
                "They are often slow or busy for a minute. Wait a moment, then try again.", false};
    if (err.find("lost") != std::string::npos || err.find("dropped") != std::string::npos)
        return {"The connection dropped", "Steam ended the session. Connect again to continue.", false};
    return {"Could not connect to CS2", "Something went wrong while connecting. Try again in a moment.", false};
}

// Whether Steam on this PC is signed in to the account. Read at most once a second, since it
// is a registry read.
bool is_active_steam_user(const core::Account& a) {
    static std::chrono::steady_clock::time_point last{};
    static std::optional<std::uint32_t> cached;
    const auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::seconds(1)) {
        cached = platform::registry::read_active_user();
        last = now;
    }
    return cached && *cached != 0 && static_cast<std::uint32_t>(a.steam_id_64 & 0xffffffffULL) == *cached;
}

// The connection card: the account picker (the card is where the account is chosen, so it is
// shown once), the state in a column of fixed width so the pill never moves with the name, and
// the actions of the state. Its height is the same in every state, so nothing below it jumps;
// in a narrow window the actions take a second row that is always reserved.
void connection_card(app::AppState& state, core::Account* acct, Cs2ScreenState* s) {
    const facet::Theme& t = facet::T();
    facet::BeginCard("##connection");
    const float avail = ImGui::GetContentRegionAvail().x / facet::Px(1.0F);
    float& actions_w = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##actions-w"), 180.0F);
    // Narrower picker and state column below 900 units, so the actions still fit on the row
    // in a 900 px window and the card stays one row tall.
    const bool compact = avail < 900.0F;
    const float picker_w = compact ? 200.0F : kPickerWidth;
    const float state_w = compact ? 190.0F : kStateWidth;
    const float lead = 40.0F + picker_w + state_w + 3.0F * t.spacing.lg;
    const bool one_row = avail >= lead + actions_w;
    // On one row the state column takes the slack, so the buttons end at the right edge without
    // a StackSpacer (which shifted the last button out of line).
    const float state_col = one_row ? std::max(state_w, avail - lead + state_w - actions_w - 2.0F) : state_w;

    facet::BeginStack("##conn-row", {.orientation = facet::Orientation::Horizontal,
                                     .gap = facet::Gap::Loose,
                                     .align = facet::LayoutAlign::Center,
                                     .width = avail});
    if (acct != nullptr)
        kit::avatar(state, *acct, {.diameter = 40.0F});
    else
        facet::Skeleton({.shape = facet::SkeletonShape::Circle, .height = 40.0F});
    (void)kit::account_picker("##cs2-account", state, &state.selected_account_id,
                              {.filter = &kit::has_steam_id,
                               .placeholder = "Choose an account",
                               .empty_text = "No account has a Steam ID yet. Refresh all accounts first.",
                               .width = picker_w});

    // The state column: a vertical stack of fixed width (pill over caption).
    facet::BeginStack("##conn-state", {.gap = facet::Gap::Tight, .width = state_col});
    const char* detail = "";
    std::string detail_buf;
    if (s == nullptr) {
        facet::StatusPill("No account", {.icon = facet::Icon("info")});
    } else {
        switch (s->phase) {
            case Cs2Phase::Idle:
                facet::StatusPill("Not connected", {.icon = facet::Icon("info")});
                detail = s->status == "Disconnected" ? "Disconnected" : "";
                break;
            case Cs2Phase::Connecting:
                facet::StatusPill("Connecting", {.tone = facet::Tone::Accent, .liveDot = true});
                detail = connect_step_text(*s);
                break;
            case Cs2Phase::Connected:
                facet::StatusPill("Connected", {.tone = facet::Tone::Success, .liveDot = true});
                if (cs2::batch_running(*s)) {
                    detail_buf = "Moving items, " + std::to_string(cs2::ops_finished(*s)) + " of " +
                                 std::to_string(s->ops.size()) + " done";
                } else {
                    detail_buf = "For " + duration_text(now_seconds() - s->connected_since);
                }
                detail = detail_buf.c_str();
                break;
            case Cs2Phase::Failed:
                facet::StatusPill("Connection failed", {.tone = facet::Tone::Destructive, .icon = facet::Icon("error")});
                break;
        }
    }
    // A caption line is always there, so the column keeps its height.
    cs2::note("%s", detail[0] != '\0' ? detail : " ");
    facet::EndStack();

    // The buttons go straight into the row stack (a nested stack shifted its first button
    // down); in a narrow window they get a stack of their own on the second row.
    auto buttons = [&] {
        float x0 = 0.0F;
        float x1 = 0.0F;
        auto track = [&] {
            if (x0 == 0.0F) x0 = ImGui::GetItemRectMin().x;
            x1 = ImGui::GetItemRectMax().x;
        };
        if (s != nullptr && s->phase == Cs2Phase::Connecting) {
            if (facet::Button("Cancel connecting")) cs2::disconnect(*s);
            track();
        } else if (s != nullptr && s->phase == Cs2Phase::Connected) {
            if (facet::Button("Refresh", {.icon = facet::Icon("refresh")})) cs2::refresh(*s);
            track();
            if (facet::Button("Disconnect", {.icon = facet::Icon("close")})) cs2::disconnect(*s);
            track();
        } else {
            ImGui::Dummy(ImVec2(0.0F, facet::Px(t.metrics.controlHeight)));
        }
        if (x1 > x0) actions_w = (x1 - x0) / facet::Px(1.0F);
    };
    if (one_row) buttons();
    facet::EndStack();
    if (!one_row) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs)));
        facet::BeginStack("##conn-actions", {.orientation = facet::Orientation::Horizontal,
                                             .align = facet::LayoutAlign::Center});
        buttons();
        facet::EndStack();
    }
    facet::EndCard();
}

// The empty state that replaces the tabs while the account cannot connect yet. Returns true
// when something was drawn.
bool draw_gate(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    const TokenState token = cs2::token_state(acct, now_seconds());
    if (token == TokenState::Ready || s.phase != Cs2Phase::Idle) return false;

    const char* title = "Set up the CS2 sign-in";
    const char* message =
        "CS2 needs its own sign-in for this account. The app signs in once with the stored password and "
        "fills in the Steam Guard code from the authenticator.";
    if (token == TokenState::Expired) {
        title = "The CS2 sign-in has expired";
        message = "Renew it once with the stored password. The Steam Guard code is filled in from the authenticator.";
    } else if (token == TokenState::Revoked) {
        title = "The CS2 sign-in no longer works";
        message = "Steam stopped accepting it. Renew it once with the stored password.";
    }
    if (acct.password.empty() || token == TokenState::NoPassword) {
        center_next(kBlockWidth);
        if (kit::empty_state("CS2 sign-in needs the password",
                             {.message = "This account has no stored password, so the app cannot sign it in to CS2. "
                                         "Add the account again with a full login.",
                              .icon = icons::token,
                              .action_label = "Show on Accounts",
                              .width = kBlockWidth}))
            kit::open_account(state, acct.id);
        return true;
    }
    const char* action = token == TokenState::NeedsSetup ? "Set up CS2 sign-in" : "Renew CS2 sign-in";
    center_next(kBlockWidth);
    if (s.acquiring) {
        (void)kit::empty_state(title, {.message = "Signing in to Steam. This takes a few seconds.",
                                       .icon = icons::token,
                                       .width = kBlockWidth});
        center_next(16.0F);
        facet::Spinner("Signing in", {.tone = facet::Tone::Accent});
    } else if (kit::empty_state(title, {.message = message,
                                        .icon = icons::token,
                                        .action_label = action,
                                        .width = kBlockWidth})) {
        cs2::start_signin(state, acct);
    }
    if (!s.signin_error.empty()) {
        center_next(kBlockWidth);
        (void)facet::InlineAlert("The sign-in did not work", nullptr,
                                 "Check that the stored password and the authenticator still belong to this "
                                 "account, then try again.",
                                 {.tone = facet::Tone::Destructive, .width = kBlockWidth});
        center_next(s.details_open ? kBlockWidth : 64.0F);
        ImGui::BeginGroup();
        if (facet::BeginDisclosure("Details###signin-details", &s.details_open, {.style = facet::DisclosureStyle::Link})) {
            cs2::note_wrapped("%s", s.signin_error.c_str());
            facet::EndDisclosure();
        }
        ImGui::EndGroup();
    }
    return true;
}

// Idle: what connecting does, in the empty state that holds the one Primary action. The
// warning about this PC's Steam goes under it, so the heading never moves.
void draw_not_connected(app::AppState& state, core::Account& acct) {
    center_next(kBlockWidth);
    if (kit::empty_state("Not connected to CS2",
                         {.message = "Connecting signs this account in to CS2 from this app. If it is in a match "
                                     "right now it leaves that match, and an account connected here before is "
                                     "disconnected.",
                          .icon = icons::crosshair,
                          .action_label = "Connect to CS2",
                          .width = kBlockWidth}))
        cs2::connect(state, acct);
    if (is_active_steam_user(acct)) {
        center_next(kBlockWidth);
        (void)facet::InlineAlert("Steam on this PC is signed in to this account", nullptr,
                                 "If CS2 is running here, connecting ends that game.",
                                 {.tone = facet::Tone::Warning, .width = kBlockWidth});
    }
}

// Failed: one heading, one sentence, "Try again" as the Primary action; the client's own text
// only behind Details.
void draw_failed(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    const FailureText f = describe_failure(s.connection_error);
    center_next(kBlockWidth);
    if (kit::empty_state(f.title, {.message = f.hint,
                                   .icon = facet::Icon("error"),
                                   .action_label = "Try again",
                                   .action_icon = facet::Icon("refresh"),
                                   .tone = facet::Tone::Destructive,
                                   .width = kBlockWidth}))
        cs2::connect(state, acct);
    if (f.token_problem && !acct.password.empty()) {
        center_next(kBlockWidth);
        facet::BeginStack("##renew", {.align = facet::LayoutAlign::Center, .width = kBlockWidth});
        if (facet::Button("Renew CS2 sign-in", {.variant = facet::ButtonVariant::Plain, .loading = s.acquiring})) {
            cs2::disconnect(s);
            s.connection_error.clear();
            cs2::start_signin(state, acct);
        }
        facet::EndStack();
    }
    center_next(s.details_open ? kBlockWidth : 64.0F);
    ImGui::BeginGroup();
    if (facet::BeginDisclosure("Details###failure-details", &s.details_open, {.style = facet::DisclosureStyle::Link})) {
        cs2::note_wrapped("%s", s.connection_error.c_str());
        facet::EndDisclosure();
    }
    ImGui::EndGroup();
}

struct TabDef {
    int id;
    const char* label;
};

// Tabs in the order of how often each is the reason for a visit: the weekly state first, then
// the inventory, then the storage units. Only the tabs the settings enable are shown.
void draw_tabs(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    const auto& gc = state.settings.cs2_gc;
    TabDef defs[3];
    facet::ChoiceItem tabs[3];
    int n = 0;
    defs[n++] = {0, "Overview"};
    if (gc.show_inventory) defs[n++] = {1, "Inventory"};
    if (gc.show_storage_units) defs[n++] = {2, "Storage units"};
    for (int i = 0; i < n; ++i) tabs[i] = {.label = defs[i].label};
    int current = 0;
    for (int i = 0; i < n; ++i)
        if (defs[i].id == s.tab) current = i;
    (void)facet::TabBar("##cs2-tabs", &current, std::span<const facet::ChoiceItem>(tabs, static_cast<std::size_t>(n)));
    s.tab = defs[current].id;
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));

    switch (s.tab) {
        case 1: cs2::draw_inventory(state, acct, s); break;
        case 2: cs2::draw_storage(state, acct, s); break;
        default: cs2::draw_overview(state, acct, s); break;
    }
}

// Transient state of the screen: the Details disclosures and the alerts. Selections and
// filters belong to the account's view model, which is dropped on lock (clear_session_secrets)
// and on an account switch.
void reset_cs2_transient(app::AppState& state) {
    if (!state.cs2_screen) return;
    Cs2ScreenState& s = *state.cs2_screen;
    s.details_open = false;
    s.action_alert_open = true;
    s.action_error.clear();
}

}  // namespace

void discard_cs2_client(app::AppState& state) {
    if (state.cs2_screen) cs2::retire_client(*state.cs2_screen);
    state.cs2_screen.reset();
}

// One account at a time: the connection card on top holds the account choice and the session
// state, because every tab depends on both; the tabs below hold the data of the live session.
void draw_cs2(app::AppState& state) {
    kit::PageScope page("cs2", {.scroll = false});
    {
        kit::PageHeaderScope header("CS2", {.context = "Weekly drop, inventory and storage units of one account"});
    }

    core::Account* acct =
        state.selected_account_id.empty() ? nullptr : state.find_account(state.selected_account_id);
    if (acct != nullptr && acct->steam_id_64 != 0 &&
        (!state.cs2_screen || state.cs2_screen->account_id != acct->id)) {
        discard_cs2_client(state);
        state.cs2_screen = std::make_unique<Cs2ScreenState>();
        state.cs2_screen->account_id = acct->id;
    }
    Cs2ScreenState* sp = acct != nullptr && acct->steam_id_64 != 0 ? state.cs2_screen.get() : nullptr;

    connection_card(state, acct, sp);
    // The picker may have chosen another account this frame; the body follows next frame.
    if (acct != nullptr && acct->id != state.selected_account_id) return;
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.md)));

    if (acct == nullptr) {
        discard_cs2_client(state);
        center_next(kBlockWidth);
        (void)kit::empty_state("Choose an account",
                               {.message = "Pick an account in the card above to see its weekly drop and mission, "
                                           "its inventory and its storage units.",
                                .icon = icons::crosshair,
                                .width = kBlockWidth});
        return;
    }
    if (acct->steam_id_64 == 0) {
        discard_cs2_client(state);
        center_next(kBlockWidth);
        if (kit::empty_state("This account has no Steam ID yet",
                             {.message = "Refresh it on the Accounts screen so the app learns its Steam ID, then come "
                                         "back to connect.",
                              .icon = icons::crosshair,
                              .action_label = "Show on Accounts",
                              .width = kBlockWidth}))
            kit::open_account(state, acct->id);
        return;
    }

    Cs2ScreenState& s = *sp;
    cs2::pump(state, s);
    if (draw_gate(state, *acct, s)) return;
    switch (s.phase) {
        case Cs2Phase::Idle: draw_not_connected(state, *acct); return;
        case Cs2Phase::Failed: draw_failed(state, *acct, s); return;
        case Cs2Phase::Connecting: draw_tabs(state, *acct, s); return;
        case Cs2Phase::Connected: draw_tabs(state, *acct, s); return;
    }
}

}  // namespace sam::ui::screens

SAM_KIT_TRANSIENT_RESET(cs2_screen, &sam::ui::screens::reset_cs2_transient)
SAM_KIT_SCREEN_LEAVE(cs2_screen, sam::app::Screen::Cs2, &sam::ui::screens::reset_cs2_transient)
