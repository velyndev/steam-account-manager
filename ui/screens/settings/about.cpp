// Settings > About: the app, its version and repository, the update check, the license and the
// third-party notices.

#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include <facet/core/text.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>

#include "app/job_pump.hpp"
#include "core/update_check.hpp"
#include "core/version.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

enum class CheckState { Idle, Checking, UpToDate, Available, Failed };

struct AboutState {
    CheckState check = CheckState::Idle;
    std::string latest;  // the tag of the newer release
};

AboutState& about_state() {
    static AboutState s;
    return s;
}

// "owner/repo" from the repository URL, as the release check takes it.
std::string owner_repo() {
    std::string_view url = kRepoUrl;
    constexpr std::string_view kHost = "https://github.com/";
    if (url.substr(0, kHost.size()) == kHost) url.remove_prefix(kHost.size());
    return std::string(url);
}

// Runs the same release query as the check at launch, on the job pump, and keeps the answer for
// this screen. It does not open the update dialog of the shell.
void check_now(app::AppState& state) {
    about_state().check = CheckState::Checking;
    app::AppState* st = &state;
    app::job_pump::submit([st, repo = owner_repo()] {
        const auto r = core::update_check::fetch_latest_release(repo, kVersion);
        st->post_ui_callback([r] {
            AboutState& a = about_state();
            if (!r) {
                a.check = CheckState::Failed;
            } else if (r->newer_than_current) {
                a.check = CheckState::Available;
                a.latest = r->latest_tag;
            } else {
                a.check = CheckState::UpToDate;
            }
        });
    });
}

// Why rows: the app's facts sit in the same grouped card as every other section, so the page
// reads as one list; the name is the section heading.
void draw_app(Page& p) {
    section(p, "Steam Account Manager");
    if (begin_row(p, "Version", {.description = "The release you are running.", .keywords = "version release about"})) {
        const std::string version(kVersion);
        {
            kit::RowCell cell({.min_height = 28.0F});
            facet::Text(facet::TextStyle::Body, "%s", version.c_str());
        }
        end_row();
    }
    if (begin_row(p, "Source code", {.description = "The project page on GitHub, with releases and issues.",
                                     .keywords = "github source repository project"})) {
        const std::string repo(kRepoUrl);
        {
            kit::RowCell cell({.min_height = 28.0F});
            facet::Link("Open on GitHub", {.url = repo.c_str()});
        }
        end_row();
    }
    if (begin_row(p, "License", {.description = "Open source under the MIT License.",
                                 .keywords = "license mit open source copyright"})) {
        const std::string url = std::string(kRepoUrl) + "/blob/main/LICENSE";
        {
            kit::RowCell cell({.min_height = 28.0F});
            facet::Link("Read the license", {.url = url.c_str()});
        }
        end_row();
    }
    if (begin_row(p, "Third-party notices",
                  {.description = "The libraries this app is built with, and their licenses.",
                   .keywords = "third party notices licenses libraries credits open source"})) {
        const std::string url = std::string(kRepoUrl) + "/blob/main/THIRD_PARTY_NOTICES.md";
        {
            kit::RowCell cell({.min_height = 28.0F});
            facet::Link("Read the notices", {.url = url.c_str()});
        }
        end_row();
    }
}

void draw_updates(Page& p) {
    AboutState& a = about_state();
    section(p, "Updates");
    toggle_row(p, "Check for updates at launch", &p.s.check_updates_on_launch, p.d.check_updates_on_launch,
               "Asks GitHub for a newer release when the app starts. No account data is sent. Applies to every "
               "vault.",
               "update version release github new");
    if (begin_row(p, "Check now", {.description = "Asks GitHub for the newest release right away.",
                                   .keywords = "update check version release now"})) {
        if (fit_button("Check for updates", {.loading = a.check == CheckState::Checking})) check_now(p.state);
        ImGui::SameLine();
        switch (a.check) {
            case CheckState::Idle:
            case CheckState::Checking: break;
            case CheckState::UpToDate:
                facet::StatusPill("Up to date", {.tone = facet::Tone::Success, .icon = facet::Icon("success"),
                                                 .size = facet::ControlSize::Small});
                break;
            case CheckState::Failed:
                facet::StatusPill("Could not reach GitHub", {.tone = facet::Tone::Destructive,
                                                             .icon = facet::Icon("error"),
                                                             .size = facet::ControlSize::Small});
                break;
            case CheckState::Available: {
                const std::string text = a.latest + " is available";
                facet::StatusPill(text.c_str(), {.tone = facet::Tone::Accent, .icon = facet::Icon("download"),
                                                 .size = facet::ControlSize::Small});
                const std::string releases = std::string(kRepoUrl) + "/releases/latest";
                facet::Link("Open the release page", {.url = releases.c_str()});
                break;
            }
        }
        end_row();
    }
}

}  // namespace

void draw_about(Page& p) {
    draw_app(p);
    draw_updates(p);
}

}  // namespace sam::ui::screens::settings
