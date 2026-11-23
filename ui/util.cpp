#include "ui/util.hpp"

#include <cstdio>

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

namespace sam::ui {

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                       nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring to_wide(std::string_view u8) {
    if (u8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()),
                                       nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), out.data(), n);
    return out;
}

std::string format_date(std::int64_t unix_seconds) {
    if (unix_seconds <= 0) return {};
    const auto t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string format_relative(std::int64_t unix_seconds) {
    if (unix_seconds <= 0) return "never";
    const auto delta = now_seconds() - unix_seconds;
    if (delta < 0)        return "in the future";
    if (delta < 60)       return std::to_string(delta) + "s ago";
    if (delta < 3600)     return std::to_string(delta / 60) + "m ago";
    if (delta < 86400)    return std::to_string(delta / 3600) + "h ago";
    if (delta < 86400*30) return std::to_string(delta / 86400) + "d ago";
    return format_date(unix_seconds);
}

std::string truncate_to_width(const std::string& text, float max_w) {
    if (max_w <= 0.0F) return {};
    if (ImGui::CalcTextSize(text.c_str()).x <= max_w) return text;
    const float ellipsis_w = ImGui::CalcTextSize("...").x;
    std::string out = text;
    while (!out.empty()) {
        while (!out.empty()) {
            const unsigned char c = static_cast<unsigned char>(out.back());
            out.pop_back();
            if ((c & 0xC0) != 0x80) break;
        }
        if (out.empty()) break;
        if (ImGui::CalcTextSize(out.c_str()).x + ellipsis_w <= max_w) break;
    }
    out += "...";
    return out;
}

void open_url(const std::string& url) {
    const auto w = to_wide(url);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_folder(const std::filesystem::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace sam::ui
