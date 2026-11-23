#include "ui/screens/add_account/bulk.hpp"

#include <algorithm>
#include <cstdio>
#include <span>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/icons.h>
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

#include "ui/kit/account.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/add_account_detail.hpp"

namespace sam::ui::screens::add_account_detail {

namespace {

constexpr ImGuiWindowFlags kRegionFlags =
    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

struct FlowFrame {
    bool body_open = false;
};
FlowFrame g_flow;

struct StatusLook {
    const char* text;
    facet::Tone tone;
};

StatusLook status_look(RowStatus status) {
    switch (status) {
        case RowStatus::New: return {"New", facet::Tone::Neutral};
        case RowStatus::Update: return {"Updates existing", facet::Tone::Neutral};
        case RowStatus::Blocked: return {"Can't be imported", facet::Tone::Warning};
        case RowStatus::Imported: return {"Added", facet::Tone::Success};
        case RowStatus::Updated: return {"Updated", facet::Tone::Success};
        case RowStatus::Skipped: return {"Skipped", facet::Tone::Neutral};
        case RowStatus::NotImported: return {"Not imported", facet::Tone::Warning};
        case RowStatus::Failed: return {"Failed", facet::Tone::Destructive};
    }
    return {"", facet::Tone::Neutral};
}

float commit_bar_units() {
    const facet::Theme& t = facet::T();
    // Two gaps, the hairline and the button row, each followed by the item spacing of the window.
    const float spacing = ImGui::GetStyle().ItemSpacing.y / facet::Px(1.0F);
    return t.spacing.md * 2.0F + t.metrics.hairline + t.metrics.controlHeight + spacing * 5.0F + t.spacing.xs;
}

void pill(const char* id, int count, const char* what, facet::Tone tone) {
    if (count <= 0) return;
    char label[96];
    std::snprintf(label, sizeof(label), "%s %s###%s", kit::format_count(count).c_str(), what, id);
    facet::StatusPill(label, {.tone = tone, .size = facet::ControlSize::Small});
}

void pills(const std::vector<BulkRow>& rows, bool result) {
    facet::BeginStack("##pills", {.orientation = facet::Orientation::Horizontal, .gap = facet::Gap::Tight});
    if (result) {
        const BulkCounts c = count_results(rows);
        pill("imp", c.imported, "added", facet::Tone::Success);
        pill("upd", c.updated, "updated", facet::Tone::Success);
        pill("skp", c.skipped, "skipped", facet::Tone::Neutral);
        pill("not", c.not_imported, "not imported", facet::Tone::Warning);
        pill("fail", c.failed, "failed", facet::Tone::Destructive);
    } else {
        int fresh = 0;
        int update = 0;
        int blocked = 0;
        for (const auto& r : rows) {
            if (r.status == RowStatus::New) ++fresh;
            if (r.status == RowStatus::Update) ++update;
            if (r.status == RowStatus::Blocked) ++blocked;
        }
        pill("new", fresh, "new", facet::Tone::Neutral);
        pill("upd", update, update == 1 ? "update" : "updates", facet::Tone::Neutral);
        pill("blk", blocked, "can't be imported", facet::Tone::Warning);
    }
    facet::EndStack();
}

}  // namespace

void hint(const char* text) {
    facet::StyledText(text, {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
}

void gap(float units) { ImGui::Dummy(ImVec2(0.0F, facet::Px(units))); }

// --- Flow shell ---------------------------------------------------------------------------------

// Why a fixed-height region with its own scrolling body: the commit button of a flow must stay
// on screen (rule 7), so only the step body scrolls and the bar under it never moves.
void begin_flow(const char* id, std::span<const char* const> steps, int current, float width) {
    const facet::Theme& t = facet::T();
    const float avail = facet::AvailableWidthUnits();
    const float w = std::min(avail, width);
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + facet::Px((avail - w) * 0.5F));
    ImGui::BeginChild(id, ImVec2(facet::Px(w), 0.0F), ImGuiChildFlags_NavFlattened, kRegionFlags);
    facet::StepIndicator("##steps", current, steps);
    gap(t.spacing.lg);
    const float left = ImGui::GetContentRegionAvail().y / facet::Px(1.0F) - commit_bar_units();
    g_flow.body_open = facet::BeginScrollArea("##body", {.height = std::max(left, t.metrics.controlHeight * 3.0F)});
}

float body_height_left() { return ImGui::GetContentRegionAvail().y / facet::Px(1.0F); }

void begin_commit_bar(const char* status) {
    const facet::Theme& t = facet::T();
    if (g_flow.body_open) facet::EndScrollArea();
    g_flow.body_open = false;
    gap(t.spacing.md);
    facet::Separator();
    gap(t.spacing.md);
    facet::BeginStack("##commit", {.orientation = facet::Orientation::Horizontal,
                                   .width = facet::AvailableWidthUnits()});
    if (status != nullptr) facet::StyledText(status, {.style = facet::TextStyle::Footnote, .secondary = true});
    facet::StackSpacer();
    facet::BeginStack("##actions", {.orientation = facet::Orientation::Horizontal});
}

void end_flow() {
    facet::EndStack();
    facet::EndStack();
    ImGui::EndChild();
}

void begin_step_card(const char* id, const char* title, const char* description, bool fill) {
    const facet::Theme& t = facet::T();
    const float height = fill ? std::max(0.0F, body_height_left() - t.spacing.xs) : 0.0F;
    facet::BeginCard(id, {.height = height});
    if (title == nullptr) return;
    facet::Text(facet::TextStyle::Headline, "%s", title);
    if (description != nullptr) {
        gap(t.spacing.xs);
        hint(description);
    }
    gap(t.spacing.lg);
}

void end_step_card() { facet::EndCard(); }

void done_header(const char* title, const char* message, bool warning) {
    const facet::Theme& t = facet::T();
    const facet::StackOptions centered{.align = facet::LayoutAlign::Center, .width = facet::AvailableWidthUnits()};
    facet::BeginStack("##done-mark", centered);
    facet::IconGlyph(facet::Icon(warning ? "warning" : "success"), facet::Px(t.metrics.controlHeightLarge),
                     facet::ToU32(warning ? t.colors.warning : t.colors.success));
    facet::EndStack();
    gap(t.spacing.sm);
    facet::BeginStack("##done-title", centered);
    facet::Text(facet::TextStyle::Title3, "%s", title);
    facet::EndStack();
    if (message != nullptr) {
        gap(t.spacing.xs);
        facet::BeginStack("##done-text", centered);
        facet::StyledText(message, {.style = facet::TextStyle::Footnote, .secondary = true});
        facet::EndStack();
    }
    gap(t.spacing.lg);
}

bool flow_error(const char* id, const char* title, const char* message, const std::string& raw, const char* action) {
    ImGui::PushID(id);
    char label[160];
    std::snprintf(label, sizeof(label), "%s##alert", title);
    const bool pressed = facet::InlineAlert(label, nullptr, message,
                                            {.tone = facet::Tone::Destructive, .actionLabel = action}) ==
                         facet::InlineAlertAction::Action;
    if (!raw.empty() && facet::BeginDisclosure("Details", nullptr, {.style = facet::DisclosureStyle::Link})) {
        facet::StyledText(raw.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
        facet::EndDisclosure();
    }
    ImGui::PopID();
    return pressed;
}

void open_accounts_button(app::AppState& state, const std::string& account_id) {
    if (facet::Button(account_id.empty() ? "Open accounts" : "Open account", {.variant = facet::ButtonVariant::Primary})) {
        if (account_id.empty()) {
            kit::go_to(state, app::Screen::Accounts);
        } else {
            kit::open_account(state, account_id);
        }
        reset_screen();
    }
}

// --- Bulk imports -------------------------------------------------------------------------------

bool row_importable(const BulkRow& row) {
    return row.status == RowStatus::New || row.status == RowStatus::Update;
}

int selected_count(const std::vector<BulkRow>& rows) {
    return static_cast<int>(std::count_if(rows.begin(), rows.end(),
                                          [](const BulkRow& r) { return r.checked && row_importable(r); }));
}

void set_all(std::vector<BulkRow>& rows, bool checked) {
    for (auto& r : rows) r.checked = checked && row_importable(r);
}

void finish_rows(std::vector<BulkRow>& rows) {
    for (auto& r : rows) {
        if (r.status == RowStatus::Blocked) {
            r.status = RowStatus::NotImported;
        } else if (row_importable(r)) {
            r.status = RowStatus::Skipped;
            r.note = "Not selected.";
        }
    }
}

void select_all_box(const char* id, std::vector<BulkRow>& rows) {
    ImGui::PushID(id);
    const int importable =
        static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const BulkRow& r) { return row_importable(r); }));
    const int selected = selected_count(rows);
    facet::CheckState all = selected == 0            ? facet::CheckState::Off
                            : selected == importable ? facet::CheckState::On
                                                     : facet::CheckState::Mixed;
    ImGui::BeginDisabled(importable == 0);
    if (facet::Checkbox("Select all", &all)) set_all(rows, all == facet::CheckState::On);
    ImGui::EndDisabled();
    ImGui::PopID();
}

std::string selection_status(const std::vector<BulkRow>& rows) {
    const int importable =
        static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const BulkRow& r) { return row_importable(r); }));
    std::string s = kit::format_count(selected_count(rows)) + " of " + kit::format_count(importable) + " selected";
    const int blocked = static_cast<int>(rows.size()) - importable;
    if (blocked > 0) s += ", " + kit::format_count(blocked) + " can't be imported";
    return s;
}

// Why a data table: a few hundred files must stay readable and scroll smoothly, and the status
// column lines up so the eye can run down it looking for "Failed".
void bulk_table(const char* id, std::vector<BulkRow>& rows, const BulkTableOptions& options) {
    const facet::Theme& t = facet::T();
    const facet::DataTableColumn columns[] = {
        {.label = "##pick", .width = t.metrics.checkboxSize + t.spacing.lg, .flags = ImGuiTableColumnFlags_NoResize},
        {.label = "Account"},
        {.label = options.source_header},
        {.label = "Status", .width = 136.0F},
        {.label = "Details"},
    };
    const std::span<const facet::DataTableColumn> shown =
        options.selectable ? std::span<const facet::DataTableColumn>(columns)
                           : std::span<const facet::DataTableColumn>(columns).subspan(1);
    const float min_height = t.table.headerHeight + t.list.rowHeight * 3.0F;
    const float height = options.height > 0.0F ? options.height : std::max(min_height, body_height_left() - t.spacing.lg);

    if (!facet::BeginDataTable(id, shown, static_cast<int>(rows.size()), nullptr,
                               {.resizable = false, .emptyText = "Nothing found", .height = height})) {
        return;
    }
    facet::RowRange range;
    while (facet::NextVisibleRows(&range)) {
        for (int i = range.first; i < range.last; ++i) {
            BulkRow& row = rows[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            const bool importable = row_importable(row);
            if (facet::DataTableRow(i) && options.selectable && importable) row.checked = !row.checked;
            if (options.selectable) {
                ImGui::TableNextColumn();
                kit::RowCell cell;
                ImGui::BeginDisabled(!importable);
                bool checked = row.checked && importable;
                if (facet::Checkbox("##pick", &checked)) row.checked = checked;
                ImGui::EndDisabled();
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::TruncatedText(options.hide_names ? kit::kHiddenLogin : row.name.c_str());
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::TruncatedText(row.source.c_str(), {.mode = facet::TruncateMode::Middle, .secondary = true});
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                const StatusLook look = status_look(row.status);
                facet::Badge(look.text, {.tone = look.tone});
            }
            ImGui::TableNextColumn();
            {
                kit::RowCell cell;
                facet::TruncatedText(row.note.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
                if (!row.raw.empty()) facet::ItemTooltip("%s", row.raw.c_str());
            }
            ImGui::PopID();
        }
    }
    facet::EndDataTable();
}

BulkCounts count_results(const std::vector<BulkRow>& rows) {
    BulkCounts c;
    for (const auto& r : rows) {
        switch (r.status) {
            case RowStatus::Imported: ++c.imported; break;
            case RowStatus::Updated: ++c.updated; break;
            case RowStatus::Failed: ++c.failed; break;
            case RowStatus::NotImported:
            case RowStatus::Blocked: ++c.not_imported; break;
            default: ++c.skipped; break;
        }
    }
    return c;
}

// Why the counts sit beside the title: the outcome reads in one glance at the card's head, and
// the table below says the why for each row, so no banner has to repeat it.
void bulk_header(const char* title, const std::vector<BulkRow>& rows, bool result) {
    const facet::Theme& t = facet::T();
    const bool narrow = facet::AvailableWidthUnits() < 640.0F;
    facet::BeginStack("##bulk-head", {.orientation = facet::Orientation::Horizontal,
                                      .align = facet::LayoutAlign::Center,
                                      .width = facet::AvailableWidthUnits()});
    facet::Text(facet::TextStyle::Headline, "%s", title);
    facet::StackSpacer();
    if (!narrow) pills(rows, result);
    facet::EndStack();
    if (narrow) {
        gap(t.spacing.xs);
        pills(rows, result);
    }
    gap(t.spacing.md);
}

}  // namespace sam::ui::screens::add_account_detail
