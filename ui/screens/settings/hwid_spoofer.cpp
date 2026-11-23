// Settings > HWID spoofer: whether every launch spoofs, which components, and the real
// hardware next to what the last spoofed account was given.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/text.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/selection.h>

#include "ui/kit/account.hpp"
#include "ui/kit/actions.hpp"
#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

struct Component {
    const char* label;
    std::uint32_t bit;
};

constexpr std::array<Component, 10> kComponents = {{{"Machine GUID", 0x001u},
                                                     {"MAC address", 0x002u},
                                                     {"Disk serial", 0x004u},
                                                     {"PC name", 0x008u},
                                                     {"GPU", 0x010u},
                                                     {"Motherboard", 0x020u},
                                                     {"RAM", 0x040u},
                                                     {"Monitor", 0x080u},
                                                     {"Storage", 0x100u},
                                                     {"Sound card", 0x200u}}};

void draw_components(Page& p) {
    auto& mask = p.s.hwid.component_mask;
    if (!begin_row(p, "Components",
                   {.description = "The identifiers a spoofed launch replaces, also for accounts you spoof one by one from "
                                   "their menu, so they stay editable while Spoof every launch is off.",
                    .keywords = "components guid mac disk serial gpu motherboard ram monitor storage sound",
                    .modified = mask != p.d.hwid.component_mask,
                    .reset = true})) {
        return;
    }
    std::array<facet::ChoiceItem, kComponents.size()> items{};
    std::array<bool, kComponents.size()> checked{};
    for (std::size_t i = 0; i < kComponents.size(); ++i) {
        items[i].label = kComponents[i].label;
        checked[i] = (mask & kComponents[i].bit) != 0;
    }
    if (facet::CheckboxGroup("##value", checked, items, {.selectAll = true})) {
        for (std::size_t i = 0; i < kComponents.size(); ++i) {
            if (checked[i]) {
                mask |= kComponents[i].bit;
            } else {
                mask &= ~kComponents[i].bit;
            }
        }
    }
    if (end_row()) mask = p.d.hwid.component_mask;
}

std::string monitor_text(int w, int h, int hz) {
    if (w <= 0 || h <= 0) return {};
    return std::to_string(w) + " x " + std::to_string(h) + " at " + std::to_string(hz) + " Hz";
}

// Why a table: each component is a row with two values to compare side by side.
void draw_comparison(Page& p) {
    section(p, "Hardware comparison", "This PC next to the values the last spoofed account got.");
    if (!block(p, "Hardware comparison", "The real hardware next to what the last spoofed account got.",
               "comparison real spoofed hardware values")) {
        return;
    }
    facet::CardScope card("##hwid-compare-card");
    const core::Account* acc =
        p.state.last_hwid_account_id.empty() ? nullptr : p.state.find_account(p.state.last_hwid_account_id);
    if (acc == nullptr || !acc->hwid.has_value()) {
        kit::empty_state("Nothing spoofed yet",
                         {.message = "Launch an account with the spoofer on to compare its values with this PC.",
                          .icon = facet::Icon("monitor")});
        return;
    }
    const std::string caption = "Spoofed values of " + kit::display_name(p.state, *acc) + ", the last account launched.";
    facet::StyledText(caption.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});

    const auto& real = p.state.real_hardware;
    const auto& fake = *acc->hwid;
    struct Row {
        const char* label;
        std::string real;
        std::string fake;
    };
    const std::vector<Row> rows = {
        {"Machine GUID", real.machine_guid, fake.machine_guid},
        {"MAC address", real.mac_address, fake.mac_address},
        {"Disk serial", real.disk_serial, fake.disk_serial},
        {"PC name", real.pc_name, fake.pc_name},
        {"GPU", real.gpu_name, fake.gpu_name},
        {"Motherboard", real.board_model, fake.board_model},
        {"Board maker", real.board_manufacturer, fake.board_manufacturer},
        {"RAM", real.ram_mb > 0 ? std::to_string(real.ram_mb) + " MB" : std::string{},
         fake.ram_mb > 0 ? std::to_string(fake.ram_mb) + " MB" : std::string{}},
        {"Monitor", monitor_text(real.monitor_width, real.monitor_height, real.monitor_refresh),
         monitor_text(fake.monitor_width, fake.monitor_height, fake.monitor_refresh)},
        {"Sound card", real.sound_card, fake.sound_card},
        {"Display", real.display_model, fake.display_model},
    };
    static const facet::DataTableColumn kColumns[] = {
        {.label = "Component", .width = 130.0F}, {.label = "This PC"}, {.label = "Spoofed"}};
    if (facet::BeginDataTable("##hwid-compare", kColumns, static_cast<int>(rows.size()), nullptr,
                              {.resizable = true, .height = 40.0F + 36.0F * static_cast<float>(rows.size())})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                const Row& r = rows[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                facet::DataTableRow(i);
                ImGui::TableNextColumn();
                facet::StyledText(r.label, {.secondary = true});
                ImGui::TableNextColumn();
                facet::TruncatedText(r.real.empty() ? "Unknown" : r.real.c_str());
                ImGui::TableNextColumn();
                facet::TruncatedText(r.fake.empty() ? "Unknown" : r.fake.c_str());
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
}

}  // namespace

void draw_hwid_spoofer(Page& p) {
    ban_risk_banner(p, "The spoofer changes the hardware identifiers Steam and games read when an account "
                       "launches. That can put the launched accounts at risk, which is why safe mode hides it.");
    section(p, "Spoofing");
    toggle_row(p, "Spoof every launch", &p.s.hwid.always_spoof, p.d.hwid.always_spoof,
               "Spoofs the hardware identifiers for every account you launch. Leave single accounts out from "
               "their menu on Accounts.",
               "always spoof hwid hardware launch");
    draw_components(p);
    draw_comparison(p);
}

}  // namespace sam::ui::screens::settings
