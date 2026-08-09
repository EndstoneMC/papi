#pragma once

#include <endstone/endstone.hpp>

namespace papi {
class PlaceholderAPI : public endstone::Service {
public:
    using Processor = std::function<std::string(const endstone::Player *, std::string)>;
    /**
     * Translates all placeholders into their corresponding values.
     * <br>The pattern of a valid placeholder is <code>{<identifier>|<params>}</code>.
     *
     * @param player Player to parse the placeholders against, could be nullptr.
     * @param text Text to set the placeholder values in
     * @return String containing all translated placeholders
     */
    [[nodiscard]] virtual std::string setPlaceholders(const endstone::Player *player, std::string_view text) const = 0;

    /**
     * Check if a specific placeholder identifier is currently registered.
     *
     * @param identifier The identifier to check
     * @return true if identifier is already registered
     */
    [[nodiscard]] virtual bool isRegistered(std::string_view identifier) const = 0;

    /**
     * Get all registered placeholder identifiers.
     *
     * @return A Set of type String containing the identifiers of all registered placeholders
     */
    [[nodiscard]] virtual std::vector<std::string> getRegisteredIdentifiers() const = 0;

    /**
     * Get the normal placeholder pattern.
     *
     * @return Regex Pattern of <code>[{]([^{}]+)[}]</code>
     */
    [[nodiscard]] virtual std::string getPlaceholderPattern() const = 0;

    /**
     * Check if a String contains any placeholders (<code>{<identifier>|<params>}</code>).
     *
     * @param text String to check
     * @return true if String contains any matches to the bracket placeholder pattern, false otherwise
     */
    [[nodiscard]] virtual bool containsPlaceholders(std::string_view text) const = 0;

    /**
     * Attempt to register a placeholder.
     *
     * @param plugin the plugin that is registering the placeholder
     * @param identifier the identifier of the placeholder
     * @param processor the processor that will process the placeholder
     * @return true if the placeholder was successfully registered, false otherwise
     */
    [[nodiscard]] virtual bool registerPlaceholder(const endstone::Plugin &plugin, std::string_view identifier,
                                                   Processor processor) const = 0;
};
}  // namespace papi
