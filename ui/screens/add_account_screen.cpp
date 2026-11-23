#include "ui/screens/add_account_screen.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/media.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/selection.h>

#include "ui/icons.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/actions.hpp"
#include "ui/kit/page.hpp"
#include "ui/kit/transient.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/screens/add_account_detail.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens {

namespace add_account_detail {

std::string generate_ulid() {
    static std::atomic<std::uint64_t> counter{0};
    const auto n = counter.fetch_add(1);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx%08llx", static_cast<unsigned long long>(now_seconds()),
                  static_cast<unsigned long long>(n));
    return buf;
}

std::string trim_path(std::string s) {
    while (!s.empty() && (s.front() == '"' || s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() &&
           (s.back() == '"' || s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::vector<std::filesystem::path> scan_dir_for_extension(const std::filesystem::path& dir,
                                                          std::string_view ext_lower) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return out;
    for (const auto& e : it) {
        if (!e.is_regular_file(ec)) continue;
        std::string lower = e.path().filename().string();
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.size() >= ext_lower.size() &&
            lower.compare(lower.size() - ext_lower.size(), ext_lower.size(), ext_lower) == 0) {
            out.push_back(e.path());
        }
    }
    return out;
}

namespace {

struct ScreenState {
    bool chooser = true;
    int choice = 0;  // the highlighted card; FullLogin until the user picks another
    Method method = Method::FullLogin;
    const app::AppState* owner = nullptr;
    const app::AppState* seeded = nullptr;
};

ScreenState& screen() {
    static ScreenState s;
    return s;
}

struct MethodInfo {
    const char* title;
    const char* when;  // one line: when this is the right method
    facet::Icon icon;
};

// Ordered by how often each is used: a password sign-in and SDA files cover most accounts.
const MethodInfo& method_info(Method m) {
    static const MethodInfo kInfo[kMethodCount] = {
        {"Sign in with password",
         "You know the login and password. Steam asks for a Steam Guard code or an approval in its app.",
         facet::Icon("user")},
        {"Import maFiles", "You have Steam Desktop Authenticator .maFile files, one or a whole folder.",
         icons::authenticator},
        {"Take from this PC", "The account is signed in to Steam on this computer with its login remembered.",
         facet::Icon("monitor")},
        {"Paste an NFA token", "You have a token line for an account that has no password.", icons::token},
        {"Import info.dat", "You have an SDA info.dat export that lists several accounts.", facet::Icon("file")},
        {"Import a bundle", "You have a .samvault file exported from this app on another PC.", icons::vault},
    };
    return kInfo[static_cast<int>(m)];
}
// A new AppState (a capture scene) starts the screen fresh unless the scene seeded it.
void adopt(const app::AppState& state) {
    ScreenState& s = screen();
    if (s.owner == &state) return;
    if (s.seeded != &state) reset_screen();
    screen().owner = &state;
}

// WM_DROPFILES fills the queues from the window procedure; they move into the flows here so
// files dropped while another screen or method was showing are not lost. A bundle is only queued
// when no maFile or info.dat came with it, as with picked files.
void drain_drops(app::AppState& state) {
    std::vector<std::string> mafiles;
    std::vector<std::string> info_dat;
    std::string bundle;
    bool focus_mafile = false;
    bool focus_info_dat = false;
    {
        std::lock_guard lk(state.drop_mutex);
        mafiles.swap(state.pending_mafile_drops);
        info_dat.swap(state.pending_info_dat_drops);
        bundle.swap(state.pending_bundle_drop);
        focus_mafile = std::exchange(state.pending_mafile_focus, false);
        focus_info_dat = std::exchange(state.pending_info_dat_focus, false);
    }
    if (!mafiles.empty()) queue_mafiles(state, std::move(mafiles));
    if (!info_dat.empty()) queue_info_dat(state, std::move(info_dat));
    if (focus_mafile) {
        open_method(Method::MaFile);
    } else if (focus_info_dat) {
        open_method(Method::InfoDat);
    } else if (!bundle.empty()) {
        bundle_open_file(bundle);
        open_method(Method::Bundle);
    }
}

// A file picked from the chooser's drop target goes to the flow its type belongs to.
void route_picked_files(app::AppState& state, const std::vector<std::filesystem::path>& paths) {
    std::vector<std::string> mafiles;
    std::vector<std::string> info_dat;
    std::string bundle;
    for (const auto& p : paths) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".mafile") mafiles.push_back(to_utf8(p.wstring()));
        else if (ext == ".dat") info_dat.push_back(to_utf8(p.wstring()));
        else if (ext == ".samvault" && bundle.empty()) bundle = to_utf8(p.wstring());
    }
    if (!mafiles.empty()) {
        queue_mafiles(state, std::move(mafiles));
        open_method(Method::MaFile);
    } else if (!info_dat.empty()) {
        queue_info_dat(state, std::move(info_dat));
        open_method(Method::InfoDat);
    } else if (!bundle.empty()) {
        bundle_open_file(bundle);
        open_method(Method::Bundle);
    }
}

void pick_any_file(app::AppState& state) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Choose files to import";
    opts.allow_multiselect = true;
    opts.filters = {{L"maFiles, info.dat and bundles (*.maFile;*.dat;*.samvault)", L"*.maFile;*.dat;*.samvault"},
                    {L"All files (*.*)", L"*.*"}};
    const auto picked = platform::file_dialog::open_file(opts);
    if (!picked.ok) return;
    route_picked_files(state, picked.paths.empty() ? std::vector<std::filesystem::path>{picked.path} : picked.paths);
}

// Why a lead card, a grid and a drop target: most accounts come in by password, so that card
// leads; the others share one height in a grid; and the space under them takes files, because
// dropping them anywhere in the window already starts their import.
void draw_chooser(app::AppState& state) {
    const facet::Theme& t = facet::T();
    if (!facet::BeginScrollArea("##chooser-scroll")) return;
    begin_column("##chooser", kWideWidth);
    const MethodInfo& lead = method_info(Method::FullLogin);
    if (kit::action_card(lead.title, {.icon = lead.icon, .description = lead.when})) open_method(Method::FullLogin);
    gap(t.spacing.lg);
    facet::BeginGrid("##methods", {.minCellWidth = t.metrics.cardWidth, .maxColumns = 3, .gap = facet::Gap::Loose});
    for (int i = 1; i < kMethodCount; ++i) {
        const MethodInfo& info = method_info(static_cast<Method>(i));
        facet::GridNext();
        if (kit::action_card(info.title, {.icon = info.icon, .description = info.when, .equal_height_group = "methods"})) {
            open_method(static_cast<Method>(i));
        }
    }
    facet::EndGrid();
    gap(t.spacing.xl);
    static const char* const kAccept[] = {"SAM_FILES"};
    if (facet::DropZone("Drop files to import##drop-any", kAccept,
                        {.hint = "Drop maFiles or info.dat files anywhere in this window, or click to choose a "
                                 "file to import (a bundle too)",
                         .icon = facet::Icon("upload"),
                         .height = t.metrics.controlHeightLarge * 4.0F}) == facet::DropZoneAction::Activated) {
        pick_any_file(state);
    }
    end_column();
    facet::EndScrollArea();
}
}  // namespace

void open_method(Method method) {
    ScreenState& s = screen();
    s.method = method;
    s.choice = static_cast<int>(method);
    s.chooser = false;
}

void open_chooser() { screen().chooser = true; }

void reset_screen() {
    reset_full_login();
    reset_mafile_import();
    reset_info_dat_import();
    reset_nfa_token();
    reset_this_pc();
    reset_bundle_import();
    ScreenState& s = screen();
    s.chooser = true;
    s.choice = 0;
    s.method = Method::FullLogin;
}

void mark_seeded(const app::AppState& state) {
    ScreenState& s = screen();
    s.seeded = &state;
    s.owner = &state;
}

}  // namespace add_account_detail

// Why the way back sits only in the page header: every method's first step is one level below
// the chooser, so "All methods" next to the title is the single way back; the context line names
// the method the user is in.
void draw_add_account(app::AppState& state) {
    using namespace add_account_detail;
    adopt(state);
    drain_drops(state);
    if (state.pending_relogin_login.has_value() && begin_relogin(state, *state.pending_relogin_login)) {
        state.pending_relogin_login.reset();
        open_method(Method::FullLogin);
    }

    ScreenState& s = screen();
    const bool relogin = !s.chooser && s.method == Method::FullLogin && relogin_active();
    kit::PageScope page("add-account", {.scroll = false});
    {
        kit::PageHeaderScope header(relogin ? "Sign in again" : "Add account",
                                    {.context = s.chooser  ? "Choose where the account comes from"
                                                : relogin  ? "Sign in with password"
                                                           : method_info(s.method).title});
        if (!s.chooser && facet::Button("All methods", {.icon = facet::Icon("arrow-left")})) open_chooser();
    }

    if (s.chooser) {
        draw_chooser(state);
        return;
    }
    switch (s.method) {
        case Method::FullLogin: draw_full_login(state); break;
        case Method::MaFile: draw_mafile_import(state); break;
        case Method::ThisPc: draw_this_pc(state); break;
        case Method::NfaToken: draw_nfa_token(state); break;
        case Method::InfoDat: draw_info_dat_import(state); break;
        case Method::Bundle: draw_bundle_import(state); break;
    }
}

namespace {

// Rule 1: the flows hold secrets and half-done sign-ins, so locking or switching the vault ends
// them. The harness runs this before every capture scene too.
void reset_add_account(app::AppState&) { add_account_detail::reset_screen(); }

}  // namespace
SAM_KIT_TRANSIENT_RESET(add_account_flows, &reset_add_account)

}  // namespace sam::ui::screens
