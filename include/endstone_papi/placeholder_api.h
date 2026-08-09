#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <endstone/offline_player.h>
#include <endstone/player.h>
#include <endstone/plugin/plugin.h>
#include <endstone/plugin/service.h>

#include "endstone_papi/expansion_info.h"
#include "endstone_papi/placeholder_expansion.h"

namespace papi {

/**
 * @brief The PlaceholderAPI service: a framework for resolving placeholders.
 *
 * PAPI registers one concrete native implementation of this interface under the
 * Endstone service name #ServiceName. Consumers obtain it through the service
 * manager:
 *
 * @code
 * auto api = plugin.getServer().getServiceManager()
 *                .load<papi::PlaceholderAPI>(std::string(papi::PlaceholderAPI::ServiceName));
 * if (api && api->isActive()) {
 *     api->registerExpansion(plugin, std::make_shared<MyExpansion>());
 * }
 * @endcode
 *
 * The service itself provides no placeholder values. Every value comes from a
 * PlaceholderExpansion supplied by a plugin, in either C++ or Python.
 *
 * A consumer may keep its shared_ptr indefinitely. Once PAPI is disabled the
 * object becomes permanently inert instead of dangling: isActive returns false,
 * parsing returns its input unchanged, queries return empty results, mutations
 * fail, and no method touches any plugin, server, or provider state.
 *
 * Parsing and registry mutation must happen on the server's primary thread,
 * because they call into provider code. containsPlaceholders and isActive are
 * pure and safe from any thread.
 */
class PlaceholderAPI : public endstone::Service {
public:
    /**
     * @brief The Endstone service name PAPI registers itself under.
     */
    static constexpr std::string_view ServiceName = "PlaceholderAPI";

    ~PlaceholderAPI() override = default;

    /**
     * @brief Whether this service is still usable.
     *
     * @return false once PAPI has begun shutting down
     */
    [[nodiscard]] virtual bool isActive() const noexcept = 0;

    /**
     * @brief Replaces every resolvable <code>{identifier_params}</code> in text.
     *
     * The text is scanned once, left to right. For each candidate the identifier
     * is the part before the first underscore, lowercased, and the parameters are
     * everything after it, preserved exactly. A placeholder is left untouched
     * when it is malformed, its identifier is not registered, or the expansion
     * returns no value or fails. Replacement text is never rescanned.
     *
     * @param player the player to resolve against; may be null
     * @param text the text to process
     * @return the processed text, or text unchanged if the service is inactive or
     *         this is not the primary thread
     */
    [[nodiscard]] virtual std::string setPlaceholders(const endstone::OfflinePlayer *player,
                                                      std::string_view text) const = 0;

    /**
     * @brief Replaces every resolvable <code>{rel_identifier_params}</code> in text.
     *
     * Only expansions that declare relational support are consulted, and only
     * through PlaceholderExpansion::onRelationalRequest. Ordinary placeholders are
     * not processed by this method.
     *
     * @param one the first player
     * @param two the second player
     * @param text the text to process
     * @return the processed text, or text unchanged if the service is inactive or
     *         this is not the primary thread
     */
    [[nodiscard]] virtual std::string setRelationalPlaceholders(const endstone::Player &one,
                                                                const endstone::Player &two,
                                                                std::string_view text) const = 0;

    /**
     * @brief Whether text lexically contains a placeholder-shaped substring.
     *
     * This is a cheap syntactic check for a <code>{</code> followed later by a
     * <code>}</code>. It does not verify that the identifier is valid, registered,
     * or resolvable, so <code>{}</code> and <code>{unknown_x}</code> both qualify.
     *
     * @param text the text to check
     * @return true if an opening brace has a later closing brace
     */
    [[nodiscard]] virtual bool containsPlaceholders(std::string_view text) const noexcept = 0;

    /**
     * @brief Whether an identifier currently has an expansion registered.
     *
     * @param identifier the identifier to check, matched case-insensitively
     * @return true if registered; false for invalid identifiers
     */
    [[nodiscard]] virtual bool isRegistered(std::string_view identifier) const = 0;

    /**
     * @brief All registered identifiers, sorted canonically.
     *
     * @return a snapshot of canonical lowercase identifiers
     */
    [[nodiscard]] virtual std::vector<std::string> getRegisteredIdentifiers() const = 0;

    /**
     * @brief Metadata for every registered expansion, sorted by identifier.
     *
     * Only value copies are returned. Expansion objects are never exposed.
     *
     * @return a snapshot of expansion metadata
     */
    [[nodiscard]] virtual std::vector<ExpansionInfo> getExpansions() const = 0;

    /**
     * @brief Registers an expansion on behalf of a plugin.
     *
     * The expansion's metadata, capabilities, and PlaceholderExpansion::canRegister
     * are validated first, then the registry commits atomically. Registration
     * fails without side effects if the identifier is invalid or already taken,
     * the metadata is empty, the owner is not enabled, a declared required plugin
     * is missing or disabled, the preflight refuses, or this is not the primary
     * thread.
     *
     * PAPI owns the expansion until it is unregistered. The owner may keep its own
     * reference but does not need to.
     *
     * @param owner the plugin registering the expansion; must be enabled
     * @param expansion the expansion to register; must not be null
     * @return true if the expansion is now registered
     */
    virtual bool registerExpansion(endstone::Plugin &owner, std::shared_ptr<PlaceholderExpansion> expansion) = 0;

    /**
     * @brief Unregisters one expansion owned by a plugin.
     *
     * A plugin cannot unregister another plugin's expansion.
     *
     * @param owner the plugin that registered the expansion
     * @param identifier the identifier to remove, matched case-insensitively
     * @return true if an expansion was removed
     */
    virtual bool unregisterExpansion(endstone::Plugin &owner, std::string_view identifier) = 0;

    /**
     * @brief Unregisters every expansion owned by a plugin.
     *
     * PAPI already does this automatically when the plugin is disabled; calling it
     * from the owner's onDisable is merely explicit.
     *
     * @param owner the plugin whose expansions should be removed
     * @return how many expansions were removed
     */
    virtual std::size_t unregisterExpansions(endstone::Plugin &owner) = 0;

    /**
     * @brief The historical single-callback placeholder signature.
     *
     * @deprecated Implement PlaceholderExpansion instead. A processor cannot report
     *             "unresolved", cannot supply metadata, and only ever receives an
     *             online Player.
     */
    using Processor = std::function<std::string(const endstone::Player *, std::string)>;

    /**
     * @brief Registers a bare callback as a placeholder.
     *
     * Provided only so plugins written against PAPI 0.0.1 keep compiling. The
     * callback is wrapped in an internal expansion that participates in normal
     * owner-aware lifecycle, so it is still released before its plugin unloads.
     *
     * Differences from registerExpansion: the callback cannot express "leave this
     * placeholder alone" because every return value is used verbatim, including
     * the empty string; and it is passed null whenever the resolved player is not
     * an online Player. Duplicate identifiers fail instead of being renamed into a
     * <code>plugin:identifier</code> namespace.
     *
     * @deprecated Implement PlaceholderExpansion and use registerExpansion.
     *
     * @param plugin the plugin registering the placeholder
     * @param identifier the identifier to register
     * @param processor the callback that produces the value
     * @return true if the placeholder was registered
     */
    [[deprecated(
        "Implement papi::PlaceholderExpansion and call registerExpansion instead.")]] [[nodiscard]] virtual bool
    registerPlaceholder(const endstone::Plugin &plugin, std::string_view identifier, Processor processor) const = 0;

    /**
     * @brief The regular expression PAPI 0.0.1 documented for placeholders.
     *
     * Returned unchanged for source compatibility. It describes neither the current
     * syntax nor the current implementation: parsing is a single non-regex scan and
     * identifier validation is stricter than this pattern.
     *
     * @deprecated Use containsPlaceholders, or match the documented
     *             <code>{identifier_params}</code> syntax directly.
     *
     * @return the historical pattern <code>[{]([^{}]+)[}]</code>
     */
    [[deprecated("The parser is not regex based; use containsPlaceholders instead.")]] [[nodiscard]] virtual std::string
    getPlaceholderPattern() const = 0;
};

}  // namespace papi
