#include "ui/kit/format.hpp"

namespace sam::ui::kit {

std::string format_count(std::int64_t value) {
    const bool negative = value < 0;
    // Unsigned so the most negative value has a magnitude too.
    std::uint64_t magnitude = negative ? 0 - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    const std::string digits = std::to_string(magnitude);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    if (negative) out.push_back('-');
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(digits[i]);
        const std::size_t rest = n - i - 1;
        if (rest > 0 && rest % 3 == 0) out.push_back(',');
    }
    return out;
}

}  // namespace sam::ui::kit
