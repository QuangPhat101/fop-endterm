#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace halo {

inline bool parseUint64Strict(std::string_view text, uint64_t& value) {
    if (text.empty()) {
        return false;
    }

    uint64_t parsed = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10ULL) {
            return false;
        }
        parsed = parsed * 10ULL + digit;
    }
    value = parsed;
    return true;
}

}  // namespace halo
