// The list view: the grouped account list beside the account detail.

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>

#include <facet/core/color.h>
#include <facet/core/draw.h>
#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>

#include "ui/icons.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/row.hpp"
#include "ui/kit/standing.hpp"
#include "ui/screens/accounts/accounts_internal.hpp"

namespace sam::ui::screens::accounts {

namespace {

constexpr const char* kDragPayload = "SAM_ACCOUNT_ID";
constexpr float kListPaneUnits = 300.0F;

struct Row {
    bool header = false;
    std::string group;               // header: group id ("" for Ungrouped)
    const core::Group* group_ptr = nullptr;
    std::size_t account = 0;         // account row: index into state.vault.accounts
    int count = 0;                   // header: accounts in the group that pass the filter
    std::vector<std::size_t> members;
    ImGuiID key = 0;
};

ImGuiID row_key(ImGuiSelectionBasicStorage* storage, int index) {
    const auto* rows = static_cast<const std::vector<Row>*>(storage->UserData);
    return (*rows)[static_cast<std::size_t>(index)].key;
}

std::vector<Row> build_rows(const app::AppState& state, const std::vector<std::size_t>& visible) {
    std::vector<Row> rows;
    std::unordered_set<std::string> known;
    for (const core::Group& g : state.vault.groups) known.insert(g.id);
    std::unordered_map<std::string, std::vector<std::size_t>> buckets;
    std::vector<std::size_t> ungrouped;
    for (std::size_t idx : visible) {
        const core::Account& a = state.vault.accounts[idx];
        if (!a.group_id.empty() && known.count(a.group_id) != 0) {
            buckets[a.group_id].push_back(idx);
        } else {
            ungrouped.push_back(idx);
        }
    }
    const bool filtered = filters_active(state);
    auto add_bucket = [&](const std::string& key, const core::Group* group, const std::vector<std::size_t>& members) {
        if (filtered && members.empty()) return;
        Row h;
        h.header = true;
        h.group = key;
        h.group_ptr = group;
        h.count = static_cast<int>(members.size());
        h.members = members;
        h.key = group_key("group:" + key);
        rows.push_back(std::move(h));
        if (group_collapsed(state, key)) return;
        for (std::size_t idx : members) {
            Row r;
            r.account = idx;
            r.group = key;
            r.key = account_key(state.vault.accounts[idx].id);
            rows.push_back(std::move(r));
        }
    };
    if (state.vault.groups.empty()) {
        for (std::size_t idx : visible) {
            Row r;
            r.account = idx;
            r.key = account_key(state.vault.accounts[idx].id);
            rows.push_back(std::move(r));
        }
        return rows;
    }
    static const std::vector<std::size_t> kNone;
    for (const core::Group& g : state.vault.groups) {
        const auto it = buckets.find(g.id);
        add_bucket(g.id, &g, it != buckets.end() ? it->second : kNone);
    }
    add_bucket(std::string(), nullptr, ungrouped);
    return rows;
}

float line_height(facet::TextStyle style) {
    return facet::MeasureText(style, "Ag").y;
}

// The trailing status marks of an account row (the kit's glyphs with their tooltips, the ban
// first), right-aligned at `right` on the row; their width is measured in the previous frame and
// kept in the window storage under the row key.
void row_marks(app::AppState& state, const core::Account& a, ImGuiID key, ImVec2 rmin, float right,
               float row_units, std::int64_t now) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const float w = store->GetFloat(key, 0.0F);
    ImGui::SetCursorScreenPos(ImVec2(right - w, rmin.y));
    {
        kit::RowCell cell({.min_height = row_units, .gap = facet::Gap::Tight});
        if (state.refreshing_ids.count(a.id) != 0) {
            facet::Spinner("##refreshing", {.size = facet::ControlSize::Small});
            facet::ItemTooltip("Refreshing");
            cell.next();
        }
        if (kit::row_markers(state, a, now, {.ban = true}) == 0 && state.refreshing_ids.count(a.id) == 0) {
            ImGui::Dummy(ImVec2(0.0F, 0.0F));
        }
    }
    store->SetFloat(key, ImGui::GetItemRectSize().x);
}

// Why a leading trust dot: the user's own label reads as part of the identity, apart from the
// red and amber status marks at the end, so a red trust dot is never mistaken for a ban.
void account_row(app::AppState& state, core::Account& a, const Row& row, ImVec2 rmin, ImVec2 rmax, bool indented,
                 std::int64_t now) {
    const facet::Theme& t = facet::T();
    const float pad = facet::Px(t.spacing.sm);
    const float row_units = (rmax.y - rmin.y) / facet::Scale();
    const float x = rmin.x + pad + (indented ? facet::Px(t.list.indent) : 0.0F);
    const float right = rmax.x - pad;

    row_marks(state, a, row.key, rmin, right, row_units, now);
    const float marks_w = ImGui::GetStateStorage()->GetFloat(row.key, 0.0F);
    const float text_right = right - (marks_w > 0.0F ? marks_w + pad : 0.0F);

    const bool hide_login = state.settings.list_view.hide_account_name;
    std::string name = a.web.persona_name;
    if (name.empty()) name = hide_login ? std::string("Account") : kit::login_text(state, a);
    const std::string login =
        (!a.web.persona_name.empty() && !hide_login && !kit::login_hidden(state, a)) ? kit::login_text(state, a)
                                                                                    : std::string();

    const float dot = facet::Px(t.metrics.statusDotSize);
    const float avatar = facet::Px(26.0F);
    const float lead = dot + pad + avatar + pad;
    const float avail = std::max(0.0F, text_right - x - lead);
    const float name_w = std::min(facet::MeasureText(facet::TextStyle::Body, name.c_str()).x + 1.0F,
                                  login.empty() ? avail : avail * 0.65F);
    const float login_w = avail - name_w - pad;

    ImGui::SetCursorScreenPos(ImVec2(x, rmin.y));
    kit::RowCell cell({.min_height = row_units});
    if (a.trust != core::TrustLabel::Unset) {
        kit::trust_dot(a.trust, row_units);
    } else {
        ImGui::Dummy(ImVec2(dot, dot));
    }
    cell.next();
    kit::avatar(state, a, {.diameter = 26.0F});
    cell.next();
    facet::TruncatedText(name.c_str(), {.width = std::max(1.0F, name_w) / facet::Scale()});
    if (!login.empty() && login_w > facet::Px(24.0F)) {
        cell.next();
        facet::TruncatedText(login.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true,
                                             .width = login_w / facet::Scale()});
    }
}

void header_row(const app::AppState& state, const Row& row, ImVec2 rmin, ImVec2 rmax) {
    const facet::Theme& t = facet::T();
    const float center_y = (rmin.y + rmax.y) * 0.5F;
    const float pad = facet::Px(t.spacing.sm);
    const float row_units = (rmax.y - rmin.y) / facet::Scale();
    float x = rmin.x + pad;
    const bool collapsed = group_collapsed(state, row.group);
    const float icon = line_height(facet::TextStyle::Body);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    facet::DrawIcon(dl, facet::Icon(collapsed ? "chevron-right" : "chevron-down"), ImVec2(x, center_y - icon * 0.5F),
                    icon, facet::ToU32(t.colors.labelSecondary));
    x += icon + pad * 0.5F;
    if (row.group_ptr != nullptr) {
        const float dot = facet::Px(t.metrics.statusDotSize);
        facet::FillRoundRect(dl, ImVec2(x, center_y - dot * 0.5F), ImVec2(x + dot, center_y + dot * 0.5F),
                             kit::data_color(row.group_ptr->color_rgba), dot);
        x += dot + pad;
    } else {
        facet::DrawIcon(dl, icons::group, ImVec2(x, center_y - icon * 0.5F), icon, facet::ToU32(t.colors.labelSecondary));
        x += icon + pad * 0.5F;
    }
    // The count as a quiet badge at the end; its width is measured in the previous frame.
    const std::string count = kit::format_count(row.count);
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID wkey = row.key ^ 0x9E3779B9u;
    const float badge_w = store->GetFloat(wkey, facet::Px(24.0F));
    const float right = rmax.x - pad;
    const char* name = row.group_ptr != nullptr ? row.group_ptr->name.c_str() : "Ungrouped";
    ImGui::SetCursorScreenPos(ImVec2(x, rmin.y));
    {
        kit::RowCell cell({.min_height = row_units});
        facet::TruncatedText(name, {.style = facet::TextStyle::Headline,
                                    .width = std::max(1.0F, right - badge_w - pad - x) / facet::Scale()});
    }
    ImGui::SetCursorScreenPos(ImVec2(right - badge_w, rmin.y));
    {
        kit::RowCell cell({.min_height = row_units});
        facet::Badge(count.c_str());
    }
    facet::ItemTooltip(row.count == 1 ? "1 account" : "%d accounts", row.count);
    store->SetFloat(wkey, ImGui::GetItemRectSize().x);
}

void drag_source(app::AppState& state, const core::Account& a) {
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) return;
    ImGui::SetDragDropPayload(kDragPayload, a.id.data(), a.id.size());
    const bool several = multi_selection(state) && state.selected_account_ids.count(a.id) != 0;
    if (several) {
        facet::Text(facet::TextStyle::Body, "Move %zu accounts", state.selected_account_ids.size());
    } else {
        kit::account_row(state, a, {.layout = kit::AccountRowOptions::Layout::OneLine});
    }
    ImGui::EndDragDropSource();
}

void drop_target(app::AppState& state, const Row& row) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload)) {
        if (payload->Data != nullptr && payload->DataSize > 0) {
            const std::string id(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize));
            std::vector<std::string> ids;
            if (multi_selection(state) && state.selected_account_ids.count(id) != 0) {
                ids.assign(state.selected_account_ids.begin(), state.selected_account_ids.end());
            } else {
                ids.push_back(id);
            }
            move_to_group(state, ids, row.group);
        }
    }
    ImGui::EndDragDropTarget();
}

// Why a virtual list with header rows and not Facet's tree view: tree leaves draw a label only,
// and the rows need the avatar and the status marks; a virtual list hands each row to the caller
// and still gives the selection pill, multi-select and keyboard movement.
void draw_rows(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    std::vector<Row> rows = build_rows(state, visible);
    std::unordered_map<ImGuiID, std::size_t> account_of;
    std::unordered_set<ImGuiID> header_keys;
    for (const Row& r : rows) {
        if (r.header) header_keys.insert(r.key);
    }
    for (std::size_t i = 0; i < state.vault.accounts.size(); ++i) account_of[account_key(state.vault.accounts[i].id)] = i;

    load_selection(s.list_selection, state);
    s.list_selection.UserData = &rows;
    s.list_selection.AdapterIndexToStorageId = &row_key;

    // An account selected from elsewhere (a toast, the command palette, a scene) is scrolled
    // into view once; a click in the list needs no scrolling.
    if (state.selected_account_id != s.scrolled_to) {
        const ImGuiID key = account_key(state.selected_account_id);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].header && rows[i].key == key) {
                facet::SetNextVirtualListScrollTo(static_cast<int>(i));
                s.scrolled_to = state.selected_account_id;
            }
        }
    }
    int selected_drawn = 0;

    const std::int64_t now = now_unix();
    const bool grouped = !state.vault.groups.empty();
    int toggle_row = -1;
    if (facet::BeginVirtualList("##accounts", static_cast<int>(rows.size()), &s.list_selection,
                                {.rowHeight = facet::T().list.rowHeightLarge})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int i = range.first; i < range.last; ++i) {
                const Row& row = rows[static_cast<std::size_t>(i)];
                const bool activated = facet::VirtualListRow(i);
                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                if (row.header) {
                    ImGui::PushID(static_cast<int>(row.key));
                    if (activated && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toggle_row = i;
                    drop_target(state, row);
                    if (row.group_ptr != nullptr && facet::BeginContextMenu("##group-menu")) {
                        group_menu_items(state, *row.group_ptr, row.members);
                        facet::EndContextMenu();
                    }
                    header_row(state, row, rmin, rmax);
                    ImGui::PopID();
                    continue;
                }
                core::Account& a = state.vault.accounts[row.account];
                // Only rows inside the view count; the list also hands out the focused row.
                if (state.selected_account_ids.count(a.id) != 0) {
                    const float top = ImGui::GetWindowPos().y;
                    const float bottom = top + ImGui::GetWindowHeight();
                    if (rmin.y >= top - 1.0F && rmax.y <= bottom + 1.0F) ++selected_drawn;
                }
                ImGui::PushID(a.id.c_str());
                drag_source(state, a);
#ifdef SAM_UI_CAPTURE
                if (a.id == state.selected_account_id) capture_open_next_context_menu();
#endif
                if (facet::BeginContextMenu("##account-menu")) {
                    account_menu_items(state, a);
                    facet::EndContextMenu();
                }
                account_row(state, a, row, rmin, rmax, grouped, now);
                ImGui::PopID();
            }
        }
        facet::EndVirtualList();
    }
    s.list_selection.UserData = nullptr;
    s.selected_in_view = selected_drawn;

    // Read the selection back. A header row in it is never a selection: a plain click on a
    // header (the only thing selected afterwards) folds the group and keeps the accounts that
    // were selected; headers swept up by Ctrl+A or a Shift range are dropped.
    std::vector<std::string> ids;
    ImGuiID header_hit = 0;
    void* it = nullptr;
    ImGuiID id = 0;
    while (s.list_selection.GetNextSelectedItem(&it, &id)) {
        if (header_keys.count(id) != 0) {
            header_hit = id;
            continue;
        }
        if (const auto found = account_of.find(id); found != account_of.end()) {
            ids.push_back(state.vault.accounts[found->second].id);
        }
    }
    const bool header_only = header_hit != 0 && ids.empty();
    if (header_only) {
        const bool clicked = ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (clicked && toggle_row < 0) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].key == header_hit) toggle_row = static_cast<int>(i);
            }
        }
    } else if (store_selection(state, ids)) {
        s.detail_open = true;
    }
    if (toggle_row >= 0) {
        const Row& row = rows[static_cast<std::size_t>(toggle_row)];
        set_group_collapsed(state, row.group, !group_collapsed(state, row.group));
    }
}

}  // namespace

// Why list and detail side by side: the job is to find one account among hundreds and act on
// it, so the list stays in view while the detail changes, and a narrow window shows one at a time.
void draw_list_view(app::AppState& state, const std::vector<std::size_t>& visible) {
    ScreenState& s = screen();
    facet::BeginListDetail("##accounts-list", &s.detail_open, {.listWidth = kListPaneUnits});
    if (facet::BeginListPane()) {
        draw_rows(state, visible);
        facet::EndListPane();
    }
    if (facet::BeginDetailPane()) {
        begin_padded("##detail-body");
        draw_detail(state, visible);
        end_padded();
        facet::EndDetailPane();
    }
    facet::EndListDetail();
}

}  // namespace sam::ui::screens::accounts
