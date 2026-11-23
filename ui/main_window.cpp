#include "ui/main_window.hpp"

#include <algorithm>
#include <string>

#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/widgets/containers.h>

#include "platform/paths.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/screens/accounts_screen.hpp"
#include "ui/screens/add_account_screen.hpp"
#include "ui/screens/confirmations_screen.hpp"
#include "ui/screens/cs2_screen.hpp"
#include "ui/screens/sda_screen.hpp"
#include "ui/screens/settings_screen.hpp"
#include "ui/screens/trade_offers_screen.hpp"
#include "ui/screens/unlock_screen.hpp"
#include "ui/screens/vault_picker_screen.hpp"
#include "ui/kit/transient.hpp"
#include "ui/shell/shell.hpp"
#include "ui/util.hpp"

namespace sam::ui {

namespace {

// Below this content width (design units) the sidebar is the icon rail: the expanded column
// (220) next to a list and detail screen needs about 1000 units before the detail pane gets
// cramped.
constexpr float kRailBelowUnits = 1000.0F;

constexpr ImGuiWindowFlags kRegionFlags =
    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

void draw_screen(app::AppState& state) {
    switch (state.current_screen) {
        case app::Screen::Unlock: screens::draw_unlock(state); break;
        case app::Screen::Accounts: screens::draw_accounts(state); break;
        case app::Screen::Authenticator: screens::draw_sda(state); break;
        case app::Screen::Confirmations: screens::draw_confirmations(state); break;
        case app::Screen::TradeOffers: screens::draw_trade_offers(state); break;
        case app::Screen::Cs2: screens::draw_cs2(state); break;
        case app::Screen::AddAccount: screens::draw_add_account(state); break;
        case app::Screen::Settings: screens::draw_settings(state); break;
    }
}

// The redirects made before drawing: a worker that needs a fresh password login opens Add
// account, and CS2 closes its client and falls back to Accounts once it is turned off.
void apply_redirects(app::AppState& state) {
    if (state.pending_relogin_login.has_value() && state.current_screen != app::Screen::AddAccount) {
        state.current_screen = app::Screen::AddAccount;
        state.selected_account_id.clear();
    }
    if (state.current_screen == app::Screen::Cs2 && !state.settings.cs2_gc.enabled) {
        screens::discard_cs2_client(state);
        state.current_screen = app::Screen::Accounts;
    }
}

bool begin_region(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_None, kRegionFlags);
    ImGui::PopStyleVar();
    return open;
}

// Why only sidebar and content: navigation on the leading edge is where a desktop tool's users
// look for it, and the content takes all the rest, down to the window's bottom edge. There is no
// status bar: status lives where it is used, jobs and updates at the bottom of the sidebar, modes
// in the title bar, the version in Settings and the command palette.
void draw_unlocked(app::AppState& state) {
    const facet::Theme& t = facet::T();
    shell::State& st = shell::state();
    apply_redirects(state);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float body_h = std::max(0.0F, avail.y);
    const float inset = facet::Px(t.spacing.sm);
    st.rail = st.force_rail || avail.x / facet::Scale() < kRailBelowUnits;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + inset));
    shell::draw_sidebar(state, std::max(0.0F, body_h - inset * 2.0F));
    const float nav_right = ImGui::GetItemRectMax().x;

    ImGui::SetCursorScreenPos(ImVec2(nav_right, origin.y));
    if (begin_region("##content", ImVec2(std::max(1.0F, origin.x + avail.x - nav_right), std::max(1.0F, body_h)))) {
        if (st.content_override != nullptr) {
            st.content_override(state);
        } else {
            draw_screen(state);
        }
    }
    ImGui::EndChild();
}

// The locked window shows the title bar and the unlock card, which centers itself in the whole
// region (the vault choice lives in that card; draw_vault_picker only preselects a vault).
void draw_locked(app::AppState& state) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (begin_region("##locked", ImVec2(std::max(1.0F, avail.x), std::max(1.0F, avail.y)))) {
        if (state.needs_vault_pick) {
            screens::draw_vault_picker(state);
        } else {
            screens::draw_unlock(state);
        }
    }
    ImGui::EndChild();
}

std::string& ini_path() {
    static std::string path;
    return path;
}

// Transient UI never outlives its context (brief rule 1): a lock or a vault change closes every
// registered dialog, a screen change closes what the screen left registered for leaving.
void track_context(app::AppState& state) {
    shell::State& st = shell::state();
    const std::string vault = platform::active_vault_id();
    if (st.seen) {
        const bool locked = st.was_unlocked && !state.unlocked;
        const bool vault_changed = vault != st.last_vault;
        if (locked || vault_changed) {
            kit::reset_transient_ui(state);
            st.palette_open = false;
            st.sheet_open = false;
            st.vault_menu_open = false;
            st.activity_open = false;
        } else if (state.current_screen != st.last_screen) {
            kit::leave_screen(state, st.last_screen);
        }
    }
    st.seen = true;
    st.was_unlocked = state.unlocked;
    st.last_vault = vault;
    st.last_screen = state.current_screen;
}

// Why the work rect shrinks by the title bar: Facet's drawers, toasts, the palette and modal
// clamping place themselves in the main viewport's work rect, so with the bar taken out of it
// nothing a screen opens can cover the window buttons or the search pill. The inset applies
// from the next frame, as Dear ImGui's own menu bars do.
void reserve_title_bar() {
    auto* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
    viewport->BuildWorkInsetMin.y += facet::Px(facet::T().containers.titleBarHeight);
}

}  // namespace

bool draw(app::AppState& state) {
    track_context(state);
    shell::State& st = shell::state();
    const bool maximized = state.main_hwnd != nullptr && IsZoomed(state.main_hwnd) != FALSE;
    if (facet::BeginAppFrame("##sam", {.hostMaximized = maximized,
                                       .minSize = ImVec2(kMinWindowWidth, kMinWindowHeight)})) {
        reserve_title_bar();
        shell::handle_shortcuts(state);
        shell::draw_title_bar(state);
        if (state.unlocked) {
            draw_unlocked(state);
        } else {
            draw_locked(state);
        }
        shell::draw_command_palette(state);
        shell::draw_update_dialog(state);
        st.caption_blocked = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        facet::EndAppFrame();
    }
    shell::render_toasts(state);
    return true;
}

void configure_ini_file() {
    std::error_code ec;
    const std::filesystem::path dir = platform::data_dir();
    std::filesystem::create_directories(dir, ec);
    ini_path() = to_utf8((dir / L"ui_state.ini").wstring());
    ImGui::GetIO().IniFilename = ini_path().c_str();
}

bool caption_blocked() {
    return shell::state().caption_blocked;
}

void reset_shell_state() {
    shell::reset_state();
}

#ifdef SAM_UI_CAPTURE
void capture_open_palette(const char* query) {
    shell::State& st = shell::state();
    st.palette_open = true;
    st.pending_palette_query = query != nullptr ? query : "";
    st.pending_palette_frames = 3;
}

void capture_force_rail() {
    shell::state().force_rail = true;
}

void capture_override_content(void (*content)(app::AppState& state)) {
    shell::state().content_override = content;
}
#endif

}  // namespace sam::ui
