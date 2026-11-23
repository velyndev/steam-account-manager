// Capture scenes of the kit itself: action cards, the Primary empty state, the quiet destructive
// button, row centring in a data table (kit-sheet), and a drawer opened from a page, which must
// start under the title bar (kit-drawer). Debug builds only; see app/ui_capture.hpp.

#ifdef SAM_UI_CAPTURE

#include <chrono>

#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

#include <facet/core/text.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/layout.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/standing.hpp"
#include "ui/kit/transient.hpp"
#include "ui/main_window.hpp"

namespace {

using sam::app::AppState;
namespace kit = sam::ui::kit;

bool g_drawer_open = false;
bool g_checked[8] = {};

void reset_sheet(AppState&) {
    g_drawer_open = false;
    for (bool& c : g_checked) c = false;
}

void draw_kit_sheet(AppState& state) {
    kit::PageScope page("kit-sheet");
    {
        kit::PageHeaderScope header("Kit sheet", {.context = "Action cards, empty state, row centring"});
        kit::fit_button("Secondary action", {.icon = facet::Icon("refresh")});
        kit::quiet_destructive_button("Remove account");
        kit::fit_button("Primary action", {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("plus")});
    }
    kit::web_api_key_notice(state, {.compact = true});
    ImGui::Dummy(ImVec2(0.0F, 8.0F));
    facet::BeginGrid("##cards", {.minCellWidth = 220.0F, .maxColumns = 3});
    facet::GridNext();
    kit::action_card("Sign in with a password", {.icon = sam::ui::icons::token,
                                                 .description = "Full access: trading, confirmations and the authenticator.",
                                                 .equal_height_group = "cards"});
    facet::GridNext();
    kit::action_card("Import a maFile", {.icon = facet::Icon("file"),
                                          .description = "An authenticator file from SDA.",
                                          .equal_height_group = "cards"});
    facet::GridNext();
    ImGui::BeginDisabled();
    kit::action_card("From this PC", {.icon = facet::Icon("monitor"),
                                       .description = "Disabled look.",
                                       .equal_height_group = "cards"});
    ImGui::EndDisabled();
    facet::EndGrid();

    ImGui::Dummy(ImVec2(0.0F, 8.0F));
    static const facet::DataTableColumn kColumns[] = {
        {.label = "Account"}, {.label = "Standing", .width = 220.0F}, {.label = "Premier", .width = 90.0F, .align = facet::Align::End},
        {.label = "Cell", .width = 60.0F}, {.label = "Plain", .width = 60.0F}};
    const int rows = 6;
    if (facet::BeginDataTable("##rows", kColumns, rows, nullptr, {.height = 230.0F})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                const sam::core::Account& a = state.vault.accounts[static_cast<std::size_t>(i * 4)];
                ImGui::PushID(i);
                facet::DataTableRow(i);
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    facet::Checkbox("##pick", &g_checked[i]);
                    cell.next();
                    kit::trust_dot(a.trust, 0.0F);
                    cell.next();
                    kit::account_row(state, a, {.layout = kit::AccountRowOptions::Layout::OneLine});
                }
                ImGui::TableNextColumn();
                kit::row_markers(state, a, sam::app::ui_capture::kDemoClockUnix, {.ban = true});
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    facet::StyledText(kit::format_count(a.cs2.premier_rating > 0 ? a.cs2.premier_rating : 0).c_str());
                }
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    facet::Checkbox("##cell", &g_checked[i]);
                }
                ImGui::TableNextColumn();
                facet::Checkbox("##plain", &g_checked[i]);
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
    ImGui::Dummy(ImVec2(0.0F, 8.0F));
    kit::empty_state("No trade offers", {.message = "Nothing is waiting for a decision.",
                                         .icon = sam::ui::icons::trade,
                                         .action_label = "Refresh offers",
                                         .action_icon = facet::Icon("refresh")});
    if (facet::BeginDrawer("Drawer under the title bar", &g_drawer_open)) {
        facet::StyledText("A drawer opened from a page starts below the title bar.", {.wrap = true});
        facet::EndDrawer();
    }
}

void setup_sheet(AppState& state) {
    sam::app::ui_capture::open_demo_vault(state);
    state.current_screen = sam::app::Screen::Settings;
    sam::ui::capture_override_content(&draw_kit_sheet);
}

void setup_drawer(AppState& state) {
    setup_sheet(state);
    g_drawer_open = true;
}

}  // namespace

SAM_KIT_TRANSIENT_RESET(capture_kit_sheet, &reset_sheet)
SAM_UI_CAPTURE_SCENE(kit_sheet, "kit-sheet", &setup_sheet, 30)
SAM_UI_CAPTURE_SCENE(kit_drawer, "kit-drawer", &setup_drawer, 30)

#endif  // SAM_UI_CAPTURE
