#pragma once

// The Authenticator screen's dialogs: linking (and checking with Steam), removing, backing up,
// exporting maFiles and showing secrets. Their state lives in one object for the whole run, so
// the background jobs of a link or removal can write into it after the screen is left.
// Only sda_screen.cpp and the capture scenes use this header.

#include <array>
#include <string>
#include <unordered_set>

#include "app/app_state.hpp"

namespace sam::ui::screens::authenticator {

struct LinkFlowState {
    enum class Mode {
        Link,    // add a new authenticator, or finish one whose activation is pending
        Verify,  // only ask Steam whether the stored authenticator is the live one
    };
    // The step shown in the step indicator. A request in flight (working) or a failure (error)
    // is shown inside the step it belongs to.
    enum class Step { Intro, Revocation, Activation, Done };

    bool open = false;
    Mode mode = Mode::Link;
    Step step = Step::Intro;
    std::string account_id;
    bool working = false;          // a Steam request is in flight; working_text says which
    std::string working_text;
    std::string error;             // non-empty: the last request failed (shown in place)
    std::string error_details;     // Steam's own text or status, behind "Details"; may be empty
    bool acknowledged = false;     // the revocation code was written down
    bool bad_code = false;         // Steam rejected the activation code; ask for the newest one
    std::array<char, 16> activation{};
};

struct RemoveFlowState {
    bool open = false;
    std::string account_id;
    int scheme = 1;                // 1: switch to email Steam Guard, 2: remove Steam Guard entirely
    std::array<char, 8> code{};    // the current code, typed as a check
    bool working = false;
    bool done = false;
    std::string error;
    std::string error_details;     // Steam's own text, behind "Details"; may be empty
};

struct BackupState {
    bool open = false;
    std::string account_id;
    std::string synthesized_uri;   // rebuilt from the shared secret when none is stored; not persisted
};

struct ExportState {
    bool open = false;
    std::string opened_for;        // the account selected when the export was opened
    std::unordered_set<std::string> selected_ids;
    std::string passphrase;
    std::string confirm;
    std::string status;
    int written = 0;
    int failed = 0;
};

struct SecretsState {
    bool open = false;
    std::string account_id;
};

struct Dialogs {
    LinkFlowState link;
    RemoveFlowState remove;
    BackupState backup;
    ExportState export_mafiles;
    SecretsState secrets;
};

Dialogs& dialogs();

// Opens the link flow. An account whose authenticator is stored but not activated
// (fully_enrolled false) resumes at the activation code; any other starts at the intro.
void request_link(app::AppState& app, const std::string& account_id);
// Opens the check with Steam and starts it: QueryStatus confirms that the stored maFile is the
// live authenticator and marks it fully enrolled.
void request_verify(app::AppState& app, const std::string& account_id);
void request_remove(const std::string& account_id);
void request_backup(const std::string& account_id);
// Opens the export with `preselect_id` checked (empty for none); it belongs to that account.
void request_export(const std::string& preselect_id);
void request_secrets(const std::string& account_id);

// Closes every dialog and clears what was typed into them (registered with the kit's transient
// reset and screen-leave hooks). A link or removal whose Steam request is still running keeps
// its account id, so the answer still reaches the vault; only the dialog closes.
void reset_dialogs(app::AppState& app);
// Closes the dialogs opened for an account other than `selected_id` (rule: dialog state follows
// the selection). A dialog waiting for Steam stays open; it is modal, so the selection cannot
// change under it from the list.
void close_dialogs_not_for(const std::string& selected_id);

// Draws whichever dialog is open. Call every frame from the screen, at one place.
void draw_link_dialog(app::AppState& app);
void draw_remove_dialog(app::AppState& app);
void draw_backup_dialog(app::AppState& app);
void draw_export_dialog(app::AppState& app);
void draw_secrets_dialog(app::AppState& app);

#ifdef SAM_UI_CAPTURE
// Capture harness only: shows the revocation code of `account_id` as if revealed.
void capture_reveal_revocation(const std::string& account_id);
#endif

}  // namespace sam::ui::screens::authenticator
