#pragma once

#include <string>
#include <string_view>

namespace papi::detail {

/**
 * @brief Whether an identifier matches the registration grammar.
 *
 * The grammar is a full match of <code>[A-Za-z0-9][A-Za-z0-9-]*</code>.
 * Dot is the placeholder separator and underscore belongs to keys, so neither is
 * permitted in an identifier.
 */
[[nodiscard]] bool isValidIdentifier(std::string_view identifier) noexcept;

/**
 * @brief The canonical form of an identifier: ASCII lowercase.
 */
[[nodiscard]] std::string canonicalizeIdentifier(std::string_view identifier);

}  // namespace papi::detail
