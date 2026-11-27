// Settings > Appearance: theme, accent and scale (a live preview until Save), then what the
// account views show.

#include <array>
#include <span>

#include <imgui.h>

#include <facet/core/theme.h>
#include <facet/widgets/composites.h>
#include <facet/widgets/selection.h>

#include "ui/screens/settings/categories.hpp"
#include "ui/theme.hpp"

namespace sam::ui::screens::settings {

namespace {

facet::ThemeMode to_facet(app::ThemeMode mode) {
    switch (mode) {
        case app::ThemeMode::Light: return facet::ThemeMode::Light;
        case app::ThemeMode::Dark: return facet::ThemeMode::Dark;
        case app::ThemeMode::System: return facet::ThemeMode::System;
    }
    return facet::ThemeMode::Dark;
}

app::ThemeMode from_facet(facet::ThemeMode mode) {
    switch (mode) {
        case facet::ThemeMode::Light: return app::ThemeMode::Light;
        case facet::ThemeMode::Dark: return app::ThemeMode::Dark;
        case facet::ThemeMode::System: return app::ThemeMode::System;
    }
    return app::ThemeMode::Dark;
}

// The theme controls apply at once (Facet blends to the new look); the values stay pending
// until Save, and Discard re-applies the saved look.
void draw_theme(Page& p) {
    section(p, "Look", "Applies to every vault.");

    if (begin_row(p, "Theme",
                  {.description = "Light, dark, or follow the Windows app mode while the app runs.",
                   .keywords = "dark light mode system colors night",
                   .modified = p.s.theme_mode != p.d.theme_mode,
                   .reset = true})) {
        facet::ThemeMode mode = to_facet(p.s.theme_mode);
        if (facet::ThemeModeControl("##value", &mode)) p.s.theme_mode = from_facet(mode);
        if (end_row()) {
            p.s.theme_mode = p.d.theme_mode;
            theme::apply_appearance(p.s, true);
        }
    }

    if (begin_row(p, "Accent color",
                  {.description = "Colors the selection, the main button of each view and the keyboard focus.",
                   .keywords = "accent color colour highlight tint",
                   .modified = p.s.ui_accent != p.d.ui_accent,
                   .reset = true})) {
        bool theme_default = p.s.ui_accent.empty();
        ImVec4 accent = theme::parse_accent(p.s.ui_accent).value_or(facet::T().colors.accent);
        if (facet::AccentPicker("##value", &theme_default, &accent)) {
            p.s.ui_accent = theme_default ? std::string{} : theme::format_accent(accent);
        }
        if (end_row()) {
            p.s.ui_accent = p.d.ui_accent;
            theme::apply_appearance(p.s, true);
        }
    }

    if (begin_row(p, "Interface size",
                  {.description = "Scales text and controls on top of the display scaling of Windows.",
                   .keywords = "scale zoom size dpi bigger smaller text font",
                   .modified = p.s.ui_scale != p.d.ui_scale,
                   .reset = true})) {
        facet::UiScaleControl("##value", &p.s.ui_scale, app::kUiScaleMin, app::kUiScaleMax, {.width = cap(320.0F)});
        if (end_row()) {
            p.s.ui_scale = p.d.ui_scale;
            theme::apply_appearance(p.s, true);
        }
    }
}

// Checkbox groups for the account facts: three rows instead of fourteen, grouped by where the
// data comes from, because that is also what each group needs to work.
template <std::size_t N>
void info_group(Page& p, const char* label, const char* description, const char* keywords,
                const std::array<bool*, N>& values, const std::array<bool, N>& defaults,
                const std::array<facet::ChoiceItem, N>& items) {
    bool modified = false;
    for (std::size_t i = 0; i < N; ++i) modified = modified || *values[i] != defaults[i];
    if (!begin_row(p, label, {.description = description, .keywords = keywords, .modified = modified, .reset = true})) {
        return;
    }
    std::array<bool, N> checked{};
    for (std::size_t i = 0; i < N; ++i) checked[i] = *values[i];
    if (facet::CheckboxGroup("##value", checked, items)) {
        for (std::size_t i = 0; i < N; ++i) *values[i] = checked[i];
    }
    if (end_row()) {
        for (std::size_t i = 0; i < N; ++i) *values[i] = defaults[i];
    }
}

void draw_account_views(Page& p) {
    section(p, "Account views");

    if (begin_row(p, "Accounts layout",
                  {.description = "List shows groups and rows beside the selected account. Table shows every "
                                  "account in sortable columns.",
                   .keywords = "list table columns rows view layout grid",
                   .modified = p.s.accounts_view != p.d.accounts_view,
                   .immediate = true})) {
        // Index 1 is the table layout (AccountsViewMode::Table).
        static const char* const kViews[] = {"List", "Table"};
        int index = p.s.accounts_view == app::AccountsViewMode::List ? 0 : 1;
        if (facet::SegmentedControl("##value", &index, kViews, {.width = cap(240.0F)})) {
            p.s.accounts_view = index == 0 ? app::AccountsViewMode::List : app::AccountsViewMode::Table;
            commit_now(p.state);
        }
        end_row();
    }
    toggle_row(p, "Show avatars", &p.s.show_avatars, p.d.show_avatars,
               "Shows each account's Steam avatar. Off shows initials and downloads no pictures.",
               "avatar picture image profile photo initials");
    toggle_row(p, "Hide account notes", &p.s.hide_notes, p.d.hide_notes,
               "Hides notes everywhere they are shown. They stay editable when you edit the account.",
               "notes hide private");

    const auto& d = p.d.info;
    auto& i = p.s.info;
    info_group<5>(p, "Ban status", "Needs a Steam Web API key (Accounts and refresh). VAC live comes from the CS2 Game Coordinator.",
                  "vac game community trade ban live",
                  {&i.show_vac, &i.show_game_ban, &i.show_community_ban, &i.show_trade_ban, &i.show_vac_live},
                  {d.show_vac, d.show_game_ban, d.show_community_ban, d.show_trade_ban, d.show_vac_live},
                  {facet::ChoiceItem{"VAC ban"}, facet::ChoiceItem{"Game ban"}, facet::ChoiceItem{"Community ban"},
                   facet::ChoiceItem{"Trade ban"}, facet::ChoiceItem{"VAC live"}});
    info_group<4>(p, "Steam profile",
                  "Level and games need the Web API key. External funds come from Refresh spent, which signs in "
                  "to each account.",
                  "steam level owned games id steamid funds spent wallet",
                  {&i.show_steam_level, &i.show_owned_games, &i.show_steam_id, &i.show_external_funds},
                  {d.show_steam_level, d.show_owned_games, d.show_steam_id, d.show_external_funds},
                  {facet::ChoiceItem{"Steam level"}, facet::ChoiceItem{"Owned games"}, facet::ChoiceItem{"Steam ID"},
                   facet::ChoiceItem{"External funds"}});
    info_group<5>(p, "CS2 standing",
                  "Ranks need the GCPD scraper or the Game Coordinator; cooldown and the weekly drop need the "
                  "Game Coordinator. Prime is exact once an account has connected to the Game Coordinator "
                  "itself, and estimated from level and XP until then.",
                  "premier rating wingman rank prime cooldown weekly drop xp cs2",
                  {&i.show_premier, &i.show_wingman, &i.show_prime, &i.show_cooldown, &i.show_weekly_drop},
                  {d.show_premier, d.show_wingman, d.show_prime, d.show_cooldown, d.show_weekly_drop},
                  {facet::ChoiceItem{"Premier rating"}, facet::ChoiceItem{"Wingman rank"}, facet::ChoiceItem{"Prime"},
                   facet::ChoiceItem{"Cooldown"}, facet::ChoiceItem{"Weekly drop"}});
}

void draw_list_rows(Page& p) {
    section(p, "List rows", "Markers on the rows of the list layout.");
    auto& l = p.s.list_view;
    const auto& d = p.d.list_view;
    toggle_row(p, "Cooldown marker", &l.show_cooldown_marker, d.show_cooldown_marker,
               "Marks rows whose account is on a CS2 cooldown.", "cooldown marker row list");
    toggle_row(p, "Unread events marker", &l.show_unread_badge, d.show_unread_badge,
               "Marks rows with unread ban or cooldown events.", "unread badge events notification row list");
    toggle_row(p, "Weekly drop marker", &l.show_weekly_drop_marker, d.show_weekly_drop_marker,
               "Marks rows whose weekly XP drop is claimed.", "weekly drop claimed marker row list");
    toggle_row(p, "Hide login names in rows", &l.hide_account_name, d.hide_account_name,
               "Leaves the login out of list rows. The Steam name still shows.", "hide login account name row list");
}

}  // namespace

// Why this order: the look of the whole app first (it previews at once), then what the account
// views show, from the layout down to single markers.
void draw_appearance(Page& p) {
    draw_theme(p);
    draw_account_views(p);
    draw_list_rows(p);
}

}  // namespace sam::ui::screens::settings
