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
    if (identifier.empty()) {
        return false;
    }

    const char first = identifier.front();
    if (!isAsciiLetter(first) && !isAsciiDigit(first)) {
        return false;
    }

    for (const char c : identifier.substr(1)) {
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
