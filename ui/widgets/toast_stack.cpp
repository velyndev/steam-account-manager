#include "ui/widgets/toast_stack.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

#include <imgui.h>

#include <facet/widgets/overlays.h>

namespace sam::ui::widgets {

namespace {

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

facet::Tone facet_tone(const ToastItem& t) {
    switch (t.tone) {
        case ToastTone::FromWarningFlag: return t.is_warning ? facet::Tone::Warning : facet::Tone::Destructive;
        case ToastTone::Neutral: return facet::Tone::Neutral;
        case ToastTone::Success: return facet::Tone::Success;
        case ToastTone::Warning: return facet::Tone::Warning;
        case ToastTone::Destructive: return facet::Tone::Destructive;
    }
    return facet::Tone::Neutral;
}

bool clickable(const ToastItem& t) {
    return !t.account_id.empty() || t.on_click_action == ToastClickAction::Settings;
}

facet::ToastOptions facet_options(const ToastItem& t, std::int64_t now) {
    facet::ToastOptions o;
    o.tone = facet_tone(t);
    if (t.on_click_action == ToastClickAction::Settings) {
        o.actionLabel = "Open settings";
    } else if (!t.account_id.empty()) {
        o.actionLabel = "Show account";
    }
    if (t.expires_at_unix <= 0) {
        o.duration = -1.0F;
    } else {
        o.duration = static_cast<float>(std::max<std::int64_t>(1, t.expires_at_unix - now));
    }
    return o;
}

}  // namespace

void ToastStack::push(ToastItem item) {
    if (!item.id.empty()) {
        for (auto& existing : items_) {
            if (existing.item.id == item.id) {
                existing.item = std::move(item);
                existing.changed = true;
                return;
            }
        }
    }
    items_.push_back(Entry{std::move(item)});
}

void ToastStack::push_summary(std::string message) {
    for (auto it = items_.begin(); it != items_.end();) {
        if (!it->item.account_id.empty()) {
            if (it->facet_id != 0) dismissed_.push_back(it->facet_id);
            it = items_.erase(it);
        } else {
            ++it;
        }
    }
    ToastItem t;
    t.id = "summary-" + std::to_string(now_unix());
    t.message = std::move(message);
    t.is_warning = false;
    t.expires_at_unix = now_unix() + 8;
    push(std::move(t));
}

void ToastStack::render(const std::function<void(const ToastItem&)>& on_click) {
    for (const std::uint32_t id : dismissed_) facet::DismissToast(facet::ToastId{id});
    dismissed_.clear();

    const auto now = now_unix();
    for (auto it = items_.begin(); it != items_.end();) {
        Entry& e = *it;
        // A toast that expired before it ever showed is dropped unseen.
        if (e.facet_id == 0 && e.item.expires_at_unix > 0 && e.item.expires_at_unix <= now) {
            it = items_.erase(it);
            continue;
        }
        if (e.facet_id == 0) {
            e.facet_id = facet::ShowToast(e.item.message.c_str(), facet_options(e.item, now)).value;
            e.changed = false;
            if (e.facet_id == 0) {  // outside a frame; try again next frame
                ++it;
                continue;
            }
        } else if (e.changed) {
            const facet::ToastId shown{e.facet_id};
            if (facet::IsToastVisible(shown)) {
                facet::UpdateToast(shown, e.item.message.c_str(), facet_options(e.item, now));
            } else {
                e.facet_id = facet::ShowToast(e.item.message.c_str(), facet_options(e.item, now)).value;
                e.hidden_frames = 0;
            }
            e.changed = false;
        }
        const facet::ToastId id{e.facet_id};
        if (clickable(e.item) && facet::ToastActionPressed(id)) {
            const ToastItem item = e.item;
            it = items_.erase(it);
            if (on_click) on_click(item);
            continue;
        }
        // The action button dismisses its toast and reports the press on the next frame, so a
        // toast that stopped showing stays queued for one more frame before it leaves.
        if (!facet::IsToastVisible(id) && ++e.hidden_frames > 1) {
            it = items_.erase(it);
            continue;
        }
        ++it;
    }
}

void ToastStack::clear() {
    for (const Entry& e : items_) {
        if (e.facet_id != 0) facet::DismissToast(facet::ToastId{e.facet_id});
    }
    items_.clear();
}

}  // namespace sam::ui::widgets
