#include "core/parser/identifier.h"

namespace papi::detail {
namespace {

[[nodiscard]] constexpr bool isAsciiDigit(const char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool isAsciiLetter(const char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr char toAsciiLower(const char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

bool isValidIdentifier(const std::string_view identifier) noexcept
{
    const auto size = identifier.size();
    if (size == 0) {
        return false;
    }

    // Use data()/size() instead of front()/substr() so the noexcept contract is
    // provably satisfied -- substr is not noexcept (may throw out_of_range).
    const char *const chars = identifier.data();
    const char first = chars[0];
    if (!isAsciiLetter(first) && !isAsciiDigit(first)) {
        return false;
    }

    for (std::size_t i = 1; i < size; ++i) {
        const char c = chars[i];
        if (!isAsciiLetter(c) && !isAsciiDigit(c) && c != '.' && c != '-') {
            return false;
        }
    }
    return true;
}

std::string canonicalizeIdentifier(const std::string_view identifier)
{
    std::string canonical;
    canonical.reserve(identifier.size());
    for (const char c : identifier) {
        canonical.push_back(toAsciiLower(c));
    }
    return canonical;
}

}  // namespace papi::detail
