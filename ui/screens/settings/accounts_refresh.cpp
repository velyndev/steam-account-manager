// Settings > Accounts and refresh: when account data is fetched, how long it stays fresh, and
// the sources it comes from (the Web API key and the GCPD scraper).

#include <imgui.h>

#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>

#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

constexpr const char* kApiKeyUrl = "https://steamcommunity.com/dev/apikey";

void draw_refresh(Page& p) {
    section(p, "Refresh", "Batch refreshes skip accounts whose data is newer than the cache lifetime. Refreshing "
                          "one account by hand always fetches.");
    toggle_row(p, "Refresh at launch", &p.s.refresh_on_launch, p.d.refresh_on_launch,
               "Fetches Steam data for every account when the vault opens, skipping fresh ones.",
               "refresh launch startup open fetch");
    number_row(p, "Keep Steam data for", &p.s.steam_cache_hours, p.d.steam_cache_hours, 1, 24, "h",
               "Bans, summaries, level and games newer than this are not fetched again by batch refreshes.",
               "cache ttl lifetime hours stale fresh");
    toggle_row(p, "Refresh while the app is open", &p.s.auto_refresh_enabled, p.d.auto_refresh_enabled,
               "On a timer, refreshes accounts whose Steam or Game Coordinator data expired. Never runs the "
               "funds or GCPD scrapes.",
               "auto refresh puller timer background periodic");
    if (begin_dependent(p, "##auto-refresh", p.s.auto_refresh_enabled)) {
        number_row(p, "Check every", &p.s.auto_refresh_minutes, p.d.auto_refresh_minutes, 10, 720, "min",
                   "How often the timer looks for expired accounts.", "auto refresh interval minutes");
        end_dependent(p);
    }
}

// Why the key row carries its link: the one question about the key is where it comes from.
void draw_sources(Page& p) {
    section(p, "Data sources");

    if (begin_row(p, "Steam Web API key",
                  {.description = "Needed for bans, Steam level and owned games. Saved in this vault's settings.",
                   .keywords = "api key web steam token bans level games apikey",
                   .modified = !p.s.web_api_key.empty(),
                   .reset = true})) {
        facet::SecureField("##value", &p.s.web_api_key,
                           {.placeholder = "32 characters from steamcommunity.com/dev/apikey", .width = cap(360.0F)});
        facet::Link("Get a key on steamcommunity.com", {.url = kApiKeyUrl});
        if (end_row()) p.s.web_api_key.clear();
    }

    toggle_row(p, "GCPD scraper", &p.s.gcpd_enabled, p.d.gcpd_enabled,
               "Reads Premier rating, Wingman rank, CS2 level and cooldown from your account's CS2 pages on Steam. "
               "Needs a full login (Add account, Full login).",
               "gcpd scrape scraper premier wingman prime cooldown rank");
}

}  // namespace

// Why this order: when data is fetched is changed more often than where it comes from, and the
// key is entered once.
void draw_accounts_refresh(Page& p) {
    draw_refresh(p);
    draw_sources(p);
}

}  // namespace sam::ui::screens::settings
