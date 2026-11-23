// The sidebar's activity item and update notice, at the bottom of the navigation and shown only
// while there is something to say.

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/navigation.h>
#include <facet/widgets/overlays.h>

#include "core/version.hpp"
#include "ui/kit/row_line.hpp"
#include "ui/shell/shell.hpp"
#include "ui/util.hpp"

namespace sam::ui::shell {

namespace {

std::string progress_text(const char* what, int done, int total) {
    char buf[96];
    if (total > 0) {
        std::snprintf(buf, sizeof(buf), "%s %d of %d", what, std::min(done, total), total);
    } else {
        std::snprintf(buf, sizeof(buf), "%s", what);
    }
    return buf;
}

float fraction(const Job& j) {
    return j.total > 0 ? std::clamp(static_cast<float>(j.done) / static_cast<float>(j.total), 0.0F, 1.0F)
                       : facet::kIndeterminate;
}

// The list of every running job, in the popover of the activity item.
void job_list(const std::vector<Job>& jobs) {
    facet::Text(facet::TextStyle::Headline, "Running in the background");
    ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.xs)));
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        facet::ProgressBar(jobs[i].text.c_str(), fraction(jobs[i]),
                           {.size = facet::ControlSize::Small, .width = 260.0F});
        ImGui::PopID();
    }
}

}  // namespace

// Reads the progress counters the app already keeps; idle counters add nothing.
std::vector<Job> running_jobs(const app::AppState& app) {
    std::vector<Job> jobs;
    const int total = app.refresh_all_total.load(std::memory_order_relaxed);
    const int done = app.refresh_all_done.load(std::memory_order_relaxed);
    const bool refreshing_all = total > 0 && done < total;
    if (refreshing_all) jobs.push_back({progress_text("Refreshing accounts", done, total), done, total});

    const bool validate_busy = app.gc_validate.active && !app.gc_validate.feed_refresh_all;
    const int gc_done = app.gc_autopull.received + (validate_busy ? app.gc_validate.done : 0);
    const int gc_total = app.gc_autopull.total + (validate_busy ? static_cast<int>(app.gc_validate.queue.size()) : 0);
    if (gc_total > 0 && gc_done < gc_total) {
        jobs.push_back({progress_text("Pulling CS2 data", gc_done, gc_total), gc_done, gc_total});
    } else if (app.gc_autopull.active || validate_busy) {
        jobs.push_back({progress_text("Pulling CS2 data", 0, 0), 0, 0});
    }

    const int sp_total = app.spend_total.load(std::memory_order_relaxed);
    const int sp_done = app.spend_done.load(std::memory_order_relaxed);
    if (sp_total > 0 && sp_done < sp_total) {
        jobs.push_back({progress_text("Fetching funds", sp_done, sp_total), sp_done, sp_total});
    } else if (app.spend_bulk_running.load(std::memory_order_acquire)) {
        jobs.push_back({progress_text("Fetching funds", 0, 0), 0, 0});
    }

    const int c_total = app.conf_refresh_all_total.load(std::memory_order_relaxed);
    const int c_done = app.conf_refresh_all_done.load(std::memory_order_relaxed);
    if (c_total > 0 && c_done < c_total) {
        jobs.push_back({progress_text("Refreshing confirmations", c_done, c_total), c_done, c_total});
    }

    if (app.cleaner_busy.load()) jobs.push_back({"Cleaning Steam", 0, 0});

    if (!refreshing_all && !app.refreshing_ids.empty()) {
        const int n = static_cast<int>(app.refreshing_ids.size());
        char buf[64];
        std::snprintf(buf, sizeof(buf), n == 1 ? "Refreshing 1 account" : "Refreshing %d accounts", n);
        jobs.push_back({buf, 0, 0});
    }
    return jobs;
}

// Why a quiet block and not a button: it reports, it does not act; hovering or clicking it
// shows every job, so a long Refresh all never hides a cleaner run behind it.
bool draw_activity(app::AppState& app, bool rail) {
    State& st = state();
    const std::vector<Job> jobs = running_jobs(app);
    if (jobs.empty()) {
        st.activity_open = false;
        return false;
    }
    const facet::Theme& t = facet::T();
    const Job& first = jobs.front();
    ImGui::PushID("activity");
    if (rail) {
        const float d = facet::ControlHeight(facet::ControlSize::Small);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - d) * 0.5F));
        facet::ProgressRing("##activity-ring", fraction(first), {.diameter = d / facet::Scale()});
        if (!st.activity_open) {
            std::string tip = first.text;
            if (jobs.size() > 1) tip += jobs.size() == 2 ? " and 1 more job" : " and " + std::to_string(jobs.size() - 1) + " more jobs";
            facet::ItemTooltip("%s", tip.c_str());
        }
    } else {
        ImGui::BeginGroup();
        std::string text = first.text;
        if (jobs.size() > 1) text += ", and " + std::to_string(jobs.size() - 1) + " more";
        {
            kit::detail::RowLine line("##activity-line", facet::MeasureText(facet::TextStyle::Footnote, "Ag").y);
            facet::Spinner("##activity-spinner", {.size = facet::ControlSize::Small});
            line.next();
            facet::TruncatedText(text.c_str(), {.style = facet::TextStyle::Footnote});
        }
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.xs) - ImGui::GetStyle().ItemSpacing.y));
        facet::ProgressBar("##activity-progress", fraction(first), {.size = facet::ControlSize::Small});
        ImGui::EndGroup();
    }
    if (ImGui::IsItemClicked()) st.activity_open = !st.activity_open;
    if (facet::BeginPopover("##activity-jobs", &st.activity_open,
                            {.placement = facet::PopoverPlacement::End, .openOnHover = true})) {
        job_list(jobs);
        facet::EndPopover();
    }
    ImGui::PopID();
    return true;
}

std::string pending_update(app::AppState& app) {
    std::lock_guard lk(app.update_mutex);
    if (app.update_result && app.update_result->latest_tag != app.settings.version_check_skip_until) {
        return app.update_result->latest_tag;
    }
    return {};
}

bool draw_update_notice(app::AppState& app, bool rail) {
    const std::string tag = pending_update(app);
    if (tag.empty()) return false;
    State& st = state();
    char label[96];
    std::snprintf(label, sizeof(label), "Update to %s###update-notice", tag.c_str());
    if (rail) {
        const float control = facet::ControlHeight(facet::ControlSize::Inherit);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - control) * 0.5F));
        if (facet::IconButton(label, facet::Icon("download"))) st.update_open = true;
        // The dot on the icon's corner says "new", as a badge does on the other rail items.
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 after = ImGui::GetCursorScreenPos();
        const float dot = facet::Px(facet::T().metrics.statusDotSize);
        ImGui::SetCursorScreenPos(ImVec2(max.x - dot * 1.5F, min.y + dot * 0.25F));
        facet::StatusDot("##update-dot", {.tone = facet::Tone::Accent});
        ImGui::SetCursorScreenPos(after);
    } else {
        // A navigation row like the others around it: left aligned, never selected.
        if (facet::SidebarItem(label, facet::Icon("download"))) st.update_open = true;
        facet::ItemTooltip("Version %s is available. You have %s.", tag.c_str(), std::string(sam::kVersion).c_str());
    }
    return true;
}

void open_repository() {
    open_url(std::string(sam::kRepoUrl));
}

}  // namespace sam::ui::shell
