#include "core/parser/bracket_replacer.h"

#include "core/parser/identifier.h"

namespace papi::detail {

std::string replacePlaceholders(const std::string_view text, PlaceholderResolver &resolver)
{
    if (text.find('{') == std::string_view::npos) {
        return std::string(text);
    }

    std::string result;
    result.reserve(text.size());

    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto open = text.find('{', pos);
        if (open == std::string_view::npos) {
            result.append(text.substr(pos));
            break;
        }
        result.append(text.substr(pos, open - pos));

        const auto close = text.find('}', open + 1);
        if (close == std::string_view::npos) {
            result.append(text.substr(open));
            break;
        }

        const auto candidate = text.substr(open + 1, close - open - 1);
        const auto token = text.substr(open, close - open + 1);

        std::optional<std::string> value;
        if (const auto separator = candidate.find(':'); separator != std::string_view::npos) {
            const auto raw_identifier = candidate.substr(0, separator);
            if (isValidIdentifier(raw_identifier)) {
                value = resolver.resolve(canonicalizeIdentifier(raw_identifier), candidate.substr(separator + 1));
            }
        }

        if (value) {
            result.append(*value);
        }
        else {
            result.append(token);
        }

        pos = close + 1;
    }

    return result;
}

bool containsPlaceholders(const std::string_view text) noexcept
{
    const auto open = text.find('{');
    return open != std::string_view::npos && text.find('}', open + 1) != std::string_view::npos;
}

}  // namespace papi::detail
