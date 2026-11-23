#include "ui/kit/transient.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace sam::ui::kit {

namespace {

// Function-local statics: registrations run during static initialisation, in any order.
std::vector<TransientReset>& resets() {
    static std::vector<TransientReset> list;
    return list;
}

std::vector<std::pair<app::Screen, TransientReset>>& leaves() {
    static std::vector<std::pair<app::Screen, TransientReset>> list;
    return list;
}

}  // namespace

void register_transient_reset(TransientReset fn) {
    if (fn == nullptr) return;
    auto& list = resets();
    if (std::find(list.begin(), list.end(), fn) == list.end()) list.push_back(fn);
}

void register_screen_leave(app::Screen screen, TransientReset fn) {
    if (fn == nullptr) return;
    auto& list = leaves();
    const auto entry = std::make_pair(screen, fn);
    if (std::find(list.begin(), list.end(), entry) == list.end()) list.push_back(entry);
}

void reset_transient_ui(app::AppState& state) {
    for (const TransientReset fn : resets()) fn(state);
    for (const auto& [screen, fn] : leaves()) fn(state);
}

void leave_screen(app::AppState& state, app::Screen left) {
    for (const auto& [screen, fn] : leaves()) {
        if (screen == left) fn(state);
    }
}

}  // namespace sam::ui::kit
