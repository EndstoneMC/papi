#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace papi::detail {

/**
 * @brief Supplies values for ordinary placeholders during a scan.
 *
 * The parser knows nothing about the registry; it hands each syntactically valid
 * candidate to a resolver and uses whatever comes back.
 */
class PlaceholderResolver {
public:
    virtual ~PlaceholderResolver() = default;

    /**
     * @brief Resolves one placeholder.
     *
     * @param canonical_identifier the identifier, already lowercased and validated
     * @param params everything after the first underscore, byte for byte
     * @return the replacement value, or nullopt to keep the original token
     */
    [[nodiscard]] virtual std::optional<std::string> resolve(std::string_view canonical_identifier,
                                                             std::string_view params) = 0;
};

/**
 * @brief Replaces every resolvable <code>{identifier_params}</code> in text.
 *
 * A single forward pass. For each opening brace the parser takes the first closing
 * brace after it as the end of the candidate, so nesting is not recognized: in
 * <code>{player_{other_x}}</code> the candidate is <code>player_{other_x</code> and
 * the trailing brace is ordinary text.
 *
 * The candidate splits at its first underscore. The identifier must satisfy the
 * registration grammar, which is why <code>{player}</code>, <code>{_x}</code>, and
 * <code>{ player_x}</code> can never resolve and stay literal. Parameters are
 * passed through untouched, including case, spaces, and braces.
 *
 * A candidate the resolver declines is emitted exactly as it appeared. Whether it
 * resolved or not, the scan continues after the candidate's closing brace, so a
 * replacement value is never rescanned and an unresolved candidate cannot be
 * reinterpreted from a later offset.
 *
 * The scan is linear in the length of the input, uses no regular expression, and
 * does not recurse.
 */
[[nodiscard]] std::string replacePlaceholders(std::string_view text, PlaceholderResolver &resolver);

/**
 * @brief Whether text lexically contains a placeholder-shaped substring.
 *
 * True when some opening brace has a closing brace after it. Deliberately does not
 * check for an underscore, a valid identifier, or a registration, so
 * <code>{}</code> and <code>{unknown_x}</code> both qualify while <code>}{</code>
 * and <code>{unclosed</code> do not.
 *
 * Pure, allocation-free, and safe from any thread.
 */
[[nodiscard]] bool containsPlaceholders(std::string_view text) noexcept;

}  // namespace papi::detail
