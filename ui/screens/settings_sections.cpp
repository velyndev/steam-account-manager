#include "ui/screens/settings_sections.hpp"

#include <cstdint>
#include <exception>
#include <span>

#include "app/app_paths.hpp"
#include "core/log.hpp"
#include "platform/dpapi.hpp"
#include "platform/fs.hpp"

namespace sam::ui::screens {
namespace settings_detail {

bool write_master_pw_cache(app::AppState& state) {
    try {
        std::span<const std::uint8_t> pw_bytes{
            reinterpret_cast<const std::uint8_t*>(state.master_password.data()),
            state.master_password.size()};
        auto wrapped = sam::platform::dpapi::protect(pw_bytes);
        sam::platform::atomic_write_file(app::master_pw_cache_path(), wrapped);
        SAM_LOG_INFO("auto-unlock: DPAPI cache written");
        return true;
    } catch (const std::exception& ex) {
        SAM_LOG_ERROR("auto-unlock: cache write failed: {}", ex.what());
        return false;
    }
}

}  // namespace settings_detail
}  // namespace sam::ui::screens
