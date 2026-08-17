#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace papi::detail {

class PlaceholderResolver {
public:
    virtual ~PlaceholderResolver() = default;

    /**
     * @brief Resolves one placeholder.
     *
     * @param canonical_identifier the identifier, already lowercased and validated
     * @param params everything after the first colon, byte for byte
     * @return the replacement value, or nullopt to keep the original token
     */
    [[nodiscard]] virtual std::optional<std::string> resolve(std::string_view canonical_identifier,
                                                             std::string_view params) = 0;
};

/**
 * @brief Replaces every resolvable <code>{identifier:params}</code> in text.
 *
 * The candidate splits at its first colon. The identifier must satisfy the registration
 * grammar and is ASCII-lowercased. Parameters are passed through untouched, including
 * underscores, dots, later colons, case, spaces, punctuation, and an empty value.
 *
 * A candidate the resolver declines is emitted exactly as it appeared. Replacement
 * text is never rescanned. The scan is linear, uses no regular expression, and does
 * not recurse.
 */
[[nodiscard]] std::string replacePlaceholders(std::string_view text, PlaceholderResolver &resolver);

/**
 * @brief Whether text lexically contains a placeholder-shaped substring.
 *
 * True when some opening brace has a closing brace after it. This deliberately does
 * not validate the candidate or consult the registry.
 */
[[nodiscard]] bool containsPlaceholders(std::string_view text) noexcept;

}  // namespace papi::detail
