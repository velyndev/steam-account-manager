#include "ui/screens/settings/page.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <windows.h>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/composites.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/theme.hpp"

namespace sam::ui::screens::settings {

namespace {

void vertical_gap(float units) {
    const float gap = facet::Px(units) - ImGui::GetStyle().ItemSpacing.y;
    if (gap > 0.0F) ImGui::Dummy(ImVec2(0.0F, gap));
}

// Every pending field, as (live, saved) pairs. Immediate fields and the fields other screens
// edit (sort, quick filters, collapsed groups, the accounts view, trade links, the skipped
// update tag) are not listed: they never wait for Save.
template <class F>
void visit_pending(app::Settings& a, const app::Settings& b, F&& f) {
#define SAM_PENDING(field) f(a.field, b.field)
    SAM_PENDING(clipboard_clear_seconds);
    SAM_PENDING(auto_lock_minutes);
    SAM_PENDING(show_avatars);
    SAM_PENDING(hide_notes);
    SAM_PENDING(refresh_on_launch);
    SAM_PENDING(gcpd_enabled);
    SAM_PENDING(steam_cache_hours);
    SAM_PENDING(auto_refresh_enabled);
    SAM_PENDING(auto_refresh_minutes);
    SAM_PENDING(disable_cloud_on_login);
    SAM_PENDING(disable_news_on_login);
    SAM_PENDING(disable_remote_play_on_login);
    SAM_PENDING(login_invisible);
    SAM_PENDING(disable_workshop_on_login);
    SAM_PENDING(remember_password_on_login);
    SAM_PENDING(sign_in_method);
    SAM_PENDING(web_api_key);
    SAM_PENDING(check_updates_on_launch);
    SAM_PENDING(theme_mode);
    SAM_PENDING(ui_accent);
    SAM_PENDING(ui_scale);

    SAM_PENDING(info.show_vac);
    SAM_PENDING(info.show_game_ban);
    SAM_PENDING(info.show_community_ban);
    SAM_PENDING(info.show_trade_ban);
    SAM_PENDING(info.show_steam_level);
    SAM_PENDING(info.show_owned_games);
    SAM_PENDING(info.show_steam_id);
    SAM_PENDING(info.show_premier);
    SAM_PENDING(info.show_wingman);
    SAM_PENDING(info.show_prime);
    SAM_PENDING(info.show_vac_live);
    SAM_PENDING(info.show_cooldown);
    SAM_PENDING(info.show_weekly_drop);
    SAM_PENDING(info.show_external_funds);

    SAM_PENDING(notifications.enabled);
    SAM_PENDING(notifications.surface_in_card);
    SAM_PENDING(notifications.surface_toast);
    SAM_PENDING(notifications.surface_windows_notification);
    SAM_PENDING(notifications.on_new_vac_ban);
    SAM_PENDING(notifications.on_new_game_ban);
    SAM_PENDING(notifications.on_new_community_ban);
    SAM_PENDING(notifications.on_new_trade_ban);
    SAM_PENDING(notifications.on_new_vac_live);
    SAM_PENDING(notifications.on_ban_removed);
    SAM_PENDING(notifications.on_cooldown_started);
    SAM_PENDING(notifications.on_cooldown_ended);
    SAM_PENDING(notifications.toast_duration_seconds);
    SAM_PENDING(notifications.coalesce_threshold);
    SAM_PENDING(notifications.retention_days);

    SAM_PENDING(list_view.show_cooldown_marker);
    SAM_PENDING(list_view.show_unread_badge);
    SAM_PENDING(list_view.show_weekly_drop_marker);
    SAM_PENDING(list_view.hide_account_name);

    SAM_PENDING(sda.auto_copy_on_select);
    SAM_PENDING(sda.show_next_code);
    SAM_PENDING(sda.hide_current_code);

    SAM_PENDING(confirmations.per_account_cooldown_seconds);
    SAM_PENDING(confirmations.refresh_stagger_ms);
    SAM_PENDING(confirmations.permanent_failure_threshold);
    SAM_PENDING(confirmations.background_poll_enabled);
    SAM_PENDING(confirmations.background_poll_minutes);
    SAM_PENDING(confirmations.toast_on_new_confirmations);
    SAM_PENDING(confirmations.show_account_search);
    SAM_PENDING(confirmations.auto_approve_enabled);
    SAM_PENDING(confirmations.auto_approve_market);
    SAM_PENDING(confirmations.auto_approve_phone_change);
    SAM_PENDING(confirmations.audit_retention_days);

    SAM_PENDING(trade.auto_confirm_sent);
    SAM_PENDING(trade.per_account_cooldown_seconds);
    SAM_PENDING(trade.inventory_cooldown_seconds);
    SAM_PENDING(trade.refresh_stagger_ms);

    SAM_PENDING(cs2_video.mode);
    SAM_PENDING(cs2_video.launch_options);

    SAM_PENDING(cs2_gc.enabled);
    SAM_PENDING(cs2_gc.show_weekly_drop);
    SAM_PENDING(cs2_gc.show_weekly_mission);
    SAM_PENDING(cs2_gc.show_inventory);
    SAM_PENDING(cs2_gc.show_storage_units);
    SAM_PENDING(cs2_gc.auto_mark_claimed);
    SAM_PENDING(cs2_gc.auto_pull_on_startup);
    SAM_PENDING(cs2_gc.cache_hours);

    SAM_PENDING(hwid.always_spoof);
    SAM_PENDING(hwid.new_profile_every_launch);
    SAM_PENDING(hwid.component_mask);

    SAM_PENDING(cleaner.enabled);
    SAM_PENDING(cleaner.profile);
    SAM_PENDING(cleaner.run_before_launch);
    SAM_PENDING(cleaner.run_on_unlock);
    SAM_PENDING(cleaner.run_on_exit);
    SAM_PENDING(cleaner.preserved_steam_ids);
#undef SAM_PENDING
}

struct Tracking {
    bool have = false;
    app::Settings saved;
    int last_frame = -2;
    std::string vault_id;
    bool privacy = false;
};

Tracking& tracking() {
    static Tracking t;
    return t;
}

bool appearance_differs(const app::Settings& a, const app::Settings& b) {
    return a.theme_mode != b.theme_mode || a.ui_accent != b.ui_accent || a.ui_scale != b.ui_scale;
}

}  // namespace

// Each section's rows sit in one card, with a hairline between rows. A card opens with the
// first row that passes the search and closes at the next section, before content that is not
// a row (a block), at the end of the dependent group it was opened in, and at the end of the
// category, so cards never cross a group boundary.
void open_card(Page& p) {
    if (p.card_open) return;
    char id[32];
    std::snprintf(id, sizeof(id), "##settings-card-%d", p.card_index++);
    facet::BeginCard(id);
    p.card_open = true;
    p.card_depth = p.group_depth;
    p.rows_in_card = 0;
}

void close_card(Page& p) {
    if (!p.card_open || p.card_depth != p.group_depth) return;
    facet::EndCard();
    p.card_open = false;
    p.rows_in_card = 0;
}

void begin_category(Page& p, SettingsCategory category) {
    p.category = category;
    p.category_title = kit::settings_category_title(category);
    p.section_title = nullptr;
    p.section_description = nullptr;
    p.section_shown = false;
    p.category_shown = false;
    p.first_section = true;
}

void end_category(Page& p) {
    close_card(p);
}

void section(Page& p, const char* title, const char* description) {
    close_card(p);
    p.section_title = title;
    p.section_description = description;
    p.section_shown = false;
}

void reveal(Page& p) {
    if (p.searching && !p.category_shown) {
        p.category_shown = true;
        int shown_before = 0;
        for (const int h : p.hits) shown_before += h;
        if (shown_before > 0) vertical_gap(facet::T().spacing.xxxl);
        facet::Text(facet::TextStyle::Title3, "%s", p.category_title);
        p.first_section = true;
    }
    if (p.section_title != nullptr && !p.section_shown) {
        p.section_shown = true;
        if (!p.first_section) vertical_gap(facet::T().spacing.xl);
        p.first_section = false;
        facet::SectionHeader(p.section_title,
                             {.description = p.searching ? nullptr : p.section_description});
    }
}

bool begin_dependent(Page& p, const char* id, bool enabled, bool indent) {
    const bool shown = facet::BeginDependentGroup(id, enabled, {.indent = indent});
    if (shown) {
        ++p.group_depth;
        p.indent_stack[p.group_depth] = indent;
        if (indent) ++p.indent_depth;
    }
    return shown;
}

void end_dependent(Page& p) {
    close_card(p);
    if (p.indent_stack[p.group_depth]) --p.indent_depth;
    --p.group_depth;
    facet::EndDependentGroup();
}

// The row between begin_row and end_row (rows do not nest).
struct OpenRow {
    std::string description;
    std::string label;
    bool modified = false;
    bool reset = false;
    bool disabled = false;
};

OpenRow& open_row() {
    static OpenRow r;
    return r;
}

bool begin_row(Page& p, const char* label, const RowSpec& spec_in) {
    OpenRow& row = open_row();
    row.description.clear();
    if (spec_in.description != nullptr) row.description = spec_in.description;
    if (spec_in.immediate) row.description += row.description.empty() ? kImmediateNote : std::string(" ") + kImmediateNote;
    RowSpec spec = spec_in;
    spec.description = row.description.empty() ? nullptr : row.description.c_str();
    if (!facet::SettingsSearchMatch(label, {.description = spec.description,
                                            .keywords = spec.keywords,
                                            .section = p.category_title})) {
        return false;
    }
    reveal(p);
    open_card(p);
    if (p.rows_in_card++ > 0) facet::Separator();
    ++p.hits[static_cast<int>(p.category)];
    // Rows in an indented dependent group give up the indent in their label column only, so every
    // control of a card starts at one x.
    const float indent = facet::T().spacing.lg * static_cast<float>(p.indent_depth);
    facet::BeginSettingsRow(label, {.description = spec.description,
                                    .modified = spec.modified,
                                    .labelWidth = std::max(120.0F, p.label_width - indent)});
    row.label = label;
    row.modified = spec.modified;
    row.reset = spec.reset;
    row.disabled = p.controls_disabled;
    if (row.disabled) ImGui::BeginDisabled(true);
    return true;
}

bool end_row() {
    OpenRow& row = open_row();
    if (row.disabled) ImGui::EndDisabled();
    // The reset button follows the control instead of sitting at the far edge of the card.
    bool pressed = false;
    if (row.reset && row.modified && !row.disabled) {
        ImGui::SameLine();
        pressed = facet::IconButton("Reset to the default##row-reset", facet::Icon("reset"),
                                    {.size = facet::ControlSize::Small});
    }
    facet::EndSettingsRow();
    if (row.modified) {
        // The modified dot after the label explains itself on hover: a zero-height item over it
        // carries the tooltip, and the layout continues where the row ended.
        const ImVec2 row_min = ImGui::GetItemRectMin();
        const ImVec2 resume = ImGui::GetCursorScreenPos();
        const char* end = row.label.c_str();
        while (*end != 0 && !(end[0] == '#' && end[1] == '#')) ++end;
        const float label_w = facet::MeasureText(facet::TextStyle::Body, row.label.c_str(), end).x;
        ImGui::SetCursorScreenPos(ImVec2(row_min.x + label_w, row_min.y));
        ImGui::Dummy(ImVec2(facet::Px(16.0F), facet::ControlHeight(facet::ControlSize::Inherit)));
        facet::ItemTooltip("Changed from the default");
        ImGui::SetCursorScreenPos(ImVec2(resume.x, resume.y - ImGui::GetStyle().ItemSpacing.y));
        ImGui::Dummy(ImVec2(0.0F, 0.0F));
    }
    return pressed;
}

bool block(Page& p, const char* label, const char* description, const char* keywords) {
    if (!p.searching) {
        close_card(p);
        reveal(p);
        return true;
    }
    if (begin_row(p, label, {.description = description, .keywords = keywords})) {
        const std::string button = std::string("Show in ") + p.category_title;
        if (fit_button(button.c_str())) p.open_category = static_cast<int>(p.category);
        end_row();
    }
    return false;
}

bool toggle_row(Page& p, const char* label, bool* value, bool def, const char* description,
                const char* keywords) {
    bool written = false;
    if (begin_row(p, label, {.description = description,
                             .keywords = keywords,
                             .modified = *value != def,
                             .reset = true})) {
        written = facet::Toggle("##value", value);
        if (end_row()) {
            *value = def;
            written = true;
        }
    }
    return written;
}

bool number_row(Page& p, const char* label, int* value, int def, int min, int max, const char* unit,
                const char* description, const char* keywords) {
    bool written = false;
    if (begin_row(p, label, {.description = description,
                             .keywords = keywords,
                             .modified = *value != def,
                             .reset = true})) {
        written = facet::NumberInput("##value", value,
                                     {.min = static_cast<double>(min),
                                      .max = static_cast<double>(max),
                                      .unit = unit,
                                      .width = 180.0F});
        if (end_row()) {
            *value = def;
            written = true;
        }
    }
    return written;
}

float cap(float units) {
    return std::max(40.0F, std::min(units, facet::AvailableWidthUnits()));
}

void align_trailing(float total_units) {
    const float x = ImGui::GetContentRegionAvail().x - facet::Px(total_units);
    if (x > 0.0F) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
}

void ban_risk_banner(Page& p, const char* message) {
    if (p.searching) return;
    if (facet::Banner("Ban risk##ban-risk", nullptr, message,
                      {.tone = facet::Tone::Warning, .primaryAction = "Turn on safe mode"}) ==
        facet::BannerAction::Primary) {
        p.s.safe_mode = true;
        commit_now(p.state);
    }
    vertical_gap(facet::T().spacing.lg);
}

void restart_elevated(app::AppState& state) {
    if (platform::process::relaunch_elevated(L"--switch")) {
        PostMessageW(state.main_hwnd, WM_CLOSE, 0, 0);
    }
}

// --- Pending changes ---------------------------------------------------------------------------

app::Settings& saved() {
    return tracking().saved;
}

void commit_now(app::AppState& state) {
    Tracking& t = tracking();
    if (!t.have) {
        state.save_settings();
        return;
    }
    app::Settings live = state.settings;
    visit_pending(state.settings, t.saved, [](auto& a, const auto& b) { a = b; });
    state.save_settings();
    state.settings = std::move(live);
}

void track_saved_state(app::AppState& state) {
    Tracking& t = tracking();
    const int frame = ImGui::GetFrameCount();
    const bool reopened = t.last_frame != frame - 1;
    const std::string vault = platform::active_vault_id();
    const bool vault_changed = t.have && vault != t.vault_id;
    // The title bar's privacy toggle saves every setting, pending ones included.
    const bool saved_elsewhere = t.have && !reopened && state.settings.privacy_mode != t.privacy;
    if (!t.have || vault_changed || saved_elsewhere || (reopened && pending_count(state) == 0)) {
        t.saved = state.settings;
        t.have = true;
    }
    t.vault_id = vault;
    t.last_frame = frame;
}

void end_frame_tracking(const app::AppState& state) {
    tracking().privacy = state.settings.privacy_mode;
}

int pending_count(const app::AppState& state) {
    Tracking& t = tracking();
    if (!t.have) return 0;
    int count = 0;
    visit_pending(const_cast<app::Settings&>(state.settings), t.saved,
                  [&count](const auto& a, const auto& b) { count += (a == b) ? 0 : 1; });
    return count;
}

bool cleaner_settings_pending(const app::AppState& state) {
    Tracking& t = tracking();
    if (!t.have) return false;
    const auto& a = state.settings.cleaner;
    const auto& b = t.saved.cleaner;
    return a.enabled != b.enabled || a.profile != b.profile || a.run_before_launch != b.run_before_launch ||
           a.run_on_unlock != b.run_on_unlock || a.run_on_exit != b.run_on_exit ||
           a.preserved_steam_ids != b.preserved_steam_ids;
}

void save_pending(app::AppState& state) {
    state.save_settings();
    tracking().saved = state.settings;
    tracking().have = true;
}

void discard_pending(app::AppState& state) {
    Tracking& t = tracking();
    if (!t.have) return;
    const bool appearance = appearance_differs(state.settings, t.saved);
    visit_pending(state.settings, t.saved, [](auto& a, const auto& b) { a = b; });
    if (appearance) theme::apply_appearance(state.settings, true);
}

}  // namespace sam::ui::screens::settings
