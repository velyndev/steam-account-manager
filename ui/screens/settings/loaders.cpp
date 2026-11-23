// Settings > CS2 loaders: the gamesense and luminary loader executables that accounts set to
// launch with them run once CS2 starts.

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <imgui.h>

#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>

#include "app/gamesense_loader.hpp"
#include "app/luminary_loader.hpp"
#include "core/log.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

struct Loader {
    const char* label;        // row label
    const char* description;  // row description
    const char* keywords;
    const wchar_t* dialog_title;
    std::optional<std::filesystem::path> (*path)();
    bool (*install)(const std::filesystem::path& src, std::string* err);
};

// One row per loader: its state, then choose or replace and remove. The error of the last
// install stays under the row until the next attempt.
void loader_row(Page& p, const Loader& l, std::string* error) {
    if (!begin_row(p, l.label, {.description = l.description, .keywords = l.keywords})) return;
    const auto installed = l.path();
    {
        kit::RowCell cell;
        if (fit_button(installed ? "Replace loader" : "Choose loader")) {
            platform::file_dialog::Options opts;
            opts.parent = p.state.main_hwnd;
            opts.title = l.dialog_title;
            opts.filters = {{L"Executable (*.exe)", L"*.exe"}, {L"All files (*.*)", L"*.*"}};
            const auto res = platform::file_dialog::open_file(opts);
            if (res.ok) {
                error->clear();
                if (!l.install(res.path, error)) SAM_LOG_ERROR("loader install failed: {}", *error);
            }
        }
        cell.next();
        if (installed) {
            facet::StatusPill(installed->filename().string().c_str(),
                              {.icon = facet::Icon("file"), .size = facet::ControlSize::Small});
            cell.next();
            if (fit_button("Remove loader", {.variant = facet::ButtonVariant::Plain})) {
                std::error_code ec;
                std::filesystem::remove(*installed, ec);
            }
        } else {
            facet::StyledText("No loader chosen", {.style = facet::TextStyle::Footnote, .secondary = true});
        }
    }
    if (!error->empty()) {
        facet::InlineAlert("The loader was not installed##err", nullptr, error->c_str(), {.tone = facet::Tone::Destructive});
    }
    end_row();
}

}  // namespace

void draw_loaders(Page& p) {
    ban_risk_banner(p, "Accounts set to launch CS2 with a loader run these third-party programs once CS2 starts. "
                       "That can put those accounts at risk, which is why safe mode hides them.");
    section(p, "Loaders", "Copied into the app data folder when chosen.");
    static std::string gamesense_error;
    static std::string luminary_error;
    loader_row(p,
               {"Gamesense", "Run by accounts set to Launch CS2 + gamesense once CS2 starts.",
                "gamesense loader exe external", L"Choose gamesense loader", &app::gamesense_loader_path,
                &app::install_gamesense_loader},
               &gamesense_error);
    loader_row(p,
               {"Luminary", "Run by accounts set to Launch CS2 + luminary once CS2 starts.",
                "luminary loader exe external", L"Choose luminary loader", &app::luminary_loader_path,
                &app::install_luminary_loader},
               &luminary_error);
}

}  // namespace sam::ui::screens::settings
