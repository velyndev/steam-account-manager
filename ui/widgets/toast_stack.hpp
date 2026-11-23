#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace sam::ui::widgets {

enum class ToastClickAction { None, Account, Settings };

// The toast's tone. FromWarningFlag reads ToastItem::is_warning (true: Warning, false:
// Destructive), which is all a push from app/ sets; UI code that knows better sets a tone of
// its own (kit::show_toast).
enum class ToastTone { FromWarningFlag, Neutral, Success, Warning, Destructive };

struct ToastItem {
    std::string id;
    std::string message;
    std::string account_id;     // empty for non-account toasts
    bool is_warning = false;    // false = danger styling (with tone FromWarningFlag)
    ToastClickAction on_click_action = ToastClickAction::None;
    // Which settings tab a Settings-action toast opens: a screens::SettingsCategory value,
    // as an int so this header doesn't have to pull in the settings screen. Ignored unless
    // on_click_action is Settings.
    int settings_category = 0;
    std::int64_t expires_at_unix = 0;  // 0 = until dismissed
    ToastTone tone = ToastTone::FromWarningFlag;
};

// The app's toast queue. Workers push through post_ui_callback, so every member runs on the UI
// thread. render() forwards the queue to Facet's toasts (facet::ShowToast) every frame.
class ToastStack {
public:
    // Queues a toast. A toast with the id of one already queued or showing replaces it (its text
    // and tone update in place).
    void push(ToastItem item);

    // Replaces all account-scoped pending toasts with one summary entry, so a
    // refresh batch producing many events doesn't flood the stack.
    void push_summary(std::string message);

    // UI-thread only, inside a frame. Shows queued toasts as Facet toasts: the tone from
    // ToastItem::tone, the time left until expires_at_unix as duration (until dismissed for 0),
    // and for an account or settings toast an action button ("Show account", "Open settings").
    // on_click fires with the toast on the frame after its action button was pressed. Toasts
    // the user dismissed or whose time ran out leave the queue.
    void render(const std::function<void(const ToastItem&)>& on_click);

    void clear();

private:
    struct Entry {
        ToastItem item;
        std::uint32_t facet_id = 0;  // facet::ToastId value; 0 until shown
        bool changed = false;        // replaced since it was shown
        int hidden_frames = 0;       // frames since Facet stopped showing it (its action reports a frame late)
    };
    std::deque<Entry> items_;
    std::vector<std::uint32_t> dismissed_;  // shown toasts removed from the queue, to dismiss in Facet
};

}  // namespace sam::ui::widgets
