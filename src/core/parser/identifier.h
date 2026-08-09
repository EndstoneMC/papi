#pragma once

#include <string>
#include <string_view>

namespace papi::detail {

/**
 * @brief Whether an identifier matches the registration grammar.
 *
 * The grammar is a full match of <code>[A-Za-z0-9][A-Za-z0-9.-]*</code>.
 *
 * Underscore is excluded because it separates identifier from parameters, colon
 * because it was the historical duplicate-namespace separator, and braces,
 * percent, and whitespace because they interact with placeholder syntax.
 * Everything is compared as raw ASCII bytes, so behavior does not vary with the
 * process locale.
 */
[[nodiscard]] bool isValidIdentifier(std::string_view identifier) noexcept;

/**
 * @brief The canonical form of an identifier: ASCII lowercase.
 *
 * Only bytes in the ASCII range are folded, so a valid identifier maps to itself
 * apart from case.
 */
[[nodiscard]] std::string canonicalizeIdentifier(std::string_view identifier);

}  // namespace papi::detail
