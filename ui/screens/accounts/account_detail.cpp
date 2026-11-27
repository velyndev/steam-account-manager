// The account detail: the right pane of the list view and the inspector of the table view.
// A hero header (identity and sign-in) that never moves, one alert block for whatever is wrong,
// then a grid of cards: standing, CS2, Steam, notes, recent changes. Everything honors the
// settings.info toggles.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/draw.h>
#include <facet/core/image.h>
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
#include <facet/widgets/selection.h>

#include "core/account_store/ban_event.hpp"
#include "core/account_store/store.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/standing.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"
#include "ui/util.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::screens::accounts {

namespace {

constexpr float kAvatarUnits = 68.0F;
constexpr float kRankUnits = 34.0F;
constexpr float kMedalUnits = 32.0F;
constexpr float kWideHeaderUnits = 640.0F;
constexpr float kCardMinUnits = 280.0F;

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string days_text(std::int64_t seconds) {
    char buf[32];
    if (seconds >= 86400) {
        std::snprintf(buf, sizeof(buf), "%lld days", static_cast<long long>(seconds / 86400));
    } else if (seconds >= 3600) {
        std::snprintf(buf, sizeof(buf), "%lld hours", static_cast<long long>(seconds / 3600));
    } else {
        std::snprintf(buf, sizeof(buf), "%lld minutes", static_cast<long long>(std::max<std::int64_t>(1, seconds / 60)));
    }
    return buf;
}

void gap(float units) {
    ImGui::Dummy(ImVec2(0.0F, facet::Px(units)));
}

// A dot in a user-assigned data color followed by text: how a tag reads (not interactive).
void data_dot_label(std::uint32_t rgba, const char* text) {
    const float d = facet::Px(facet::T().metrics.statusDotSize);
    const float g = facet::Px(facet::T().spacing.xs);
    ImGui::Dummy(ImVec2(d, 1.0F));
    ImGui::SameLine(0.0F, g);
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true});
    const ImVec2 tmin = ImGui::GetItemRectMin();
    const float cy = (tmin.y + ImGui::GetItemRectMax().y) * 0.5F;
    const float x = tmin.x - g - d;
    facet::FillRoundRect(ImGui::GetWindowDrawList(), ImVec2(x, cy - d * 0.5F), ImVec2(x + d, cy + d * 0.5F),
                         kit::data_color(rgba), d);
}

// --- Cards ----------------------------------------------------------------------------------------

// A card of the detail grid: a titled surface whose rows form a grouped list. `action` adds a
// trailing link-style action to the title; returns true when it was activated.
// The grid cell pushes its width as the item width; inside the card's padding that is too wide,
// so the card's content gets "fill what is left" pushed instead.
float g_card_width = 0.0F;  // units of the column the cards are laid out in; 0 fills

bool begin_card(const char* id, const char* title, const char* action = nullptr) {
    facet::BeginCard(id, {.width = g_card_width});
    ImGui::PushItemWidth(-FLT_MIN);
    return facet::SectionHeader(title, {.actionLabel = action});
}

void end_card() {
    ImGui::PopItemWidth();
    facet::EndCard();
}

// The hairline between two rows of a card.
void row_rule() {
    facet::Separator();
}

// A settings-style row for a control or a mark, and a plain text value on the label's baseline.
void row_text(const char* text, bool secondary = true) {
    ImGui::AlignTextToFramePadding();
    facet::StyledText(text, {.secondary = secondary, .wrap = true});
}

// Small pills in a settings row: centered on the row's control line, where the label sits.
void pill_line() {
    const float offset = (facet::ControlHeight(facet::ControlSize::Regular) -
                          facet::ControlHeight(facet::ControlSize::Small)) * 0.5F;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
}

// --- Status line ----------------------------------------------------------------------------------

bool any_ban_shown(const app::AppState& state) {
    const auto& info = state.settings.info;
    return info.show_vac || info.show_game_ban || info.show_trade_ban || info.show_community_ban || info.show_vac_live;
}

// Why a reserved line: the active problems (bans, a cooldown, a revoked token) sit right under
// the name as compact pills, in a line that is always there, so nothing below moves when an
// account has none. The Standing card carries the details.
void status_line(const app::AppState& state, const core::Account& a, std::int64_t now) {
    const float h = facet::ControlHeight(facet::ControlSize::Small);
    const float g = facet::Px(facet::T().spacing.xs);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.0F, h));
    int drawn = 0;
    if (kit::any_ban(state, a)) {
        ImGui::SameLine(0.0F, 0.0F);
        const std::string text = kit::ban_summary(state, a) + "###ban-status";
        facet::StatusPill(text.c_str(), {.tone = kit::ban_tone(state, a), .icon = icons::ban,
                                         .size = facet::ControlSize::Small});
        ++drawn;
    }
    if (state.settings.info.show_cooldown && kit::cooldown_active(a, now)) {
        ImGui::SameLine(0.0F, drawn > 0 ? g : 0.0F);
        kit::cooldown_pill(a, now);
        ++drawn;
    }
    if (a.is_nfa && a.nfa_status == core::NfaTokenStatus::Revoked) {
        ImGui::SameLine(0.0F, drawn > 0 ? g : 0.0F);
        facet::StatusPill("Sign-in token revoked###token-status",
                          {.tone = facet::Tone::Destructive, .icon = icons::token, .size = facet::ControlSize::Small});
        facet::ItemTooltip("This account cannot sign in. Import a fresh token or add it again with its password.");
        ++drawn;
    }
    if (drawn == 0) {
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::AlignTextToFramePadding();
        facet::StyledText(any_ban_shown(state) ? "No bans, no cooldown" : "", {.style = facet::TextStyle::Footnote,
                                                                              .secondary = true});
    }
    ImGui::EndGroup();
}

// --- Hero -----------------------------------------------------------------------------------------

void move_menu(app::AppState& state, core::Account& a) {
    int current = -1;
    for (std::size_t i = 0; i < state.vault.groups.size(); ++i) {
        if (state.vault.groups[i].id == a.group_id) current = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < state.vault.groups.size(); ++i) {
        const core::Group& g = state.vault.groups[i];
        ImGui::PushID(g.id.c_str());
        if (facet::MenuRadioItem(g.name.c_str(), &current, static_cast<int>(i))) move_to_group(state, {a.id}, g.id);
        ImGui::PopID();
    }
    if (facet::MenuRadioItem("No group", &current, -1)) move_to_group(state, {a.id}, std::string());
    if (facet::MenuItem("New group...")) request_new_group();
}

void detail_more_menu(app::AppState& state, core::Account& a) {
    ScreenState& s = screen();
    if (facet::IconButton("More actions for this account", facet::Icon("more-horizontal"),
                          {.selected = s.detail_more_open})) {
        s.detail_more_open = !s.detail_more_open;
    }
    if (facet::BeginPopover("##detail-more", &s.detail_more_open,
                            {.placement = facet::PopoverPlacement::Below, .arrow = false})) {
        if (facet::MenuItem("Edit account", {.icon = facet::Icon("edit")})) request_edit(state, a);
        if (facet::MenuItem("Edit notes", {.icon = facet::Icon("file")})) request_notes(state, a);
        if (a.steam_id_64 != 0 && facet::MenuItem("Change Steam name", {.icon = facet::Icon("user")})) {
            request_username(state, a);
        }
        if (facet::BeginMenu("Move to group", {.icon = icons::group})) {
            move_menu(state, a);
            facet::EndMenu();
        }
        if (a.sda.has_value() && facet::MenuItem("Open in Authenticator", {.icon = icons::authenticator})) {
            state.selected_account_id = a.id;
            kit::go_to(state, app::Screen::Authenticator);
        }
        if (facet::MenuItem("Export...", {.icon = facet::Icon("upload")})) request_export({a.id});
        if (facet::MenuItem("Remove account...", {.icon = facet::Icon("trash"), .destructive = true})) {
            request_remove(state, a.id);
        }
        facet::EndPopover();
    }
}

void actions_row(app::AppState& state, core::Account& a) {
    const float g = facet::Px(facet::T().spacing.sm);
    sign_in_button(state, a);
    if (a.sda.has_value()) {
        ImGui::SameLine(0.0F, g);
        if (facet::Button("Copy code", {.icon = icons::authenticator})) copy_code(state, a);
        facet::ItemTooltip("Copies the current Steam Guard code. The clipboard clears after %d seconds.",
                           state.settings.clipboard_clear_seconds);
    }
    ImGui::SameLine(0.0F, g);
    std::string reason;
    const bool refreshing = state.refreshing_ids.count(a.id) != 0;
    const bool can = can_refresh(state, a, &reason);
    ImGui::BeginDisabled(!can && !refreshing);
    if (facet::IconButton("Refresh this account", facet::Icon("refresh"),
                          {.variant = facet::ButtonVariant::Secondary, .loading = refreshing})) {
        refresh_account(state, a);
    }
    ImGui::EndDisabled();
    if (!can) facet::ItemTooltip("%s", reason.c_str());
    ImGui::SameLine(0.0F, g);
    detail_more_menu(state, a);
}

// Why the actions move under the name in a narrow pane: four controls beside a 22 unit name do
// not fit 500 units, and a wrapped header reads worse than two clean rows.
void hero(app::AppState& state, core::Account& a) {
    const facet::Theme& t = facet::T();
    const bool wide = facet::AvailableWidthUnits() >= kWideHeaderUnits;
    static float actions_w = 0.0F;  // pixels, from the last frame

    const ImVec2 top = ImGui::GetCursorPos();
    const float right = top.x + ImGui::GetContentRegionAvail().x;
    kit::avatar(state, a, {.diameter = kAvatarUnits});
    ImGui::SameLine(0.0F, facet::Px(t.spacing.lg));
    const float block_px = ImGui::GetContentRegionAvail().x - (wide ? actions_w + facet::Px(t.spacing.lg) : 0.0F);
    ImGui::BeginGroup();
    const std::string name = kit::display_name(state, a);
    facet::TruncatedText(name.c_str(), {.style = facet::TextStyle::Title1, .focusable = true,
                                        .width = std::max(40.0F, block_px / facet::Scale())});

    status_line(state, a, std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch()).count());

    // Login, kind, last sign-in, country and what signing in does, as one quiet line.
    bool line = false;
    if (!a.web.persona_name.empty() || kit::login_hidden(state, a)) {
        kit::login(state, a);
        line = true;
    }
    if (core::store::is_cached_account(a) || a.is_nfa) {
        if (line) ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
        kit::account_kind_badge(a);
        line = true;
    }
    std::string meta = a.last_login_unix > 0 ? "Last sign-in " + format_relative(a.last_login_unix)
                                             : std::string("Never signed in from this app");
    if (!a.web.country_code.empty()) meta += " \xC2\xB7 " + upper(a.web.country_code);
    const core::LoginMethod method = core::effective_login_method(a.login_method, state.settings.safe_mode);
    if (const char* what = login_method_badge(method)) meta += std::string(" \xC2\xB7 ") + what;
    if (line) {
        ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
        meta = "\xC2\xB7 " + meta;
    }
    facet::StyledText(meta.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});

    // Group and tags in one style: a dot in the user's color and the name.
    gap(t.spacing.xs);
    const core::Group* group = nullptr;
    for (const core::Group& g : state.vault.groups) {
        if (g.id == a.group_id) group = &g;
    }
    bool first_label = true;
    if (group != nullptr) {
        data_dot_label(group->color_rgba, group->name.c_str());
        facet::ItemTooltip("Group. Move it from the More actions menu.");
        first_label = false;
    }
    for (const std::string& tag_id : a.tag_ids) {
        if (tag_id == core::store::kCachedTagId) continue;
        for (const core::Tag& tag : state.vault.tags) {
            if (tag.id != tag_id) continue;
            if (!first_label) ImGui::SameLine(0.0F, facet::Px(t.spacing.md));
            first_label = false;
            ImGui::PushID(tag.id.c_str());
            data_dot_label(tag.color_rgba, tag.name.c_str());
            facet::ItemTooltip("Tag");
            ImGui::PopID();
        }
    }
    if (first_label) facet::StyledText("No group", {.style = facet::TextStyle::Footnote, .secondary = true});
    ImGui::EndGroup();

    if (wide) {
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(std::max(top.x, right - actions_w), top.y + facet::Px(t.spacing.xs)));
        ImGui::BeginGroup();
        actions_row(state, a);
        ImGui::EndGroup();
        actions_w = ImGui::GetItemRectSize().x;
        ImGui::SetCursorPos(ImVec2(top.x, std::max(after.y, ImGui::GetCursorPosY())));
    } else {
        gap(t.spacing.md);
        ImGui::BeginGroup();
        actions_row(state, a);
        ImGui::EndGroup();
        actions_w = ImGui::GetItemRectSize().x;
    }
    if (state.cleaner_busy.load()) {
        facet::StyledText("Signing in is paused while the cleaner runs.",
                          {.style = facet::TextStyle::Footnote, .secondary = true});
    }
}

// --- Standing -------------------------------------------------------------------------------------

void standing_card(app::AppState& state, core::Account& a, std::int64_t now) {
    const auto& info = state.settings.info;
    begin_card("##standing", "Standing");
    bool first = true;
    auto rule = [&] {
        if (!first) row_rule();
        first = false;
    };
    // The details of what the status line under the name shows: when, until and why.
    if (any_ban_shown(state)) {
        rule();
        const core::BanStatus& b = a.bans;
        std::string text = kit::ban_summary(state, a);
        if (kit::any_ban(state, a) && (b.vac_banned || b.game_ban_count > 0) && b.days_since_last_ban > 0) {
            text += ", the last " + std::to_string(b.days_since_last_ban) + " days ago";
        }
        if (b.last_refreshed_unix > 0) text += ". Checked " + format_relative(b.last_refreshed_unix) + ".";
        facet::BeginSettingsRow("Bans");
        row_text(text.c_str(), !kit::any_ban(state, a));
        facet::EndSettingsRow();
    }
    if (info.show_cooldown) {
        rule();
        facet::BeginSettingsRow("Cooldown");
        if (!kit::cooldown_active(a, now)) {
            row_text("None");
        } else {
            std::string text = a.cs2.cooldown_expires_unix == core::kCooldownNever
                                   ? std::string("Permanent")
                                   : "Until " + format_date(a.cs2.cooldown_expires_unix) + " (UTC)";
            if (!a.cs2.cooldown_reason.empty()) text += ", reason: " + a.cs2.cooldown_reason;
            row_text(text.c_str(), false);
        }
        facet::EndSettingsRow();
    }
    rule();
    facet::BeginSettingsRow("Trust label");
    if (kit::trust_dropdown("##trust", &a.trust)) {
        state.vault_dirty = true;
        state.save_vault_if_dirty();
    }
    facet::EndSettingsRow();
    if (a.is_nfa && a.refresh_token_expires > 0) {
        rule();
        const std::int64_t remaining = a.refresh_token_expires - now;
        facet::BeginSettingsRow("Sign-in token");
        if (remaining <= 0) {
            pill_line();
            facet::StatusPill("Expired###token", {.tone = facet::Tone::Destructive, .icon = icons::token,
                                                  .size = facet::ControlSize::Small});
        } else if (remaining < 7 * 86400) {
            pill_line();
            const std::string text = "Expires in " + days_text(remaining) + "###token";
            facet::StatusPill(text.c_str(), {.tone = facet::Tone::Warning, .icon = icons::token,
                                             .size = facet::ControlSize::Small});
        } else {
            row_text(("Expires in " + days_text(remaining)).c_str(), false);
        }
        facet::EndSettingsRow();
    }
    if (a.is_nfa && a.nfa_status != core::NfaTokenStatus::Unknown) {
        rule();
        const bool revoked = a.nfa_status == core::NfaTokenStatus::Revoked;
        std::string text = revoked ? "Revoked" : "Valid";
        if (a.nfa_last_validated_unix > 0) text += ", checked " + format_relative(a.nfa_last_validated_unix);
        facet::BeginSettingsRow("Token check");
        row_text(text.c_str(), !revoked);
        facet::EndSettingsRow();
    }
    end_card();
}

// --- CS2 ------------------------------------------------------------------------------------------

void rank_tile(const char* id, const char* caption, int wins, bool premier, int value) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    facet::StyledText(caption, {.style = facet::TextStyle::Footnote, .secondary = true});
    gap(facet::T().spacing.xs);
    const bool drew = premier ? kit::premier_rank(value, kRankUnits) : kit::wingman_rank(value, kRankUnits);
    if (!drew) facet::Text(facet::TextStyle::Title3, "%s", value > 0 ? std::to_string(value).c_str() : "Unranked");
    gap(facet::T().spacing.xs);
    char buf[32];
    if (wins >= 0) std::snprintf(buf, sizeof(buf), "%d wins", wins);
    else std::snprintf(buf, sizeof(buf), "No wins yet");
    facet::StyledText(buf, {.style = facet::TextStyle::Footnote, .secondary = true});
    ImGui::EndGroup();
    ImGui::PopID();
}

std::string medal_image_url(const std::string& url) {
    if (url.empty() || url.find("/economy/image/") == std::string::npos) return url;
    return url + "/256fx256f";
}

bool cs2_card_shown(const app::AppState& state, const core::Account& a) {
    const auto& info = state.settings.info;
    const core::CS2Status& c = a.cs2;
    return info.show_premier || info.show_wingman || info.show_weekly_drop || c.cs2_player_level >= 0 ||
           c.cs2_player_xp >= 0 || !c.medals.empty();
}

void cs2_card(app::AppState& state, core::Account& a, std::int64_t now) {
    const auto& info = state.settings.info;
    const core::CS2Status& c = a.cs2;
    begin_card("##cs2", "CS2");
    const bool any_data = c.cs2_player_level >= 0 || c.premier_rating >= 0 || c.wingman_rank >= 0 ||
                          !c.medals.empty() || c.last_refreshed_unix > 0 || c.gc_last_pulled_unix > 0;
    if (!any_data) {
        facet::StyledText("No CS2 data yet. Refresh CS2 data from the Refresh menu to load ranks, level and medals.",
                          {.secondary = true, .wrap = true});
    }
    bool first = true;
    auto rule = [&] {
        if (!first) row_rule();
        first = false;
    };
    if (any_data && (info.show_premier || info.show_wingman)) {
        first = false;
        facet::BeginGrid("##ranks", {.minCellWidth = 120.0F, .maxColumns = 2, .gap = facet::Gap::Loose});
        if (info.show_premier) {
            facet::GridNext();
            rank_tile("premier", "Premier", c.premier_wins, true, c.premier_rating);
        }
        if (info.show_wingman) {
            facet::GridNext();
            rank_tile("wingman", "Wingman", c.wingman_wins, false, c.wingman_rank);
        }
        facet::EndGrid();
        gap(facet::T().spacing.sm);
    }
    if (c.cs2_player_level >= 0) {
        rule();
        facet::BeginSettingsRow("CS2 level");
        row_text(std::to_string(c.cs2_player_level).c_str(), false);
        facet::EndSettingsRow();
    }
    if (c.cs2_player_xp >= 0) {
        rule();
        facet::BeginSettingsRow("XP");
        row_text(kit::format_count(c.cs2_player_xp).c_str(), false);
        facet::EndSettingsRow();
    }
    if (info.show_weekly_drop) {
        rule();
        facet::BeginSettingsRow("Weekly drop");
        if (state.settings.info.show_weekly_drop && kit::weekly_drop_claimed(a, now)) {
            pill_line();
            kit::weekly_drop_badge(state, a, now);
        } else {
            row_text("Not claimed this week");
        }
        facet::EndSettingsRow();
    }
    if (!c.medals.empty()) {
        rule();
        gap(facet::T().spacing.xs);
        facet::StyledText("Medals", {.style = facet::TextStyle::Footnote, .secondary = true});
        facet::BeginFlow("##medals", {.gap = facet::Gap::Tight});
        for (std::size_t i = 0; i < c.medals.size(); ++i) {
            if (!facet::FlowNext()) continue;
            const core::CS2Medal& m = c.medals[i];
            ImGui::PushID(static_cast<int>(i));
            const float d = facet::Px(kMedalUnits);
            if (auto* srv = widgets::texture_for(medal_image_url(m.icon_url))) {
                facet::Image(srv, ImVec2(d, d), {.fit = facet::ImageFit::Contain, .showStates = false});
            } else {
                facet::Skeleton({.shape = facet::SkeletonShape::Rect, .width = kMedalUnits, .height = kMedalUnits});
            }
            if (!m.name.empty()) facet::ItemTooltip("%s", m.name.c_str());
            ImGui::PopID();
        }
        facet::EndFlow();
    }
    end_card();
}

// --- Steam ----------------------------------------------------------------------------------------

struct TileSpec {
    const char* label;
    double value;
    const char* suffix;  // after the number ("" for none)
    bool money;          // dollars: "$" in front and cents
};

// Stat tile values with thousands separators (kit::format_count), dollars with cents.
int tile_format(char* buffer, int size, double value, void* user) {
    const auto* tile = static_cast<const TileSpec*>(user);
    std::string text;
    if (tile != nullptr && tile->money) {
        const long long cents = static_cast<long long>(value * 100.0 + 0.5);
        char tail[8];
        std::snprintf(tail, sizeof(tail), ".%02lld", cents % 100);
        text = "$" + kit::format_count(cents / 100) + tail;
    } else {
        text = kit::format_count(static_cast<long long>(value + 0.5));
    }
    if (tile != nullptr && tile->suffix != nullptr) text += tile->suffix;
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s", text.c_str());
}

bool steam_card_shown(const app::AppState& state) {
    const auto& info = state.settings.info;
    return info.show_steam_level || info.show_owned_games || info.show_external_funds || info.show_steam_id ||
           info.show_prime;
}

void steam_card(app::AppState& state, core::Account& a) {
    const auto& info = state.settings.info;
    begin_card("##steam", "Steam");
    std::int64_t last_refresh = 0;
    auto fold = [&](std::int64_t t) {
        if (t > 0) last_refresh = last_refresh == 0 ? t : std::min(last_refresh, t);
    };
    fold(a.web.last_refreshed_unix);
    fold(a.bans.last_refreshed_unix);
    if (state.settings.gcpd_enabled && !a.session_id.empty() && !a.steam_login_secure.empty()) {
        fold(a.cs2.last_refreshed_unix);
    }
    if (last_refresh == 0) {
        facet::StyledText("No Steam data yet. Refresh the account to load it.", {.secondary = true, .wrap = true});
        gap(facet::T().spacing.sm);
    }

    // The numbers worth a glance as tiles, in a grid that reflows with the card width.
    using Tile = TileSpec;
    std::vector<Tile> tiles;
    if (info.show_steam_level && a.web.steam_level >= 0) tiles.push_back({"Steam level", double(a.web.steam_level), "", false});
    if (info.show_owned_games && a.web.owned_games_count >= 0) {
        tiles.push_back({"Games", double(a.web.owned_games_count), "", false});
        if (a.web.total_playtime_minutes > 0) {
            tiles.push_back({"Hours played", double(a.web.total_playtime_minutes / 60), "", false});
        }
    }
    const std::string currency = a.funds.currency_is_usd ? std::string() : " " + a.funds.currency;
    if (info.show_external_funds && a.funds.total_spend_usd_cents >= 0) {
        tiles.push_back({"Spent", double(a.funds.total_spend_usd_cents) / 100.0, currency.c_str(), a.funds.currency_is_usd});
    }
    if (!tiles.empty()) {
        facet::BeginGrid("##steam-tiles", {.minCellWidth = 112.0F, .maxColumns = 4});
        for (const Tile& tile : tiles) {
            facet::GridNext();
            ImGui::PushID(tile.label);
            const std::string id = std::string(tile.label) + "###" + a.id;
            facet::StatTile(id.c_str(), tile.value, {.formatter = &tile_format, .formatterUser = const_cast<Tile*>(&tile)});
            ImGui::PopID();
        }
        facet::EndGrid();
        gap(facet::T().spacing.sm);
    }

    bool first = true;
    auto rule = [&] {
        if (!first) row_rule();
        first = false;
    };
    if (info.show_prime && (a.cs2.prime_confirmed || a.cs2.cs2_player_level >= 0)) {
        rule();
        facet::BeginSettingsRow("Prime");
        row_text(kit::prime_detail_text(state, a).c_str(), !(a.cs2.prime_confirmed && a.cs2.prime_status));
        facet::EndSettingsRow();
    }
    if (info.show_steam_id && a.steam_id_64 != 0) {
        rule();
        char sid[24];
        std::snprintf(sid, sizeof(sid), "%llu", static_cast<unsigned long long>(a.steam_id_64));
        facet::BeginSettingsRow("Steam ID");
        facet::InlineCode(sid, {.copyOnActivate = true});
        facet::ItemTooltip("Copy the SteamID64");
        facet::EndSettingsRow();
    }
    if (a.web.created_unix > 0) {
        rule();
        facet::BeginSettingsRow("Created");
        row_text(format_date(a.web.created_unix).c_str(), false);
        facet::EndSettingsRow();
    }
    if (last_refresh > 0) {
        rule();
        facet::BeginSettingsRow("Last refreshed");
        row_text(format_relative(last_refresh).c_str(), false);
        facet::EndSettingsRow();
    }
    end_card();
}

// --- Notes and events -----------------------------------------------------------------------------

void notes_card(app::AppState& state, core::Account& a) {
    if (begin_card("##notes", "Notes", "Edit")) request_notes(state, a);
    if (a.notes.empty()) {
        facet::StyledText("No notes yet.", {.secondary = true});
    } else {
        const std::string text = to_utf8(a.notes);
        facet::StyledText(text.c_str(), {.wrap = true});
    }
    end_card();
}

void events_card(app::AppState& state, core::Account& a, const std::vector<core::BanEvent>& events) {
    if (begin_card("##recent", "Recent changes", "Acknowledge all")) {
        state.notifications.acknowledge_all_for(a.id);
        end_card();
        return;
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        const core::BanEvent& ev = events[i];
        if (i > 0) row_rule();
        ImGui::PushID(ev.event_id.c_str());
        const bool cooldown = ev.kind == core::BanEventKind::CooldownStarted || ev.kind == core::BanEventKind::CooldownEnded;
        const bool good = ev.kind == core::BanEventKind::BanRemoved || ev.kind == core::BanEventKind::CooldownEnded;
        const std::string label = std::string(core::ban_event_kind_label(ev.kind)) + "###kind";
        ImGui::AlignTextToFramePadding();
        facet::StatusPill(label.c_str(),
                          {.tone = good ? facet::Tone::Success : (cooldown ? facet::Tone::Warning : facet::Tone::Destructive),
                           .icon = cooldown ? facet::Icon("clock") : icons::ban,
                           .size = facet::ControlSize::Small});
        ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.sm));
        facet::StyledText(format_relative(ev.detected_unix).c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
        ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.sm));
        if (facet::Button("Dismiss", {.variant = facet::ButtonVariant::Plain, .size = facet::ControlSize::Small})) {
            state.notifications.acknowledge(ev.event_id);
        }
        ImGui::PopID();
    }
    end_card();
}

// Why two independent columns and not a row-aligned grid: the cards differ a lot in height, and
// a grid row as tall as its tallest card leaves a hole under the short one. Standing, Steam and
// Notes stack on the left, CS2 and Recent changes on the right; a narrow pane has one column.
void cards(app::AppState& state, core::Account& a, std::int64_t now) {
    std::vector<core::BanEvent> events;
    if (state.settings.notifications.enabled) events = state.notifications.unacked_for(a.id);
    const float gap_units = facet::T().spacing.lg;
    const float avail = facet::AvailableWidthUnits();
    const bool two = avail >= 2.0F * kCardMinUnits + gap_units;
    const float col = two ? (avail - gap_units) * 0.5F : avail;

    auto left = [&] {
        standing_card(state, a, now);
        if (steam_card_shown(state)) {
            gap(gap_units);
            steam_card(state, a);
        }
        if (!state.settings.hide_notes) {
            gap(gap_units);
            notes_card(state, a);
        }
    };
    auto right = [&] {
        bool any = false;
        if (cs2_card_shown(state, a)) {
            cs2_card(state, a, now);
            any = true;
        }
        if (!events.empty()) {
            if (any) gap(gap_units);
            events_card(state, a, events);
            any = true;
        }
        if (!any) ImGui::Dummy(ImVec2(0.0F, 0.0F));
    };
    g_card_width = col;
    if (two) {
        ImGui::BeginGroup();
        left();
        ImGui::EndGroup();
        ImGui::SameLine(0.0F, facet::Px(gap_units));
        ImGui::BeginGroup();
        right();
        ImGui::EndGroup();
    } else {
        standing_card(state, a, now);
        if (cs2_card_shown(state, a)) {
            gap(gap_units);
            cs2_card(state, a, now);
        }
        if (steam_card_shown(state)) {
            gap(gap_units);
            steam_card(state, a);
        }
        if (!state.settings.hide_notes) {
            gap(gap_units);
            notes_card(state, a);
        }
        if (!events.empty()) {
            gap(gap_units);
            events_card(state, a, events);
        }
    }
    g_card_width = 0.0F;
}

void draw_account(app::AppState& state, core::Account& a) {
    const std::int64_t now = now_unix();
    ImGui::PushID(a.id.c_str());
    hero(state, a);
    gap(facet::T().spacing.xl);
    cards(state, a, now);
    gap(facet::T().spacing.xl);
    ImGui::PopID();
}

// Why the bulk actions live here: with several accounts selected the detail pane has nothing to
// show, so it becomes the place to act on the selection, as a compact list of actions.
void draw_selection_actions(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    const std::size_t n = state.selected_account_ids.size();
    facet::Text(facet::TextStyle::Title1, "%s accounts selected", kit::format_count(static_cast<long long>(n)).c_str());
    const int out_of_view = static_cast<int>(n) - s.selected_in_view;
    std::string sub = "Ctrl+click adds or removes one, Shift+click selects a range.";
    if (out_of_view > 0 && state.settings.accounts_view == app::AccountsViewMode::List) {
        sub = (out_of_view == 1 ? std::string("1 of them is") : kit::format_count(out_of_view) + " of them are") +
              " outside the visible part of the list. " + sub;
    }
    facet::StyledText(sub.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    gap(facet::T().spacing.xl);

    const float width = std::min(420.0F, facet::AvailableWidthUnits());
    char label[64];
    std::snprintf(label, sizeof(label), "Export %zu accounts", n);
    if (kit::action_card(label, {.icon = facet::Icon("upload"),
                                 .description = "Save them to a password-protected bundle file.",
                                 .width = width})) {
        request_export({state.selected_account_ids.begin(), state.selected_account_ids.end()});
    }
    gap(facet::T().spacing.sm);
    if (visible.size() > n && kit::action_card("Select all shown accounts",
                                               {.icon = facet::Icon("check"),
                                                .description = "Adds every account the search and filters show.",
                                                .width = width})) {
        std::vector<std::string> ids;
        for (std::size_t idx : visible) ids.push_back(state.vault.accounts[idx].id);
        store_selection(state, ids);
    }
    if (visible.size() > n) gap(facet::T().spacing.sm);
    if (kit::action_card("Clear selection", {.icon = facet::Icon("close"),
                                             .description = "Back to one account and its details.",
                                             .width = width})) {
        store_selection(state, {});
    }
    gap(facet::T().spacing.xl);
    std::snprintf(label, sizeof(label), "Remove %zu accounts...", n);
    if (kit::quiet_destructive_button(label)) s.bulk_remove_open = true;
}

}  // namespace

// Why a child window: it is the one stock way to inset content on the trailing side too, so
// fields filling the width and the right-aligned header actions stop short of the pane edge.
void begin_padded(const char* id) {
    const facet::Theme& t = facet::T();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, facet::Px(ImVec2(t.spacing.xl, t.spacing.lg)));
    ImGui::BeginChild(id, ImVec2(0.0F, 0.0F),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
}

void end_padded() {
    ImGui::EndChild();
}

// Why a summary instead of the first account while several are selected: the pane's actions act
// on one account, and showing one of many would invite acting on the wrong one.
void draw_detail(app::AppState& state, const std::vector<std::size_t>& visible) {
    if (multi_selection(state)) {
        draw_selection_actions(state, visible);
        return;
    }
    core::Account* a = state.selected_account_id.empty() ? nullptr : state.find_account(state.selected_account_id);
    if (a != nullptr) {
        const bool shown = std::any_of(visible.begin(), visible.end(), [&](std::size_t idx) {
            return state.vault.accounts[idx].id == a->id;
        });
        if (!shown) a = nullptr;
    }
    if (a == nullptr) {
        facet::EmptyState("No account selected",
                          {.message = "Pick an account in the list to see its standing, CS2 ranks and Steam details "
                                      "and to sign in.",
                           .icon = facet::Icon("user")});
        return;
    }
    draw_account(state, *a);
}

}  // namespace sam::ui::screens::accounts
