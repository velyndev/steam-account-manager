// The Overview tab: is this account done for the week? The weekly drop first (the focal
// point), the weekly mission beside it, then the profile facts the Game Coordinator returns.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>

#include "ui/icons.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/standing.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::cs2 {

namespace {

bool drop_claimed_this_week(const cs2_gc::WeeklyReward& reward) {
    if (!reward.loaded || reward.available) return false;
    const std::int64_t now = now_seconds();
    const std::int64_t last_reset = next_weekly_reset(now) - 7 * 86400;
    return reward.generation_time >= static_cast<std::uint32_t>(last_reset);
}

std::string thousands(long long v) {
    return kit::format_count(v);
}

// StatTile formatter: thousands separators, "Unknown" for a value the Game Coordinator did not
// report (negative).
int count_or_unknown(char* buffer, int size, double value, void*) {
    if (value < 0.0) return std::snprintf(buffer, static_cast<std::size_t>(size), "Unknown");
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s",
                         kit::format_count(static_cast<std::int64_t>(value)).c_str());
}

// Pads a card to the height of its row neighbour: each card stores its own content height under
// `key`, the shorter one fills up to the taller, so the two cards end on one line.
struct EqualHeight {
    ImGuiID self;
    ImGuiID other;
    float start;
    bool active;
    EqualHeight(const char* self_key, const char* other_key, bool on)
        : self(ImGui::GetID(self_key)), other(ImGui::GetID(other_key)), start(ImGui::GetCursorPosY()), active(on) {}
    void finish() const {
        const float h = ImGui::GetCursorPosY() - start;
        ImGuiStorage* st = ImGui::GetStateStorage();
        st->SetFloat(self, h);
        const float target = active ? st->GetFloat(other, 0.0F) : 0.0F;
        if (target > h) ImGui::Dummy(ImVec2(0.0F, target - h));
    }
};

std::string reset_text() {
    return "Resets " + format_date(next_weekly_reset(now_seconds())) + " at 01:00 UTC";
}

// The manual claimed mark of the vault (the account list's drop marker), offered while the
// automatic one is off, as the accounts list offered it.
void manual_mark(app::AppState& state, core::Account& acct) {
    if (state.settings.cs2_gc.auto_mark_claimed) return;
    const std::int64_t now = now_seconds();
    const bool marked = acct.cs2.weekly_drop_reset_unix > now;
    if (facet::Button(marked ? "Clear the claimed mark" : "Mark as claimed", {.variant = facet::ButtonVariant::Plain})) {
        acct.cs2.weekly_drop_reset_unix = marked ? 0 : next_weekly_reset(now);
        state.vault_dirty = true;
        state.save_vault_if_dirty();
    }
    facet::ItemTooltip("Sets the weekly drop marker in the account list by hand");
}

// The hero line of the weekly drop card: the answer in the Title2 style, the reset under it.
void drop_headline(const char* answer, bool done = false) {
    facet::Text(facet::TextStyle::Title2, "%s", answer);
    const std::int64_t left = next_weekly_reset(now_seconds()) - now_seconds();
    char reset[64];
    if (left >= 86400)
        std::snprintf(reset, sizeof(reset), "Resets in %lld d %lld h", static_cast<long long>(left / 86400),
                      static_cast<long long>((left % 86400) / 3600));
    else
        std::snprintf(reset, sizeof(reset), "Resets in %lld h", static_cast<long long>(std::max<std::int64_t>(1, left / 3600)));
    facet::StatusPill(reset, {.tone = done ? facet::Tone::Success : facet::Tone::Neutral,
                              .icon = facet::Icon(done ? "check" : "clock"),
                              .size = facet::ControlSize::Small});
    facet::ItemTooltip("%s", reset_text().c_str());
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
}

// Item tiles laid out as one block of rows sized to the items, so a card never scrolls inside
// itself. `tile` is the smallest tile width in units; names get two caption lines.
void tile_block(const char* id, const std::vector<const cs2_gc::DisplayItem*>& items, ImGuiSelectionBasicStorage* sel) {
    constexpr float kTile = 128.0F;
    const float avail = ImGui::GetContentRegionAvail().x / facet::Px(1.0F);
    const float gap = facet::T().spacing.sm;
    const int cols = std::max(1, static_cast<int>((avail + gap) / (kTile + gap)));
    const float tile = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
    const float h = static_cast<float>(rows) * (tile + gap) + gap;
    (void)item_tiles(id, items, {.selection = sel, .height = h, .tile_width = kTile});
}

// The weekly drop card, the hero of the tab: the answer as a heading (ready, claimed, not
// earned yet), the countdown to the reset, then what belongs to the answer: the items to pick
// and the claim action, the items picked, or the XP still missing.
void weekly_drop_card(app::AppState& state, core::Account& acct, Cs2ScreenState& s, bool loading, float width,
                      bool pair) {
    facet::BeginCard("Weekly drop", {.icon = icons::weekly_drop, .width = width});
    const EqualHeight eq("##drop-h", "##mission-h", pair);
    const cs2_gc::WeeklyReward& reward = s.snapshot.reward;
    if (loading || !reward.loaded) {
        facet::Skeleton({.width = 220.0F, .height = 28.0F});
        facet::Skeleton({.lines = 2});
        note("Waiting for the weekly drop from CS2");
    } else if (reward.available) {
        const std::size_t cap = reward.redeemable_balance;
        const bool claim_all = reward.items.size() <= cap;
        drop_headline("Ready to claim");
        char line[128];
        if (claim_all)
            std::snprintf(line, sizeof(line), "%zu item%s waiting. Claiming adds %s to the inventory.", reward.items.size(),
                          reward.items.size() == 1 ? "" : "s", reward.items.size() == 1 ? "it" : "them");
        else
            std::snprintf(line, sizeof(line), "Pick %zu of these %zu items. You get the ones you pick.",
                          cap, reward.items.size());
        facet::StyledText(line, {.wrap = true});

        std::vector<const cs2_gc::DisplayItem*> items;
        for (const cs2_gc::DisplayItem& it : reward.items) items.push_back(&it);
        // Never more picks than can be claimed: the newest pick over the cap is dropped.
        if (!claim_all) {
            std::vector<const cs2_gc::DisplayItem*> picked = selected_items(items, s.reward_selection);
            while (picked.size() > cap) {
                s.reward_selection.SetItemSelected(item_key(picked.back()->id), false);
                picked.pop_back();
            }
        }
        tile_block("##reward", items, claim_all ? nullptr : &s.reward_selection);

        std::vector<std::uint64_t> ids;
        if (claim_all) {
            ids = reward.item_ids;
            if (ids.size() > cap) ids.resize(cap);
        } else {
            for (const cs2_gc::DisplayItem* it : selected_items(items, s.reward_selection)) ids.push_back(it->id);
        }
        // The game hands out the whole drop at once: the claim waits until every pick is made.
        const std::size_t need = claim_all ? ids.size() : cap;
        char label[48];
        std::snprintf(label, sizeof(label), need == 1 ? "Claim 1 item###claim" : "Claim %zu items###claim", need);
        const bool busy = s.pending == Cs2Pending::Claim;
        ImGui::BeginDisabled(ids.size() != need || need == 0 || (s.pending != Cs2Pending::None && !busy) ||
                             s.phase != Cs2Phase::Connected);
        if (facet::Button(label, {.variant = facet::ButtonVariant::Primary, .loading = busy})) claim(s, std::move(ids));
        ImGui::EndDisabled();
        if (!claim_all) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            note("%zu of %zu picked", std::min(ids.size(), cap), cap);
        }
        if (!s.claim_error.empty())
            (void)facet::InlineAlert("The drop was not claimed", nullptr,
                                     "CS2 did not confirm the claim. Refresh, then try again.",
                                     {.tone = facet::Tone::Destructive});
        if (!s.claim_error.empty()) facet::ItemTooltip("Details: %s", s.claim_error.c_str());
    } else if (drop_claimed_this_week(reward)) {
        drop_headline("Claimed this week", true);
        std::vector<cs2_gc::DisplayItem> picked;
        std::uint64_t n = 0;
        for (const std::string& name : reward.claimed_names) {
            if (name.empty()) continue;
            cs2_gc::DisplayItem d;
            d.id = ++n;
            d.name = name;
            picked.push_back(std::move(d));
        }
        if (!picked.empty()) {
            note("You picked");
            std::vector<const cs2_gc::DisplayItem*> ptrs;
            for (const cs2_gc::DisplayItem& d : picked) ptrs.push_back(&d);
            tile_block("##picked", ptrs, nullptr);
        } else {
            facet::StyledText("The items were picked outside this app.", {.wrap = true});
        }
    } else {
        drop_headline("Not earned yet");
        facet::StyledText("The weekly drop unlocks with the next level up this week.", {.wrap = true});
        const cs2_gc::PlayerProgress& p = s.snapshot.progress;
        if (p.valid && p.xp_per_level > 0) {
            const std::string bar =
                thousands(p.xp_per_level - p.xp_in_level) + " XP to level " + std::to_string(p.level + 1) + "###xp";
            facet::ProgressBar(bar.c_str(), static_cast<float>(p.xp_in_level) / static_cast<float>(p.xp_per_level));
        }
    }
    manual_mark(state, acct);
    eq.finish();
    facet::EndCard();
}

void mission_card(const Cs2ScreenState& s, bool loading, float width, bool pair) {
    facet::BeginCard("Weekly mission", {.icon = facet::Icon("star"), .width = width});
    const EqualHeight eq("##mission-h", "##drop-h", pair);
    const cs2_gc::WeeklyMission& m = s.snapshot.mission;
    if (loading) {
        facet::Skeleton({.lines = 3});
    } else if (!m.valid) {
        facet::StyledText("CS2 has no weekly mission for this account right now.", {.secondary = true, .wrap = true});
    } else {
        if (!m.name.empty()) facet::Text(facet::TextStyle::Headline, "%s", m.name.c_str());
        if (m.target > 0) {
            const bool done = m.progress >= m.target;
            const std::string label = thousands(m.progress) + " of " + thousands(m.target) + "###mission";
            facet::ProgressBar(label.c_str(), static_cast<float>(m.progress) / static_cast<float>(m.target),
                               {.tone = done ? facet::Tone::Success : facet::Tone::Accent});
        } else {
            facet::Text(facet::TextStyle::Body, "Progress: %s", thousands(m.progress).c_str());
        }
        if (m.xp > 0)
            note("Rewards %s XP. Progress comes from playing.", thousands(m.xp).c_str());
        else
            note("Progress comes from playing.");
    }
    eq.finish();
    facet::EndCard();
}

// The account facts as one full-width row of five stat tiles (it wraps in a narrow window).
// All five are always there, loading or not, so the row never changes shape; a value CS2 did
// not report reads "Unknown". The live snapshot wins over the account's cache.
void profile_tiles(const core::Account& acct, const Cs2ScreenState& s, bool loading) {
    const facet::DataState data = loading ? facet::DataState::Loading : facet::DataState::Ready;
    const cs2_gc::PlayerProgress& p = s.snapshot.progress;
    const int level = p.valid ? p.level : acct.cs2.cs2_player_level;
    const int to_next = p.valid && p.xp_per_level > 0 ? p.xp_per_level - p.xp_in_level : -1;
    const int premier = s.snapshot.ranks.premier_rating >= 0 ? s.snapshot.ranks.premier_rating : acct.cs2.premier_rating;
    const int wingman = s.snapshot.ranks.wingman_rank >= 0 ? s.snapshot.ranks.wingman_rank : acct.cs2.wingman_rank;
    const facet::StatTileOptions opt{.formatter = &count_or_unknown, .state = data};

    facet::BeginGrid("##profile", {.minCellWidth = 150.0F, .maxColumns = 5, .gap = facet::Gap::Item});
    facet::GridNext();
    facet::StatTile("Level", level, opt);
    facet::GridNext();
    facet::StatTile("XP to next level", to_next, opt);
    facet::GridNext();
    facet::StatTile("Premier rating", premier, opt);
    facet::GridNext();
    facet::StatTile("Wingman rank", wingman, opt);
    facet::GridNext();
    facet::StatTile("Medals and coins", static_cast<double>(s.snapshot.medals.size()), opt);
    facet::EndGrid();

    const std::int64_t now = now_seconds();
    if (kit::cooldown_active(acct, now)) (void)kit::cooldown_pill(acct, now);
}

}  // namespace

// A small dashboard: the weekly drop (the question most visits ask) is the hero card, the
// mission card stands beside it at the same height, and the profile facts run as one row of
// stat tiles across the full width below both. One column when narrow. Errors come last, so
// nothing above them moves when one appears.
void draw_overview(app::AppState& state, core::Account& acct, Cs2ScreenState& s) {
    if (!facet::BeginScrollArea("##overview")) return;
    const bool loading = s.phase == Cs2Phase::Connecting || !s.has_snapshot;
    const auto& gc = state.settings.cs2_gc;
    const float avail = ImGui::GetContentRegionAvail().x / facet::Px(1.0F) - facet::T().spacing.xs;
    const float gap = facet::T().spacing.lg;
    const bool both = gc.show_weekly_drop && gc.show_weekly_mission;
    const bool wide = both && avail >= 640.0F;
    const float hero_w = wide ? std::floor((avail - gap) * 0.62F) : avail;
    const float side_w = wide ? avail - gap - hero_w : avail;

    if (gc.show_weekly_drop) {
        ImGui::BeginGroup();
        weekly_drop_card(state, acct, s, loading, hero_w, wide);
        ImGui::EndGroup();
    }
    if (gc.show_weekly_mission) {
        if (wide)
            ImGui::SameLine(0.0F, facet::Px(gap));
        else if (gc.show_weekly_drop)
            ImGui::Dummy(ImVec2(0.0F, facet::Px(gap)));
        ImGui::BeginGroup();
        mission_card(s, loading, side_w, wide);
        ImGui::EndGroup();
    }
    if (gc.show_weekly_drop || gc.show_weekly_mission) ImGui::Dummy(ImVec2(0.0F, facet::Px(gap)));
    profile_tiles(acct, s, loading);

    if (!s.action_error.empty() && s.action_alert_open) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(gap)));
        if (facet::InlineAlert("Something went wrong", &s.action_alert_open,
                               "CS2 did not finish the last request. Refresh, then try again.",
                               {.tone = facet::Tone::Destructive}) == facet::InlineAlertAction::Dismissed)
            s.action_error.clear();
        else
            facet::ItemTooltip("Details: %s", s.action_error.c_str());
    }
    facet::EndScrollArea();
}

}  // namespace sam::ui::screens::cs2