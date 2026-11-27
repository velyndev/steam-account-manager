#include "ui/kit/standing.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/icons.h>
#include <facet/core/image.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/display.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/selection.h>

#include "core/account_store/store.hpp"
#include "ui/fonts.hpp"
#include "ui/icons.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/row_line.hpp"
#include "ui/util.hpp"
#include "ui/widgets/rank_image.hpp"

namespace sam::ui::kit {

namespace {

std::string last_ban_line(const core::BanStatus& b) {
    if (b.days_since_last_ban <= 0) return {};
    const std::int64_t base = b.last_refreshed_unix > 0 ? b.last_refreshed_unix : now_seconds();
    const std::int64_t when = base - static_cast<std::int64_t>(b.days_since_last_ban) * 86400;
    return "\nMost recent ban (VAC or game): " + std::to_string(b.days_since_last_ban) + " days ago, around " +
           format_date(when) + ".";
}

std::string checked_line(const core::BanStatus& b) {
    if (b.last_refreshed_unix <= 0) return "\nNo ban data yet. Refresh this account to check.";
    return "\nChecked " + format_relative(b.last_refreshed_unix) + ".";
}

bool trade_banned(const core::BanStatus& b) {
    return b.economy_ban == "banned";
}

bool trade_probation(const core::BanStatus& b) {
    return b.economy_ban == "probation";
}

void pill(bool& any, const char* label, facet::Tone tone, const std::string& tip) {
    if (any) ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.xs));
    facet::StatusPill(label, {.tone = tone, .icon = icons::ban, .size = facet::ControlSize::Small});
    facet::ItemTooltip("%s", tip.c_str());
    any = true;
}

std::string duration_text(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const std::int64_t days = seconds / 86400;
    const std::int64_t hours = (seconds % 86400) / 3600;
    const std::int64_t minutes = (seconds % 3600) / 60;
    char buf[48];
    if (days > 0) {
        std::snprintf(buf, sizeof(buf), "%lldd %lldh", static_cast<long long>(days), static_cast<long long>(hours));
    } else if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%lldh %lldm", static_cast<long long>(hours),
                      static_cast<long long>(minutes));
    } else {
        std::snprintf(buf, sizeof(buf), "%lldm", static_cast<long long>(minutes > 0 ? minutes : 1));
    }
    return buf;
}

// A status glyph as an item of its exact size (the icon size of Body text), so a row stack
// centers it like any other item; IconGlyph would center itself on the text line instead.
void mark_glyph(const char* id, facet::Icon icon, const ImVec4& color) {
    const float size = std::round(facet::MeasureText(facet::TextStyle::Body, "M").y);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    (void)id;
    facet::DrawIcon(ImGui::GetWindowDrawList(), icon, pos, size, facet::ToU32(color));
}

bool rank_image_item(const widgets::rank_image::TexEntry* e, float height_units, ImVec2* min, ImVec2* max) {
    if (e == nullptr || e->srv == nullptr || e->h <= 0) return false;
    const float h = facet::Px(height_units);
    const float w = h * static_cast<float>(e->w) / static_cast<float>(e->h);
    facet::Image(e->srv, ImVec2(w, h), {.fit = facet::ImageFit::Contain, .showStates = false});
    *min = ImGui::GetItemRectMin();
    *max = ImGui::GetItemRectMax();
    return true;
}

}  // namespace

int ban_pills(const app::AppState& state, const core::Account& account, const BanPillsOptions& options) {
    const auto& info = state.settings.info;
    const core::BanStatus& b = account.bans;
    bool any = false;
    int drawn = 0;
    ImGui::PushID(account.id.c_str());
    if (info.show_vac && b.vac_banned) {
        std::string tip = "VAC banned.";
        if (b.vac_ban_count > 0) tip += "\nVAC bans: " + std::to_string(b.vac_ban_count);
        if (info.show_vac_live && account.cs2.vac_live) tip += "\nThe CS2 Game Coordinator reports it too.";
        pill(any, b.vac_ban_count > 1 ? (std::to_string(b.vac_ban_count) + " VAC bans###vac").c_str() : "VAC ban###vac",
             facet::Tone::Destructive, tip + last_ban_line(b) + checked_line(b));
        ++drawn;
    }
    if (info.show_game_ban && b.game_ban_count > 0) {
        const std::string tip = "Game bans: " + std::to_string(b.game_ban_count) +
                                " (issued by a game's developer, not by VAC).";
        pill(any,
             b.game_ban_count > 1 ? (std::to_string(b.game_ban_count) + " game bans###game").c_str()
                                  : "Game ban###game",
             facet::Tone::Destructive, tip + last_ban_line(b) + checked_line(b));
        ++drawn;
    }
    if (info.show_trade_ban && (trade_banned(b) || trade_probation(b))) {
        const bool banned = trade_banned(b);
        pill(any, banned ? "Trade ban###trade" : "Trade probation###trade",
             banned ? facet::Tone::Destructive : facet::Tone::Warning,
             std::string(banned ? "Trade banned: this account can't trade or use the Community Market."
                                : "Trade probation: trading is restricted.") +
                 checked_line(b));
        ++drawn;
    }
    if (info.show_community_ban && b.community_banned) {
        pill(any, "Community ban###community", facet::Tone::Warning,
             "Community banned: no forums, Workshop, groups or reviews." + checked_line(b));
        ++drawn;
    }
    // One ban, one pill: the Game Coordinator's report gets a pill of its own only while the Web
    // API does not show the ban yet (or the user hides the Web API's VAC pill).
    if (info.show_vac_live && account.cs2.vac_live && !(info.show_vac && b.vac_banned)) {
        pill(any, "VAC ban###vaclive", facet::Tone::Destructive,
             "VAC ban reported by the CS2 Game Coordinator. The Steam Web API does not show it yet; it "
             "usually follows.");
        ++drawn;
    }
    if (drawn == 0 && options.show_clean) {
        facet::StatusPill("No bans###clean", {.tone = facet::Tone::Neutral, .size = facet::ControlSize::Small});
        facet::ItemTooltip("%s", ("No active ban among the kinds shown." + checked_line(b)).c_str());
    }
    ImGui::PopID();
    return drawn;
}

std::string ban_summary(const app::AppState& state, const core::Account& account) {
    const auto& info = state.settings.info;
    const core::BanStatus& b = account.bans;
    std::string out;
    auto add = [&](const std::string& part) {
        if (!out.empty()) out += ", ";
        out += part;
    };
    if (info.show_vac && b.vac_banned) add(b.vac_ban_count > 1 ? std::to_string(b.vac_ban_count) + " VAC bans" : "VAC ban");
    else if (info.show_vac_live && account.cs2.vac_live) add("VAC ban");
    if (info.show_game_ban && b.game_ban_count > 0) {
        add(b.game_ban_count > 1 ? std::to_string(b.game_ban_count) + " game bans" : "game ban");
    }
    if (info.show_trade_ban && trade_banned(b)) add("trade ban");
    if (info.show_trade_ban && trade_probation(b)) add("trade probation");
    if (info.show_community_ban && b.community_banned) add("community ban");
    if (out.empty()) return "No bans";
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

bool any_ban(const app::AppState& state, const core::Account& account) {
    return ban_tone(state, account) != facet::Tone::Neutral;
}

facet::Tone ban_tone(const app::AppState& state, const core::Account& account) {
    const auto& info = state.settings.info;
    const core::BanStatus& b = account.bans;
    if ((info.show_vac && b.vac_banned) || (info.show_game_ban && b.game_ban_count > 0) ||
        (info.show_trade_ban && trade_banned(b)) || (info.show_vac_live && account.cs2.vac_live)) {
        return facet::Tone::Destructive;
    }
    if ((info.show_trade_ban && trade_probation(b)) || (info.show_community_ban && b.community_banned)) {
        return facet::Tone::Warning;
    }
    return facet::Tone::Neutral;
}

bool cooldown_active(const core::Account& account, std::int64_t now) {
    return account.cs2.cooldown_expires_unix == core::kCooldownNever || account.cs2.cooldown_expires_unix > now;
}

std::string cooldown_text(const core::Account& account, std::int64_t now) {
    if (account.cs2.cooldown_expires_unix == core::kCooldownNever) return "Permanent cooldown";
    if (account.cs2.cooldown_expires_unix <= now) return {};
    return "Cooldown " + duration_text(account.cs2.cooldown_expires_unix - now);
}

bool cooldown_pill(const core::Account& account, std::int64_t now) {
    if (!cooldown_active(account, now)) return false;
    const std::string text = cooldown_text(account, now) + "###cooldown";
    ImGui::PushID(account.id.c_str());
    facet::StatusPill(text.c_str(),
                      {.tone = facet::Tone::Warning, .icon = facet::Icon("clock"), .size = facet::ControlSize::Small});
    std::string tip = "CS2 matchmaking cooldown.";
    if (!account.cs2.cooldown_reason.empty()) tip += "\nReason: " + account.cs2.cooldown_reason;
    if (account.cs2.cooldown_expires_unix != core::kCooldownNever) {
        tip += "\nEnds " + format_date(account.cs2.cooldown_expires_unix) + " (UTC).";
    }
    facet::ItemTooltip("%s", tip.c_str());
    ImGui::PopID();
    return true;
}

facet::Tone trust_tone(core::TrustLabel trust) {
    switch (trust) {
        case core::TrustLabel::Green: return facet::Tone::Success;
        case core::TrustLabel::Yellow: return facet::Tone::Warning;
        case core::TrustLabel::Red: return facet::Tone::Destructive;
        case core::TrustLabel::Unset: break;
    }
    return facet::Tone::Neutral;
}

const char* trust_title(core::TrustLabel trust) {
    switch (trust) {
        case core::TrustLabel::Green: return "Green trust label";
        case core::TrustLabel::Yellow: return "Yellow trust label";
        case core::TrustLabel::Red: return "Red trust label";
        case core::TrustLabel::Unset: break;
    }
    return "No trust label";
}

void trust_dot(core::TrustLabel trust) {
    const std::string label = std::string("##") + trust_title(trust);
    facet::StatusDot(label.c_str(), {.tone = trust_tone(trust)});
}

void trust_dot(core::TrustLabel trust, float row_height) {
    detail::RowLine line("##trust-dot", facet::Px(facet::T().metrics.statusDotSize), row_height);
    trust_dot(trust);
}

bool trust_dropdown(const char* label, core::TrustLabel* trust) {
    if (trust == nullptr) return false;
    static const facet::ChoiceItem kItems[] = {
        {.label = "No trust label"},
        {.label = "Green"},
        {.label = "Yellow"},
        {.label = "Red"},
    };
    int index = static_cast<int>(*trust);
    if (!facet::Dropdown(label, &index, kItems)) return false;
    *trust = static_cast<core::TrustLabel>(index);
    return true;
}

bool account_kind_badge(const core::Account& account) {
    const bool revoked = account.is_nfa && account.nfa_status == core::NfaTokenStatus::Revoked;
    if (revoked) {
        facet::Badge("Token revoked", {.tone = facet::Tone::Destructive, .icon = icons::token});
        facet::ItemTooltip("The sign-in token failed validation. Import a fresh one or sign in with the password.");
        return true;
    }
    if (core::store::is_cached_account(account)) {
        facet::Badge("Cached", {.icon = icons::token});
        facet::ItemTooltip("Signs in with the login Steam saved on this PC.");
        return true;
    }
    if (account.is_nfa) {
        facet::Badge("NFA", {.icon = icons::token});
        facet::ItemTooltip("Token only: no password and no full access.");
        return true;
    }
    return false;
}

bool prime_badge(const app::AppState& state, const core::Account& account) {
    if (!state.settings.info.show_prime || !account.cs2.prime_status) return false;
    if (account.cs2.prime_confirmed) {
        facet::Badge("Prime");
        facet::ItemTooltip("CS2 Prime status");
    } else {
        facet::Badge("Likely Prime");
        facet::ItemTooltip("%s", prime_estimate_hint(state).c_str());
    }
    return true;
}

std::string prime_estimate_hint(const app::AppState& state) {
    std::string hint = "Estimated from the CS2 level and XP: Steam tells only the account itself whether it has Prime. ";
    hint += state.settings.cs2_gc.enabled ? "Connect the account on the CS2 screen for the exact status."
                                          : "The exact status needs the CS2 connection, which is off in Settings > CS2.";
    return hint;
}

std::string prime_detail_text(const app::AppState& state, const core::Account& account) {
    const core::CS2Status& cs2 = account.cs2;
    if (cs2.prime_confirmed) return cs2.prime_status ? "Yes" : "No";
    std::string text = cs2.prime_status ? "Likely, going by level and XP. " : "Unknown. ";
    text += state.settings.cs2_gc.enabled ? "Connect on the CS2 screen to check."
                                          : "Checking needs the CS2 connection (Settings > CS2).";
    return text;
}

bool weekly_drop_claimed(const core::Account& account, std::int64_t now) {
    return account.cs2.weekly_drop_reset_unix > now;
}

bool weekly_drop_badge(const app::AppState& state, const core::Account& account, std::int64_t now) {
    if (!state.settings.info.show_weekly_drop || !weekly_drop_claimed(account, now)) return false;
    facet::Badge("Drop claimed", {.tone = facet::Tone::Success, .icon = icons::weekly_drop});
    facet::ItemTooltip("Weekly drop claimed. Resets %s (UTC).", format_date(account.cs2.weekly_drop_reset_unix).c_str());
    return true;
}

std::size_t unread_count(const app::AppState& state, const core::Account& account) {
    if (!state.settings.notifications.enabled || !state.settings.notifications.surface_in_card) return 0;
    return state.notifications.unacked_count_for(account.id);
}

bool unread_badge(const app::AppState& state, const core::Account& account) {
    const std::size_t n = unread_count(state, account);
    if (n == 0) return false;
    facet::CountBadge(static_cast<int>(n), {.tone = facet::Tone::Destructive});
    facet::ItemTooltip(n == 1 ? "1 unread ban or cooldown event" : "%zu unread ban or cooldown events", n);
    return true;
}

int row_markers(const app::AppState& state, const core::Account& account, std::int64_t now) {
    return row_markers(state, account, now, RowMarkerOptions{});
}

int row_markers(const app::AppState& state, const core::Account& account, std::int64_t now,
                const RowMarkerOptions& options) {
    const auto& lv = state.settings.list_view;
    const bool ban = options.ban && any_ban(state, account);
    const bool cooldown = lv.show_cooldown_marker && cooldown_active(account, now);
    const bool drop = lv.show_weekly_drop_marker && weekly_drop_claimed(account, now);
    const bool unread = lv.show_unread_badge && unread_count(state, account) > 0;
    if (!ban && !cooldown && !drop && !unread) return 0;
    // Each mark is a run of the row stack (next() before every mark but the first).
    int drawn = 0;
    ImGui::PushID(account.id.c_str());
    {
    detail::RowLine line("##markers", facet::ControlHeight(facet::ControlSize::Small), 0.0F, facet::Gap::Tight);
    if (ban) {
        if (drawn++ > 0) line.next();
        const facet::Tone tone = ban_tone(state, account);
        mark_glyph("##ban", icons::ban, tone == facet::Tone::Destructive ? facet::T().colors.destructive
                                                                          : facet::T().colors.warning);
        facet::ItemTooltip("%s", ban_summary(state, account).c_str());
    }
    if (cooldown) {
        if (drawn++ > 0) line.next();
        mark_glyph("##cooldown", facet::Icon("clock"), facet::T().colors.warning);
        facet::ItemTooltip("%s: no matchmaking until it ends", cooldown_text(account, now).c_str());
    }
    if (drop) {
        if (drawn++ > 0) line.next();
        mark_glyph("##drop", icons::weekly_drop, facet::T().colors.success);
        facet::ItemTooltip("Weekly drop claimed this week");
    }
    if (unread) {
        if (drawn++ > 0) line.next();
        unread_badge(state, account);
    }
    }
    ImGui::PopID();
    return drawn;
}

ImU32 data_color(std::uint32_t rgba) {
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, 0xFF);
}

bool group_chip(const core::Group& group, bool selected) {
    ImGui::PushID(group.id.c_str());
    const bool pressed = facet::Chip(group.name.c_str(), {.selected = selected, .color = data_color(group.color_rgba)});
    ImGui::PopID();
    return pressed;
}

bool tag_chip(const core::Tag& tag, bool removable) {
    ImGui::PushID(tag.id.c_str());
    const bool pressed = facet::Chip(tag.name.c_str(), {.removable = removable, .color = data_color(tag.color_rgba)});
    ImGui::PopID();
    return pressed;
}

bool premier_rank(int rating, float height) {
    const int bracket = rating > 0 ? widgets::premier_bracket_for_rating(rating) : 0;
    ImVec2 min;
    ImVec2 max;
    if (!rank_image_item(widgets::rank_image::premier(bracket), height, &min, &max)) return false;
    // The rating sits on the badge's plate a little right of center, in the label color: the
    // plate is translucent, dark over a dark card and pale over a light one, so white text would
    // fall below 4.5:1 in the light theme.
    const std::string text = rating > 0 ? format_count(rating) : std::string("---");
    ImFont* font = fonts::badge();
    const float size = (max.y - min.y) * 0.62F;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str());
    const float x = min.x + (max.x - min.x) * 0.58F - ts.x * 0.5F;
    const float y = min.y + (max.y - min.y - ts.y) * 0.5F;
    ImGui::GetWindowDrawList()->AddText(
        font, size, ImVec2(x, y),
        facet::ToU32(rating > 0 ? facet::T().colors.label : facet::T().colors.labelSecondary), text.c_str());
    if (rating > 0) {
        facet::ItemTooltip("Premier rating %s", text.c_str());
    } else {
        facet::ItemTooltip("No Premier rating yet");
    }
    return true;
}

bool wingman_rank(int rank, float height) {
    ImVec2 min;
    ImVec2 max;
    if (!rank_image_item(widgets::rank_image::wingman(rank > 0 ? rank : 0), height, &min, &max)) return false;
    if (rank > 0) {
        facet::ItemTooltip("Wingman rank %d", rank);
    } else {
        facet::ItemTooltip("No Wingman rank yet");
    }
    return true;
}

}  // namespace sam::ui::kit
