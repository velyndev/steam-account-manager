#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>

namespace sam::ui {

std::string to_utf8(const std::wstring& w);
std::wstring to_wide(std::string_view u8);

inline std::int64_t now_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// CS2 weekly XP / shop reset: Wednesday at this UTC hour.
inline constexpr int kWeeklyResetWeekday = 3;  // tm_wday: 0=Sun .. 3=Wed
inline constexpr int kWeeklyResetHourUtc = 1;  // 01:00 UTC

// Unix time (s, UTC) of the next weekly reset strictly after from_unix. If from_unix
// is already on Wednesday past the reset hour, this rolls to the following Wednesday.
inline std::int64_t next_weekly_reset(std::int64_t from_unix) {
    const auto t = static_cast<std::time_t>(from_unix);
    std::tm tm{};
    gmtime_s(&tm, &t);
    tm.tm_mday += (kWeeklyResetWeekday - tm.tm_wday + 7) % 7;  // this week's Wednesday
    tm.tm_hour  = kWeeklyResetHourUtc;
    tm.tm_min   = 0;
    tm.tm_sec   = 0;
    tm.tm_isdst = 0;                                           // UTC has no DST
    std::int64_t reset = static_cast<std::int64_t>(_mkgmtime(&tm));
    if (reset <= from_unix) reset += 7 * 86400;                // already past today's reset
    return reset;
}

// "YYYY-MM-DD" (UTC), or empty for a non-positive timestamp.
std::string format_date(std::int64_t unix_seconds);

// Coarse "5m ago" / "3d ago", falling back to format_date past 30 days.
// "never" for a zero/unset timestamp.
std::string format_relative(std::int64_t unix_seconds);

// UTF-8-safe clip to max_w with a trailing "...". Returns the input unchanged when
// it already fits, so callers can compare to detect truncation.
std::string truncate_to_width(const std::string& text, float max_w);

void open_url(const std::string& url);

// Opens a folder (or file) in Explorer / the shell default.
void open_folder(const std::filesystem::path& path);

}  // namespace sam::ui
