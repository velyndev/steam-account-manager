#pragma once

// Dialogs and toasts the screens repeat.

#include <string>

#include <facet/widgets/overlays.h>

#include "app/app_state.hpp"
#include "ui/widgets/toast_stack.hpp"

namespace sam::ui::kit {

// --- Dialogs ---------------------------------------------------------------------------------
// Each is a Facet layer the caller opens by writing *open = true and calls every frame, open or
// not, at the same place in the id stack. Escape, the scrim and Cancel close them.

// A confirm dialog: title, `message` (read during the call), Cancel and `confirm_label` as the
// primary button (say what it does: "Approve 3 confirmations", never "OK"). Enter confirms.
// Returns Confirmed or Canceled on the deciding frame and writes *open = false; None otherwise.
facet::DialogResult confirm_dialog(const char* title, bool* open, const char* message, const char* confirm_label);

// The same for an action that deletes or cannot be undone: the confirm button is Destructive,
// focus starts on Cancel and Enter does not confirm.
facet::DialogResult destructive_dialog(const char* title, bool* open, const char* message,
                                       const char* confirm_label);

// An error the user has to read: a modal with the title, an InlineAlert in the destructive tone
// holding `message`, and a "Close" button. Returns true on the frame it closes.
bool error_dialog(const char* title, bool* open, const char* message);

// The "No Steam Web API key" warning: an InlineAlert saying which data stays empty without a key,
// with an "Open settings" action that opens the category holding the key. Draws nothing while
// settings.web_api_key is set. Reads state.settings; navigates on the action.
void web_api_key_notice(app::AppState& state);

struct ApiKeyNoticeOptions {
    // One line above a table or list: a Warning banner strip with the same action, instead of
    // the boxed alert with its title.
    bool compact = false;
};
// The same notice with options.
void web_api_key_notice(app::AppState& state, const ApiKeyNoticeOptions& options);

// --- Toasts ----------------------------------------------------------------------------------

struct ToastOptions {
    widgets::ToastTone tone = widgets::ToastTone::Neutral;
    // Coalescing key: a toast pushed with the id of one still showing replaces it. Empty for
    // none.
    std::string id;
    // With an account, the toast gets a "Show account" action that selects it on Accounts.
    std::string account_id;
    // A screens::SettingsCategory value as int: the toast gets an "Open settings" action that
    // opens that category. -1 for none. Ignored when account_id is set.
    int settings_category = -1;
    // Seconds on screen; 0 for settings.notifications.toast_duration_seconds, below 0 until
    // the user dismisses it.
    int seconds = 0;
};

// Shows a toast from UI code. It goes through state.toasts, the same queue the workers in
// app/ push into, so coalescing, the click actions and the Facet rendering are the same for
// both. Writes state.toasts.
void show_toast(app::AppState& state, std::string message, const ToastOptions& options = {});

}  // namespace sam::ui::kit
