// The "Import a bundle" method of Add account: choose the .samvault file, unlock it with its
// passphrase, review the accounts in it, and see what was imported. All on the page, in the same
// flow shell as the other methods.

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <imgui.h>

#include <facet/core/icons.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/media.h>

#include "app/app_state.hpp"
#include "core/crypto/secure_string.hpp"
#include "platform/file_dialog.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/screens/add_account/bulk.hpp"
#include "ui/screens/add_account_detail.hpp"
#include "ui/util.hpp"
#include "ui/widgets/bundle_dialogs.hpp"

#ifdef SAM_UI_CAPTURE
#include "app/ui_capture.hpp"
#endif

namespace sam::ui::screens::add_account_detail {

namespace {

constexpr const char* kSteps[] = {"Choose a bundle", "Unlock", "Review", "Done"};

struct BundleFlow {
    std::string path;
    std::string path_error;
    widgets::ImportBundleState dialog;
};

BundleFlow& flow() {
    static BundleFlow f;
    return f;
}

void open_bundle(BundleFlow& f) {
    f.path_error.clear();
    const std::filesystem::path p(to_wide(trim_path(f.path)));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        f.path_error = "No file at this path.";
        return;
    }
    widgets::request_import_bundle(f.dialog, p);
}

void pick(app::AppState& state, BundleFlow& f) {
    platform::file_dialog::Options opts;
    opts.parent = state.main_hwnd;
    opts.title = L"Import bundle";
    opts.filters = {{L"Steam Account Manager bundle (*.samvault)", L"*.samvault"}, {L"All files (*.*)", L"*.*"}};
    const auto picked = platform::file_dialog::open_file(opts);
    if (picked.ok) {
        f.path = to_utf8(picked.path.wstring());
        open_bundle(f);
    }
}

void back_to_choose(BundleFlow& f) {
    widgets::reset_import_bundle(f.dialog);
    f.dialog.open = false;
}

void draw_choose(app::AppState& state, BundleFlow& f) {
    const facet::Theme& t = facet::T();
    begin_flow("##bundle", kSteps, 0, kFormWidth);
    begin_step_card("##bundle-card", "Choose a bundle",
                    "A bundle is an encrypted .samvault file exported from this app. You see every account in it "
                    "before anything is written.");
    static const char* const kAccept[] = {"SAM_FILES"};
    if (facet::DropZone("Choose a bundle##bundle-zone", kAccept,
                        {.hint = "Click to choose a .samvault file", .icon = facet::Icon("upload")}) ==
        facet::DropZoneAction::Activated) {
        pick(state, f);
    }
    gap(t.spacing.lg);
    facet::PathField("Or a path", &f.path,
                     {.placeholder = "steam-accounts-2026-01-01.samvault",
                      .error = f.path_error.empty() ? nullptr : f.path_error.c_str()});
    if (facet::PathFieldBrowsePressed()) pick(state, f);
    end_step_card();
    begin_commit_bar();
    ImGui::BeginDisabled(trim_path(f.path).empty());
    if (facet::Button("Open bundle", {.variant = facet::ButtonVariant::Primary})) open_bundle(f);
    ImGui::EndDisabled();
    end_flow();
}

void draw_unlock(app::AppState& state, BundleFlow& f) {
    const facet::Theme& t = facet::T();
    auto& d = f.dialog;
    begin_flow("##bundle", kSteps, 1, kFormWidth);
    begin_step_card("##bundle-unlock", "Unlock the bundle",
                    "Its passphrase was chosen when it was exported. It is not your master password.");
    facet::BeginKeyValueList("##file");
    facet::KeyValue("File", to_utf8(d.picked_path.filename().wstring()).c_str());
    facet::KeyValue("Folder", to_utf8(d.picked_path.parent_path().wstring()).c_str());
    facet::EndKeyValueList();
    gap(t.spacing.md);
    if (facet::SecureField("Passphrase", d.passphrase_buf.data(), d.passphrase_buf.size(),
                           {.error = d.wrong_passphrase ? "This passphrase does not open the bundle." : nullptr})) {
        d.wrong_passphrase = false;
    }
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && d.passphrase_buf[0] != 0;
    bool choose_other = false;
    if (!d.error.empty()) {
        gap(t.spacing.md);
        choose_other = flow_error("open-error", "The bundle could not be opened", d.error.c_str(), d.error_raw,
                                  "Choose another file");
    }
    end_step_card();
    begin_commit_bar();
    if (facet::Button("Choose another file")) choose_other = true;
    ImGui::BeginDisabled(d.passphrase_buf[0] == 0);
    const bool unlock = facet::Button("Unlock bundle", {.variant = facet::ButtonVariant::Primary});
    ImGui::EndDisabled();
    end_flow();
    if (unlock || enter) widgets::bundle_unlock(state, d);
    if (choose_other) back_to_choose(f);
}

void draw_review(app::AppState& state, BundleFlow& f) {
    const facet::Theme& t = facet::T();
    auto& d = f.dialog;
    begin_flow("##bundle", kSteps, 2, kWideWidth);
    begin_step_card("##bundle-review", nullptr, nullptr, true);
    const auto n = static_cast<std::int64_t>(d.preview_vault.accounts.size());
    const std::string title = kit::format_count(n) + (n == 1 ? " account in this bundle" : " accounts in this bundle");
    bulk_header(title.c_str(), d.rows, false);
    if (d.preview_report.overwritten > 0) {
        hint("An account that updates one in the vault takes the bundle's login, password, secrets and notes, and "
             "keeps its place in the vault.");
        gap(t.spacing.sm);
    }
    select_all_box("##select", d.rows);
    gap(t.spacing.sm);
    bulk_table("##bundle-rows", d.rows, {.hide_names = state.settings.privacy_mode, .source_header = "Login"});
    if (!d.error.empty()) {
        gap(t.spacing.sm);
        (void)flow_error("import-error", "Nothing was imported", d.error.c_str(), d.error_raw);
    }
    end_step_card();
    const std::string status = selection_status(d.rows) + ". Nothing is written until you import.";
    begin_commit_bar(status.c_str());
    const bool other = facet::Button("Choose another file");
    const int picked = selected_count(d.rows);
    char label[64];
    std::snprintf(label, sizeof(label), picked == 1 ? "Import 1 account" : "Import %s accounts", kit::format_count(picked).c_str());
    ImGui::BeginDisabled(picked == 0);
    const bool import = facet::Button(label, {.variant = facet::ButtonVariant::Primary});
    ImGui::EndDisabled();
    end_flow();
    if (import) widgets::bundle_import(state, d);
    if (other) back_to_choose(f);
}

void draw_done(app::AppState& state, BundleFlow& f) {
    const facet::Theme& t = facet::T();
    auto& d = f.dialog;
    begin_flow("##bundle", kSteps, 4, kWideWidth);
    kit::web_api_key_notice(state, {.compact = true});
    if (state.settings.web_api_key.empty()) gap(t.spacing.sm);
    begin_step_card("##bundle-done", nullptr, nullptr, true);
    bulk_header("Bundle imported", d.rows, true);
    bulk_table("##bundle-results", d.rows,
               {.selectable = false, .hide_names = state.settings.privacy_mode, .source_header = "Login"});
    end_step_card();
    begin_commit_bar();
    if (facet::Button("Import another bundle")) {
        back_to_choose(f);
        f.path.clear();
    } else {
        open_accounts_button(state, {});
    }
    end_flow();
}

}  // namespace

// Why on the page: the bundle has the same choose, review and done steps as the other imports,
// so it runs in the same flow shell instead of a stack of modals.
void draw_bundle_import(app::AppState& state) {
    BundleFlow& f = flow();
    if (f.dialog.done) {
        draw_done(state, f);
    } else if (f.dialog.have_preview) {
        draw_review(state, f);
    } else if (!f.dialog.picked_path.empty()) {
        draw_unlock(state, f);
    } else {
        draw_choose(state, f);
    }
}

void reset_bundle_import() {
    BundleFlow& f = flow();
    widgets::reset_import_bundle(f.dialog);
    f = BundleFlow{};
}

void bundle_open_file(const std::string& path) {
    BundleFlow& f = flow();
    f.path = path;
    open_bundle(f);
}

#ifdef SAM_UI_CAPTURE
namespace {

void setup_bundle(app::AppState& state) {
    app::ui_capture::open_demo_vault(state);
    state.current_screen = app::Screen::AddAccount;
    reset_screen();
    open_method(Method::Bundle);
    mark_seeded(state);
}

constexpr const char* kDemoBundle = "C:\\Users\\Demo\\Desktop\\steam-accounts-2026-11-01.samvault";

// A bundle from another PC: three accounts this vault already has and five new ones, seeded in
// memory without a file.
void seed_preview(app::AppState& state, bool done) {
    setup_bundle(state);
    BundleFlow& f = flow();
    f.path = kDemoBundle;
    widgets::request_import_bundle(f.dialog, f.path);
    core::Vault v;
    for (std::size_t i = 0; i < 3 && i < state.vault.accounts.size(); ++i) v.accounts.push_back(state.vault.accounts[i]);
    for (int i = 0; i < 5; ++i) {
        core::Account a;
        a.id = "demo-bundle-" + std::to_string(i);
        a.login = "demo-bundle-acc-" + std::to_string(i);
        a.is_nfa = i == 4;
        v.accounts.push_back(std::move(a));
    }
    f.dialog.preview_report = core::store::preview_merge(state.vault, v);
    f.dialog.preview_vault = std::move(v);
    f.dialog.have_preview = true;
    widgets::build_import_rows(state, f.dialog);
    f.dialog.rows[5].checked = false;
    if (done) {
        for (auto& r : f.dialog.rows) {
            if (!r.checked) continue;
            r.note = r.status == RowStatus::Update ? "Replaced the account in the vault." : r.note;
            r.status = r.status == RowStatus::Update ? RowStatus::Updated : RowStatus::Imported;
        }
        finish_rows(f.dialog.rows);
        f.dialog.done = true;
    }
}

void setup_bundle_unlock(app::AppState& state) {
    setup_bundle(state);
    BundleFlow& f = flow();
    f.path = kDemoBundle;
    widgets::request_import_bundle(f.dialog, f.path);
    std::snprintf(f.dialog.passphrase_buf.data(), f.dialog.passphrase_buf.size(), "%s", "placeholder");
    f.dialog.wrong_passphrase = true;
}

void setup_bundle_preview(app::AppState& state) { seed_preview(state, false); }
void setup_bundle_result(app::AppState& state) { seed_preview(state, true); }

}  // namespace
SAM_UI_CAPTURE_SCENE(add_bundle, "add-bundle", &setup_bundle, 30)
SAM_UI_CAPTURE_SCENE(add_bundle_unlock, "add-bundle-unlock", &setup_bundle_unlock, 30)
SAM_UI_CAPTURE_SCENE(add_bundle_preview, "add-bundle-preview", &setup_bundle_preview, 30)
SAM_UI_CAPTURE_SCENE(add_bundle_result, "add-bundle-result", &setup_bundle_result, 30)
#endif

}  // namespace sam::ui::screens::add_account_detail
