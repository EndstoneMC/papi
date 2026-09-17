#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <endstone/offline_player.h>
#include <endstone/player.h>

#include "endstone_papi/unregister_reason.h"

namespace papi {

/**
 * @brief The extension point implemented by placeholder providers.
 *
 * Both C++ and Python providers implement this same contract and share one
 * registry. An expansion is registered through PlaceholderAPI::registerExpansion
 * and owned by PAPI until it is unregistered, its owner is disabled, or PAPI
 * shuts down.
 *
 * Metadata and capabilities are queried once during registration and copied.
 * They are never re-queried, so an implementation must report stable values.
 *
 * Arguments passed to callbacks are borrowed for the duration of the call only.
 * An implementation must not retain the player reference or store it for later.
 *
 * All callbacks are invoked on the server's primary thread and never while PAPI
 * holds an internal lock.
 */
class PlaceholderExpansion {
public:
    PlaceholderExpansion() = default;
    PlaceholderExpansion(const PlaceholderExpansion &) = delete;
    PlaceholderExpansion &operator=(const PlaceholderExpansion &) = delete;
    virtual ~PlaceholderExpansion() = default;

    /**
     * @brief The identifier this expansion answers to.
     *
     * Must match <code>[A-Za-z0-9][A-Za-z0-9-]*</code>. It is canonicalized to
     * ASCII lowercase, so <code>Demo</code> and <code>demo</code> are the same
     * identifier and cannot both be registered. Dot separates the identifier from
     * parameters, while underscore is available inside parameters, so neither is
     * permitted in an identifier.
     *
     * @return the identifier, queried once at registration
     */
    [[nodiscard]] virtual std::string getIdentifier() const = 0;

    /**
     * @brief The author of this expansion.
     *
     * @return a non-empty author string, queried once at registration
     */
    [[nodiscard]] virtual std::string getAuthor() const = 0;

    /**
     * @brief The version of this expansion.
     *
     * @return a non-empty version string, queried once at registration
     */
    [[nodiscard]] virtual std::string getVersion() const = 0;

    /**
     * @brief The display name of this expansion.
     *
     * @return the display name; defaults to the identifier
     */
    [[nodiscard]] virtual std::string getName() const { return getIdentifier(); }

    /**
     * @brief The plugin this expansion requires in order to function.
     *
     * The name is matched case-sensitively against Endstone's loaded plugins.
     * The plugin must exist and be enabled at registration time, and the
     * expansion is removed automatically if that plugin is later disabled.
     *
     * @return the required plugin name, or nullopt if the expansion has none
     */
    [[nodiscard]] virtual std::optional<std::string> getRequiredPlugin() const { return std::nullopt; }

    /**
     * @brief Final chance to refuse registration.
     *
     * Called once before the registry commits, after metadata and dependency
     * validation succeed. Returning false aborts registration cleanly.
     *
     * @return true to allow registration
     */
    [[nodiscard]] virtual bool canRegister() const { return true; }

    /**
     * @brief Whether this expansion answers relational placeholders.
     *
     * Declaring the capability explicitly avoids a cast across the plugin
     * boundary. When false, onRelationalRequest is never called.
     *
     * @return true if onRelationalRequest is implemented
     */
    [[nodiscard]] virtual bool supportsRelationalPlaceholders() const { return false; }

    /**
     * @brief Whether this expansion wants to be told when a player quits.
     *
     * When false, onPlayerQuit is never called.
     *
     * @return true if onPlayerQuit is implemented
     */
    [[nodiscard]] virtual bool supportsPlayerCleanup() const { return false; }

    /**
     * @brief Resolves an ordinary placeholder.
     *
     * @param player the player the placeholder is being resolved against; may be
     *        null, and may be an offline player that is not currently online
     * @param params everything after the first colon of the placeholder, preserved
     *        byte for byte including underscores, dots, later colons, case and spaces; empty when the
     *        placeholder was written as <code>{identifier:}</code>
     * @return the replacement value, or nullopt to leave the original
     *         placeholder text untouched
     */
    [[nodiscard]] virtual std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                               std::string_view params) = 0;

    /**
     * @brief Resolves a relational placeholder between two online players.
     *
     * Only called when supportsRelationalPlaceholders returns true.
     *
     * @param one the first player
     * @param two the second player
     * @param params everything after the second colon in
     *        <code>{rel:identifier:params}</code>, preserved byte for byte
     * @return the replacement value, or nullopt to leave the original
     *         placeholder text untouched
     */
    [[nodiscard]] virtual std::optional<std::string> onRelationalRequest(const endstone::Player &one,
                                                                         const endstone::Player &two,
                                                                         std::string_view params)
    {
        (void)one;
        (void)two;
        (void)params;
        return std::nullopt;
    }

    /**
     * @brief Notifies the expansion that a player left the server.
     *
     * Only called when supportsPlayerCleanup returns true. This does not change
     * the expansion's registration.
     *
     * @param player the player who quit
     */
    virtual void onPlayerQuit(const endstone::Player &player) { (void)player; }

    /**
     * @brief Notifies the expansion that it is no longer registered.
     *
     * Called exactly once, after the expansion has already become invisible to
     * lookups and after any in-flight callback has returned.
     *
     * @param reason why the expansion was removed
     */
    virtual void onUnregister(UnregisterReason reason) { (void)reason; }
};

}  // namespace papi
