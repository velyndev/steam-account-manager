#pragma once

// Account standing as Facet status marks: bans, cooldown, trust, kind, Prime, the weekly drop,
// unread events, groups and tags, and the CS2 rank images.
//
// One tone mapping everywhere, and a tone is never the only signal (each mark has text or an
// icon and a tooltip):
//   Destructive  VAC ban, game ban, VAC ban from the Game Coordinator, trade ban, a revoked
//                token, unread ban or cooldown events, red trust
//   Warning      trade probation, community ban, an active cooldown, yellow trust
//   Success      weekly drop claimed, green trust
//   Neutral      Prime, NFA, cached, unset trust, a clean record
// A healthy account shows almost nothing, so a banned one is what the eye finds.

#include <cstdint>
#include <string>

#include <facet/widgets/common.h>

#include "app/app_state.hpp"
#include "core/account_store/account.hpp"
#include "core/account_store/account_meta.hpp"

namespace sam::ui::kit {

// --- Bans ------------------------------------------------------------------------------------

struct BanPillsOptions {
    bool show_clean = false;  // a neutral "No bans" pill when no shown ban is active
};

// One StatusPill per active ban, in the order VAC, game, trade, community, each ban once (a VAC
// ban both the Web API and the CS2 Game Coordinator report is one pill; the Game Coordinator's
// report alone also reads "VAC ban", with the source in its tooltip), each with the sam-ban icon, a text such as "VAC ban" or "2 game bans", and a
// tooltip with the count, the date of the last ban and when the data was checked. Kinds the
// user turned off (settings.info.show_vac, show_game_ban, show_trade_ban, show_community_ban,
// show_vac_live) are skipped. The pills sit on one line, ImGui::SameLine between them; nothing
// is drawn when no pill is due. Returns how many pills it drew. Reads state.settings.
int ban_pills(const app::AppState& state, const core::Account& account, const BanPillsOptions& options = {});

// Whether any ban the user shows is active, and the strongest tone among them (Neutral when
// none). For a list row's dot or a sort key. Reads state.settings.
bool any_ban(const app::AppState& state, const core::Account& account);
facet::Tone ban_tone(const app::AppState& state, const core::Account& account);

// --- Cooldown --------------------------------------------------------------------------------

// The CS2 matchmaking cooldown: true while one runs (cs2.cooldown_expires_unix in the future or
// permanent). `now` is unix seconds.
bool cooldown_active(const core::Account& account, std::int64_t now);
// "Cooldown 3h 12m", "Cooldown 2d 4h", "Permanent cooldown", or "" when none.
std::string cooldown_text(const core::Account& account, std::int64_t now);
// A Warning StatusPill with the clock icon and cooldown_text, the reason in its tooltip. Draws
// nothing and returns false when no cooldown runs.
bool cooldown_pill(const core::Account& account, std::int64_t now);

// --- Trust, kind, Prime, weekly drop, unread -------------------------------------------------

facet::Tone trust_tone(core::TrustLabel trust);
const char* trust_title(core::TrustLabel trust);  // "Green trust label", ..., "No trust label"

// A StatusDot in the trust tone with trust_title as its tooltip and name ("##" form, so it is a
// dot alone). Not interactive.
void trust_dot(core::TrustLabel trust);
// The same dot centered on a row: on the line the caller is on (a list row or table cell after
// its row item), or in `row_height` units when that is taller.
void trust_dot(core::TrustLabel trust, float row_height);

// A Dropdown of the four trust labels, each with its dot tone in the detail text. Writes *trust
// and returns true when the user picks one; the caller marks the vault dirty and saves.
bool trust_dropdown(const char* label, core::TrustLabel* trust);

// The kind badge of an account that is not a plain password account: "NFA" (token only),
// "Cached" (signed in from this PC's saved login) or, when the token failed validation,
// "Token revoked" in Destructive. Draws nothing and returns false for a full-access account.
bool account_kind_badge(const core::Account& account);

// A neutral "Prime" badge while cs2.prime_status is set, "Likely Prime" while that value is
// only the estimate (cs2.prime_confirmed unset); nothing otherwise. Returns whether it drew.
// Honors settings.info.show_prime.
bool prime_badge(const app::AppState& state, const core::Account& account);
// Tooltip for an estimated Prime value: where the estimate comes from and how to get the exact
// status. Names the CS2 screen only while the CS2 connection is on, since it is hidden otherwise.
std::string prime_estimate_hint(const app::AppState& state);
// The Prime line of an account's details: "Yes" or "No" once the Game Coordinator has told the
// account itself, else the estimate ("Likely" or "Unknown") followed by how to check it.
std::string prime_detail_text(const app::AppState& state, const core::Account& account);

// The weekly drop: true while it is claimed for this week (cs2.weekly_drop_reset_unix in the
// future). weekly_drop_badge draws a Success badge "Drop claimed" with the sam-weekly-drop icon
// and the reset date in its tooltip; nothing while unclaimed. Honors settings.info.show_weekly_drop.
bool weekly_drop_claimed(const core::Account& account, std::int64_t now);
bool weekly_drop_badge(const app::AppState& state, const core::Account& account, std::int64_t now);

// Unacknowledged ban and cooldown events of the account (state.notifications), 0 while
// notifications or their in-card surface are turned off. unread_badge draws a Destructive
// CountBadge for a count above 0 with "N unread events" as tooltip; nothing for 0.
std::size_t unread_count(const app::AppState& state, const core::Account& account);
bool unread_badge(const app::AppState& state, const core::Account& account);

// The compact markers of a list row at the end of the row: cooldown (clock icon), weekly drop
// (gift icon) and unread events (count badge), each only when active and when its
// settings.list_view toggle is on. One item: the marks centered on the line the caller is on (a
// list row or table cell), so no SameLine or baseline patching is needed around it. Returns how
// many it drew (0: nothing was submitted). Reads state.
int row_markers(const app::AppState& state, const core::Account& account, std::int64_t now);

struct RowMarkerOptions {
    // Also a ban glyph (sam-ban in the ban tone) first, with ban_summary as its tooltip. Off by
    // default, so screens that show bans elsewhere in the row keep one mark per fact.
    bool ban = false;
};
// The same with options. Every glyph has a tooltip that says what it means and they all share
// the row's center. The trust dot is not part of the group: draw trust_dot(trust, row_height) as
// a leading mark before the avatar, so a red trust dot never sits next to a red ban glyph.
int row_markers(const app::AppState& state, const core::Account& account, std::int64_t now,
                const RowMarkerOptions& options);

// The bans the user shows, in one sentence-case line: "VAC ban, 2 game bans", or "No bans".
std::string ban_summary(const app::AppState& state, const core::Account& account);

// --- Groups and tags -------------------------------------------------------------------------

// The group or tag color (0xRRGGBBAA, user data) as a packed ImU32 for Facet data colors.
ImU32 data_color(std::uint32_t rgba);

// A Chip with the group's or tag's name and its color as the leading dot. Returns true when the
// chip body is activated (for a filter); with `removable` the remove button is read with
// facet::ChipRemovePressed() right after the call. Keyed by the item id.
bool group_chip(const core::Group& group, bool selected = false);
bool tag_chip(const core::Tag& tag, bool removable = false);

// --- CS2 ranks -------------------------------------------------------------------------------

// The Premier badge image of the rating's bracket with the rating over it ("---" while unknown),
// `height` units tall at the image's aspect ratio. The rating text is drawn in white on the
// badge, part of Valve's badge artwork. Draws nothing and returns false when the image is
// unavailable. Reads the embedded rank images.
bool premier_rank(int rating, float height);
// The Wingman rank image (rank 0 is "unranked"), `height` units tall.
bool wingman_rank(int rank, float height);

}  // namespace sam::ui::kit
