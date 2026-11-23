// Settings > Sign-in and launch: how an account is signed in, what is changed on the account
// being launched, and the CS2 config and launch options it gets.

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>

#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/selection.h>

#include "app/app_paths.hpp"
#include "app/job_pump.hpp"
#include "core/cs2_config/video_config.hpp"
#include "core/cs2_config/workshop_block.hpp"
#include "core/log.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/settings/categories.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::settings {

namespace {

void draw_sign_in(Page& p) {
    section(p, "Sign-in");
    if (begin_row(p, "Sign-in method",
                  {.description = "The driver types into Steam's login window. Token injection skips the window, "
                                  "but needs a stored password once to create the token.",
                   .keywords = "sign in login method driver token injection jwt connectcache",
                   .modified = p.s.sign_in_method != p.d.sign_in_method,
                   .reset = true})) {
        static const char* const kMethods[] = {"Sign-in driver", "Token injection"};
        int index = static_cast<int>(p.s.sign_in_method);
        if (facet::SegmentedControl("##value", &index, kMethods)) {
            p.s.sign_in_method = static_cast<app::SignInMethod>(index);
        }
        if (end_row()) p.s.sign_in_method = p.d.sign_in_method;
    }
    const bool token = p.s.sign_in_method == app::SignInMethod::TokenInject;
    if (begin_dependent(p, "##remember", !token)) {
        toggle_row(p, "Remember the password in Steam", &p.s.remember_password_on_login,
                   p.d.remember_password_on_login,
                   "Ticks Steam's Remember me box so the session is kept. Token injection always keeps one.",
                   "remember me password session saved login");
        end_dependent(p);
    }
}

void draw_launched_account(Page& p) {
    section(p, "On the launched account",
            "Written while Steam is closed for the switch. These are account-wide Steam settings and are not "
            "put back afterwards.");
    toggle_row(p, "Turn off Steam Cloud", &p.s.disable_cloud_on_login, p.d.disable_cloud_on_login,
               "Turns Steam Cloud off for every account you launch.", "cloud sync saves disable");
    toggle_row(p, "Turn off new release news", &p.s.disable_news_on_login, p.d.disable_news_on_login,
               "Turns off the new release notifications for every account you launch.", "news release popup disable");
    toggle_row(p, "Turn off Remote Play", &p.s.disable_remote_play_on_login, p.d.disable_remote_play_on_login,
               "Turns off Remote Play for every account you launch.", "remote play streaming disable");
    toggle_row(p, "Sign in as invisible", &p.s.login_invisible, p.d.login_invisible,
               "Sets the friends status to Invisible before sign-in, so switching never shows an account online.",
               "invisible offline status friends online");
    toggle_row(p, "Block CS2 workshop downloads", &p.s.disable_workshop_on_login, p.d.disable_workshop_on_login,
               "Stops subscribed workshop maps from downloading, so CS2 starts at once. Subscriptions are kept "
               "and come back when this is off.",
               "workshop maps download subscriptions block");
    if (begin_row(p, "Workshop subscriptions",
                  {.description = "Gives every account on this PC its workshop subscriptions back now.",
                   .keywords = "workshop restore enable subscriptions unblock"})) {
        if (fit_button("Restore workshop downloads")) {
            const auto r = cs2_config::clear_all_workshop_blocks();
            SAM_LOG_INFO("settings: clear workshop blocks -> ok={} ({})", r.ok, r.message);
            kit::show_toast(p.state,
                            r.ok ? std::string("Workshop downloads restored for every account on this PC.")
                                 : "Workshop downloads could not be restored: " + r.message,
                            {.tone = r.ok ? widgets::ToastTone::Success : widgets::ToastTone::Destructive});
        }
        end_row();
    }
}

struct UserdataState {
    bool importing = false;
    std::string error;
    std::string noted_for = "\x01";  // the label the summary below was computed for
    std::string games;               // "N game folders: 730, 570, ..."
    bool no_config_beside = false;   // the picked folder has no config folder: maybe userdata itself
};

UserdataState& userdata_state() {
    static UserdataState s;
    return s;
}

enum class TemplateAction { None, Choose, Remove };

// The current template as text, then its Choose and Remove buttons.
TemplateAction template_buttons(const std::string& label, const char* choose, const char* remove) {
    facet::StyledText(label.empty() ? "No template chosen" : label.c_str(),
                      {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
    TemplateAction action = TemplateAction::None;
    if (fit_button(choose)) action = TemplateAction::Choose;
    if (!label.empty()) {
        ImGui::SameLine();
        if (fit_button(remove)) action = TemplateAction::Remove;
    }
    return action;
}

void import_video_txt(Page& p) {
    platform::file_dialog::Options opts;
    opts.parent = p.state.main_hwnd;
    opts.title = L"Select CS2 video.txt";
    opts.filters = {{L"Video config (*.txt)", L"*.txt"}, {L"All files (*.*)", L"*.*"}};
    const auto res = platform::file_dialog::open_file(opts);
    if (!res.ok) return;
    std::error_code ec;
    std::filesystem::copy_file(res.path, app::cs2_video_template_path(),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        SAM_LOG_ERROR("cs2 video template import failed: {}", ec.message());
        kit::show_toast(p.state, "The video.txt could not be copied: " + ec.message(),
                        {.tone = widgets::ToastTone::Destructive});
        return;
    }
    p.s.cs2_video.source_label = res.path.string();
    commit_now(p.state);
}

void import_730(Page& p) {
    platform::file_dialog::Options opts;
    opts.parent = p.state.main_hwnd;
    opts.title = L"Select CS2 730 folder";
    const auto res = platform::file_dialog::pick_folder(opts);
    if (!res.ok) return;
    const auto imp = cs2_config::import_730_template(res.path, app::cs2_730_template_dir());
    if (!imp.ok) {
        SAM_LOG_ERROR("cs2 730 template import failed: {}", imp.message);
        kit::show_toast(p.state, "The 730 folder could not be copied: " + imp.message,
                        {.tone = widgets::ToastTone::Destructive});
        return;
    }
    p.s.cs2_video.folder_source_label = res.path.string();
    commit_now(p.state);
}

void import_userdata(Page& p) {
    platform::file_dialog::Options opts;
    opts.parent = p.state.main_hwnd;
    opts.title = L"Select Steam userdata account folder";
    const auto res = platform::file_dialog::pick_folder(opts);
    if (!res.ok) return;
    UserdataState& u = userdata_state();
    u.importing = true;
    u.error.clear();
    app::AppState* state = &p.state;
    app::job_pump::submit([state, src = res.path] {
        const auto imp = cs2_config::import_userdata_template(src, app::userdata_template_dir());
        state->post_ui_callback([state, src, imp] {
            UserdataState& us = userdata_state();
            us.importing = false;
            if (!imp.ok) {
                us.error = imp.message;
                SAM_LOG_ERROR("userdata template import failed: {}", imp.message);
                return;
            }
            state->settings.cs2_video.userdata_source_label = src.string();
            commit_now(*state);
            us.noted_for.clear();
        });
    });
}

// What the userdata snapshot holds, recomputed when the template label changes.
void note_userdata(const std::string& label) {
    UserdataState& u = userdata_state();
    if (u.importing || u.noted_for == label) return;
    u.noted_for = label;
    u.games.clear();
    u.no_config_beside = false;
    if (label.empty()) return;
    std::error_code ec;
    std::vector<std::string> ids;
    for (auto it = std::filesystem::directory_iterator(app::userdata_template_dir(), ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) ids.push_back(to_utf8(it->path().filename().wstring()));
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        u.games += (i == 0 ? std::to_string(ids.size()) + " game folders: " : std::string{", "}) + ids[i];
    }
    const std::filesystem::path src(label);
    if (std::filesystem::is_directory(src, ec)) u.no_config_beside = !std::filesystem::is_directory(src / "config", ec);
}

void draw_template(Page& p) {
    auto& v = p.s.cs2_video;
    switch (v.mode) {
        case app::CS2ConfigMode::None: return;
        case app::CS2ConfigMode::VideoTxt:
            if (begin_row(p, "video.txt template",
                          {.description = "Copied into the app data folder and given to every account you launch.",
                           .keywords = "video txt template cs2 config graphics", .immediate = true})) {
                const auto action = template_buttons(v.source_label, "Choose video.txt", "Remove template##video");
                if (action == TemplateAction::Choose) import_video_txt(p);
                if (action == TemplateAction::Remove) {
                    std::error_code ec;
                    std::filesystem::remove(app::cs2_video_template_path(), ec);
                    v.source_label.clear();
                    commit_now(p.state);
                }
                end_row();
            }
            return;
        case app::CS2ConfigMode::Folder730:
            if (begin_row(p, "730 folder template",
                          {.description = "A copy is taken now and merged into the account's 730 folder at sign-in. "
                                          "Choose again to update it.",
                           .keywords = "730 folder template cs2 config cfg", .immediate = true})) {
                const auto action = template_buttons(v.folder_source_label, "Choose 730 folder", "Remove template##730");
                if (action == TemplateAction::Choose) import_730(p);
                if (action == TemplateAction::Remove) {
                    std::error_code ec;
                    std::filesystem::remove_all(app::cs2_730_template_dir(), ec);
                    v.folder_source_label.clear();
                    commit_now(p.state);
                }
                std::error_code ec;
                if (!v.folder_source_label.empty() &&
                    !std::filesystem::is_directory(app::cs2_730_template_dir() / "local" / "cfg", ec)) {
                    facet::InlineAlert("##no-cfg", nullptr,
                                       "There is no local/cfg folder inside. Is this really a 730 folder?",
                                       {.tone = facet::Tone::Warning});
                }
                end_row();
            }
            return;
        case app::CS2ConfigMode::UserdataFolder: {
            UserdataState& u = userdata_state();
            note_userdata(v.userdata_source_label);
            if (begin_row(p, "userdata folder template",
                          {.description = "Pick a userdata\\<id> folder. Every game folder in it is merged into the "
                                          "launched account's userdata. Steam's own settings (localconfig.vdf) are "
                                          "never copied.",
                           .keywords = "userdata folder template games config", .immediate = true})) {
                ImGui::BeginDisabled(u.importing);
                const auto action =
                    template_buttons(v.userdata_source_label, "Choose userdata folder", "Remove template##userdata");
                if (action == TemplateAction::Choose) import_userdata(p);
                if (action == TemplateAction::Remove) {
                    std::error_code ec;
                    std::filesystem::remove_all(app::userdata_template_dir(), ec);
                    v.userdata_source_label.clear();
                    u.error.clear();
                    commit_now(p.state);
                }
                ImGui::EndDisabled();
                if (u.importing) {
                    facet::ProgressBar("Copying game folders", facet::kIndeterminate);
                } else if (!u.error.empty()) {
                    facet::InlineAlert("Import failed##userdata", nullptr, u.error.c_str(),
                                       {.tone = facet::Tone::Destructive});
                } else if (!u.games.empty()) {
                    facet::StyledText(("The template holds " + u.games).c_str(),
                                      {.style = facet::TextStyle::Footnote, .secondary = true, .wrap = true});
                }
                if (u.no_config_beside) {
                    facet::InlineAlert("##no-config", nullptr,
                                       "There is no config folder beside these. Did you pick userdata itself instead "
                                       "of userdata\\<id>?",
                                       {.tone = facet::Tone::Warning});
                }
                end_row();
            }
            return;
        }
    }
}

// Why the template sits in a group under the mode: it only exists for the chosen mode.
void draw_cs2_config(Page& p) {
    section(p, "CS2 config at sign-in");
    auto& v = p.s.cs2_video;
    if (begin_row(p, "Copy into the account",
                  {.description = "What is copied into the launched account's userdata. The first three only "
                                  "touch CS2; the last covers every game in the picked folder.",
                   .keywords = "cs2 config video txt 730 userdata copy template graphics",
                   .modified = v.mode != p.d.cs2_video.mode,
                   .reset = true})) {
        static const char* const kModes[] = {"Nothing", "video.txt only", "Whole 730 folder",
                                             "Every game folder (userdata)"};
        int index = static_cast<int>(v.mode);
        if (facet::Dropdown("##value", &index, kModes, {.width = cap(280.0F)})) {
            v.mode = static_cast<app::CS2ConfigMode>(index);
        }
        if (end_row()) v.mode = p.d.cs2_video.mode;
    }
    if (v.mode != app::CS2ConfigMode::None) {
        if (begin_dependent(p, "##cs2-template", true)) {
            draw_template(p);
            end_dependent(p);
        }
    }
    if (begin_row(p, "CS2 launch options",
                  {.description = "Written into the launched account's CS2 launch options. Empty keeps what Steam "
                                  "has.",
                   .keywords = "launch options novid fps_max command line arguments",
                   .modified = v.launch_options != p.d.cs2_video.launch_options,
                   .reset = true})) {
        facet::TextField("##value", &v.launch_options, {.placeholder = "-novid +fps_max 400", .maxLength = 255});
        if (end_row()) v.launch_options = p.d.cs2_video.launch_options;
    }
}

}  // namespace

// Why this order: the sign-in method decides how every launch works; what is changed on the
// launched account and its CS2 config follow in the order a launch applies them.
void draw_sign_in_launch(Page& p) {
    draw_sign_in(p);
    draw_launched_account(p);
    draw_cs2_config(p);
}

}  // namespace sam::ui::screens::settings
