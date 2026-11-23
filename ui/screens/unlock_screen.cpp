#include "ui/screens/unlock_screen.hpp"

#include <string>

#include "ui/screens/unlock/locked_card.hpp"
#include "ui/screens/unlock/session.hpp"

namespace sam::ui::screens {

bool try_cached_unlock(app::AppState& state) {
    return unlock::unlock_with_remembered_password(state, nullptr);
}

bool create_and_open_vault(app::AppState& state, const std::string& name, std::uint32_t color,
                           const std::string& pw, std::string& err) {
    return unlock::create_separate_vault(state, name, color, pw, err);
}

void draw_unlock(app::AppState& state) {
    unlock::draw_locked_card(state);
}

}  // namespace sam::ui::screens
