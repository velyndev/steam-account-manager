#include "ui/screens/settings_screen.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/icons.h>
#include <facet/core/input.h>
#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/core/text.h>
#include <facet/widgets/composites.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "platform/paths.hpp"
#include "ui/icons.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/settings/capture_hooks.hpp"
#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens {

namespace {

struct CategoryDef {
    SettingsCategory category;
    void (*draw)(settings::Page& p);
    facet::Icon icon;
    const char* description;  // one line under the category title
};

// In SettingsCategory order, so a category value indexes it.
const CategoryDef kDefs[] = {
    {SettingsCategory::General, &settings::draw_general, facet::Icon("settings"),
     "Windows logon, administrator rights, the clipboard and auto-lock."},
    {SettingsCategory::Appearance, &settings::draw_appearance, facet::Icon("palette"),
     "Theme, accent and size, and what the account views show."},
    {SettingsCategory::AccountsRefresh, &settings::draw_accounts_refresh, facet::Icon("refresh"),
     "When account data is fetched and where it comes from."},
    {SettingsCategory::SteamGuard, &settings::draw_steam_guard, icons::authenticator,
     "Codes, the copy shortcut, confirmations and auto-approve."},
    {SettingsCategory::SignInLaunch, &settings::draw_sign_in_launch, facet::Icon("play"),
     "How accounts sign in and what each launch changes on them."},
    {SettingsCategory::Cs2, &settings::draw_cs2, icons::crosshair, "The Game Coordinator connection and the CS2 screen."},
    {SettingsCategory::Trading, &settings::draw_trading, icons::trade, "Sent offers, rate limits and saved trade links."},
    {SettingsCategory::Notifications, &settings::draw_notifications, facet::Icon("bell"),
     "Ban and cooldown notices, where they appear and how long they stay."},
    {SettingsCategory::Network, &settings::draw_network, facet::Icon("link"), "Proxies for the app's own web traffic."},
    {SettingsCategory::PrivacySecurity, &settings::draw_privacy_security, facet::Icon("eye-off"),
     "What is hidden on screen, unlocking without the password, and safe mode."},
    {SettingsCategory::VaultsStorage, &settings::draw_vaults_storage, icons::vault,
     "Your vaults and where the app keeps its data."},
    {SettingsCategory::About, &settings::draw_about, facet::Icon("info"), "Version, updates and license."},
    {SettingsCategory::Cleaner, &settings::draw_cleaner, facet::Icon("trash"), "Deletes what Steam leaves on this PC."},
    {SettingsCategory::HwidSpoofer, &settings::draw_hwid_spoofer, facet::Icon("monitor"),
     "Replaces hardware identifiers when an account launches."},
    {SettingsCategory::Loaders, &settings::draw_loaders, facet::Icon("file"),
     "Third-party programs run when CS2 starts."},
};
static_assert(std::size(kDefs) == static_cast<std::size_t>(kSettingsCategoryCount));

struct ScreenState {
    int active = 0;
    std::string query;
    std::string last_query;  // the query of the last frame, to notice a change
    bool detail_open = false;
    int last_hits[kSettingsCategoryCount] = {};  // per category, from the last frame's search
};

ScreenState& screen_state() {
    static ScreenState s;
    return s;
}

facet::ShortcutId save_shortcut() {
    static const facet::ShortcutId id =
        facet::RegisterShortcut("settings.save", ImGuiMod_Ctrl | ImGuiKey_S, {.label = "Save settings", .group = "Settings"});
    return id;
}

bool searching(const ScreenState& st) {
    return st.query.find_first_not_of(' ') != std::string::npos;
}

void reset_all() {
    settings::reset_general_transient();
    settings::reset_network_transient();
    settings::reset_privacy_transient();
    settings::reset_vaults_transient();
    settings::reset_cleaner_transient();
}

// Closes every dialog, menu and popover of the screen (brief rule 1 of section 1c). Runs when the
// category or the search changes, when the user leaves Settings, and through the kit registry on
// lock, vault change and before each capture scene.
void reset_transient(app::AppState&) {
    reset_all();
}

void select(ScreenState& st, int category) {
    if (st.active != category) reset_all();
    st.active = category;
    st.query.clear();
    st.detail_open = true;
}

// One run of category rows as a Facet list without a surface: rows straight on the page with
// the selection pill, so the list reads as secondary to the app sidebar. `ids` are category
// values. Returns the category picked by click, Enter or double click, or -1.
int category_rows(const char* id, const std::vector<int>& ids, ScreenState& st, bool search) {
    constexpr float kRowHeight = 32.0F;
    static ImGuiSelectionBasicStorage selection;
    selection.Clear();
    int active_row = -1;
    for (std::size_t r = 0; r < ids.size(); ++r) {
        if (!search && ids[r] == st.active) active_row = static_cast<int>(r);
    }
    if (active_row >= 0) selection.SetItemSelected(selection.AdapterIndexToStorageId(&selection, active_row), true);
    int picked = -1;
    // The list is as tall as all its rows, so it never scrolls itself: the list pane around it is
    // the one container that scrolls. Should the list still find content past its height (an
    // inset, rounding at a display scale), the overflow it measured is added the next frame.
    static float overflow[2] = {0.0F, 0.0F};
    float& extra = overflow[std::strcmp(id, "##categories") == 0 ? 0 : 1];
    if (facet::BeginVirtualList(id, static_cast<int>(ids.size()), &selection,
                                {.rowHeight = kRowHeight,
                                 .height = kRowHeight * static_cast<float>(ids.size()) + 2.0F + extra})) {
        if (ImGui::GetScrollMaxY() > 0.0F) extra += ImGui::GetScrollMaxY() / facet::Px(1.0F) + 1.0F;        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int r = range.first; r < range.last; ++r) {
                const int index = ids[static_cast<std::size_t>(r)];
                if (facet::VirtualListRow(r)) picked = index;
                facet::IconGlyph(kDefs[index].icon, 0.0F, facet::ToU32(facet::T().colors.labelSecondary));
                ImGui::SameLine();
                facet::Text(facet::TextStyle::Body, "%s", kit::settings_category_title(kDefs[index].category));
                if (search && st.last_hits[index] > 0) {
                    ImGui::SameLine();
                    settings::align_trailing(28.0F);
                    facet::CountBadge(st.last_hits[index], {.tone = facet::Tone::Neutral});
                }
            }
        }
        facet::EndVirtualList();
    }
    // A click writes the storage: the row it selected, if it is not the active one, was picked.
    for (std::size_t r = 0; r < ids.size() && picked < 0; ++r) {
        const ImGuiID key = selection.AdapterIndexToStorageId(&selection, static_cast<int>(r));
        if (selection.Contains(key) && static_cast<int>(r) != active_row) picked = ids[r];
    }
    return picked;
}

// Why the search sits above a plain list: the search reaches across every category and its hit
// counts show on the rows; the list has no surface of its own, so the app sidebar stays the
// one navigation column with a card behind it.
void draw_navigation(app::AppState& state, ScreenState& st) {
    if (facet::SearchField("##settings-search", &st.query, {.placeholder = "Search settings"}) && searching(st)) {
        st.detail_open = true;
    }
    ImGui::Spacing();
    const bool search = searching(st);
    std::vector<int> normal;
    std::vector<int> risky;
    for (const kit::SettingsCategoryInfo& info : kit::settings_categories()) {
        if (kit::settings_category_hidden(state, info.category)) continue;
        (info.unsafe ? risky : normal).push_back(static_cast<int>(info.category));
    }
    int picked = category_rows("##categories", normal, st, search);
    if (!risky.empty()) {
        ImGui::Spacing();
        facet::StyledText("Ban risk", {.style = facet::TextStyle::Footnote, .secondary = true});
        const int risky_pick = category_rows("##risky-categories", risky, st, search);
        if (risky_pick >= 0) picked = risky_pick;
    }
    if (picked >= 0) select(st, picked);
}

// The narrow form of the navigation (brief rule 7): a dropdown of the categories and the search
// on one line above the content, which then keeps the whole width.
void draw_navigation_bar(app::AppState& state, ScreenState& st) {
    std::vector<int> ids;
    std::vector<facet::ChoiceItem> items;
    for (const kit::SettingsCategoryInfo& info : kit::settings_categories()) {
        if (kit::settings_category_hidden(state, info.category)) continue;
        const int index = static_cast<int>(info.category);
        ids.push_back(index);
        items.push_back({.label = info.title, .icon = kDefs[index].icon, .group = info.unsafe ? "Ban risk" : nullptr});
    }
    int current = -1;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == st.active) current = static_cast<int>(i);
    }
    facet::BeginStack("##settings-nav-bar", {.orientation = facet::Orientation::Horizontal});
    facet::Dropdown("##category", &current, items, {.maxVisibleRows = 16, .width = 240.0F});
    facet::SearchField("##settings-search", &st.query, {.placeholder = "Search settings", .width = 240.0F});
    facet::EndStack();
    if (current >= 0 && ids[static_cast<std::size_t>(current)] != st.active) {
        select(st, ids[static_cast<std::size_t>(current)]);
    }
    ImGui::Spacing();
}

void draw_unsaved_bar(app::AppState& state) {
    switch (facet::UnsavedChangesBar("##settings-unsaved", settings::pending_count(state),
                                     {.applyLabel = "Save", .revertLabel = "Discard", .applyShortcut = save_shortcut()})) {
        case facet::UnsavedChangesAction::Apply:
            settings::save_pending(state);
            kit::show_toast(state, "Settings saved", {.tone = widgets::ToastTone::Success, .id = "settings-saved"});
            break;
        case facet::UnsavedChangesAction::Revert: settings::discard_pending(state); break;
        case facet::UnsavedChangesAction::None: break;
    }
}

// Why one draw path for both modes: the rows are the same code in a category and in a search,
// so a search finds every option exactly as it looks in its category.
void draw_content(app::AppState& state, ScreenState& st) {
    static const app::Settings kDefaults{};
    settings::Page p{.state = state, .s = state.settings, .d = kDefaults};
    p.searching = searching(st);
    // A gutter on the right, so the scroll bar never sits on the cards' edge.
    facet::BeginStack("##settings-content", {.width = std::max(200.0F, facet::AvailableWidthUnits() - 14.0F)});
    p.label_width = std::clamp(facet::AvailableWidthUnits() * 0.40F, 170.0F, 320.0F);

    static const facet::FilterSettings kWords{};
    facet::BeginSettingsSearch(settings::kSearchName,
                               {.filter = p.searching ? st.query.c_str() : nullptr, .filterSettings = &kWords});
    if (p.searching) {
        for (const CategoryDef& def : kDefs) {
            if (kit::settings_category_hidden(state, def.category)) continue;
            settings::begin_category(p, def.category);
            def.draw(p);
            settings::end_category(p);
        }
        int total = 0;
        for (const int h : p.hits) total += h;
        if (total == 0) {
            const std::string title = "No settings match \"" + st.query + "\"";
            if (facet::EmptyState(title.c_str(), {.message = "Try another word, for example proxy, cache, theme or hotkey.",
                                                  .icon = facet::Icon("search"),
                                                  .actionLabel = "Clear the search"})) {
                st.query.clear();
            }
        }
    } else {
        // The category name is on the selected list row (or in the dropdown), so the pane starts
        // with its one-line summary: page title, section headings and this read as three levels.
        const CategoryDef& def = kDefs[st.active];
        facet::StyledText(def.description, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)));
        settings::begin_category(p, def.category);
        def.draw(p);
        settings::end_category(p);
    }
    facet::EndSettingsSearch();
    facet::EndStack();

#ifdef SAM_UI_CAPTURE
    if (settings::capture::scrolling_to_end()) ImGui::SetScrollY(ImGui::GetScrollMaxY());
#endif
    std::copy(std::begin(p.hits), std::end(p.hits), std::begin(st.last_hits));
    if (p.open_category >= 0) select(st, p.open_category);
}

std::string page_context(const app::AppState& state) {
    const app::VaultInfo* v = state.vault_registry.find(platform::active_vault_id());
    if (v == nullptr || v->name.empty()) return "Most options belong to the open vault.";
    return "Most options belong to the vault " + v->name + ".";
}

}  // namespace

SAM_KIT_TRANSIENT_RESET(settings_screen, &reset_transient)
SAM_KIT_SCREEN_LEAVE(settings_screen, app::Screen::Settings, &reset_transient)

#ifdef SAM_UI_CAPTURE
namespace settings::capture {
namespace {
void (*g_after_baseline)(app::AppState&) = nullptr;
bool g_scroll_to_end = false;
}
void scroll_to_end() {
    g_scroll_to_end = true;
}
bool scrolling_to_end() {
    return g_scroll_to_end;
}
void set_query(const char* query) {
    screen_state().query = query;
    screen_state().detail_open = true;
}
void after_baseline(void (*fn)(app::AppState& state)) {
    g_after_baseline = fn;
}
void run_after_baseline(app::AppState& state) {
    if (g_after_baseline == nullptr) return;
    auto* fn = g_after_baseline;
    g_after_baseline = nullptr;
    fn(state);
}
}  // namespace settings::capture
#endif

// Why list and detail when wide: the category list stays in view while the rows change; when
// narrow the list becomes a dropdown so the rows keep the width.
void draw_settings(app::AppState& state) {
    ScreenState& st = screen_state();
    settings::track_saved_state(state);

    if (state.pending_settings_category >= 0 && state.pending_settings_category < kSettingsCategoryCount &&
        !kit::settings_category_hidden(state, static_cast<SettingsCategory>(state.pending_settings_category))) {
        select(st, state.pending_settings_category);
    }
    state.pending_settings_category = -1;
    if (kit::settings_category_hidden(state, static_cast<SettingsCategory>(st.active))) st.active = 0;
#ifdef SAM_UI_CAPTURE
    // After the scene's category was opened (which closes dialogs), so a scene can open one.
    settings::capture::run_after_baseline(state);
    st.last_query = st.query;
#endif

    kit::PageScope page("settings", {.scroll = false});
    {
        const std::string context = page_context(state);
        kit::PageHeaderScope header("Settings", {.context = context.c_str()});
    }

    if (st.query != st.last_query) {
        reset_all();
        st.last_query = st.query;
    }

    // Narrow (below 900 units: 900x600 and scale 1.5): the categories become a dropdown above the
    // content; wide: the plain category list beside it.
    if (facet::AvailableWidthUnits() < 900.0F) {
        draw_navigation_bar(state, st);
        if (facet::BeginScrollArea("##settings-narrow")) {
            draw_content(state, st);
            draw_unsaved_bar(state);
            facet::EndScrollArea();
        }
    } else {
        facet::BeginListDetail("##settings-layout", &st.detail_open, {.listWidth = 236.0F});
        if (facet::BeginListPane()) {
            draw_navigation(state, st);
            facet::EndListPane();
        }
        if (facet::BeginDetailPane()) {
            draw_content(state, st);
            draw_unsaved_bar(state);
            facet::EndDetailPane();
        }
        facet::EndListDetail();
    }

    settings::end_frame_tracking(state);
}

}  // namespace sam::ui::screens
