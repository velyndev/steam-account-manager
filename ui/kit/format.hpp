#pragma once

// Number formatting shared by every screen (brief rule 8: numbers with thousands separators).

#include <cstdint>
#include <string>

namespace sam::ui::kit {

// `value` with a comma between groups of three digits: 11280 -> "11,280", -4200 -> "-4,200".
std::string format_count(std::int64_t value);

}  // namespace sam::ui::kit
