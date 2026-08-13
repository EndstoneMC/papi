#pragma once

#include <string_view>

#include <endstone/event/event.h>
#include <endstone/logger.h>
#include <endstone/plugin/plugin.h>

namespace papi::detail {

/**
 * @brief The narrow slice of Endstone the registry and service depend on.
 *
 * Splitting this out keeps ownership, threading, and dependency policy testable
 * without a running Bedrock server, and makes every platform call site explicit
 * and auditable. The production implementation forwards to endstone::Server.
 *
 * Implementations must be callable from any thread for isPrimaryThread and
 * logging; the remaining operations are only invoked on the primary thread.
 */
class Platform {
public:
    virtual ~Platform() = default;

    /**
     * @brief Whether the calling thread is the server's primary thread.
     */
    [[nodiscard]] virtual bool isPrimaryThread() const = 0;

    /**
     * @brief Whether a plugin is currently loaded and enabled.
     *
     * Plugin names are matched case-sensitively, as Endstone does.
     */
    [[nodiscard]] virtual bool isPluginEnabled(std::string_view name) const = 0;

    /**
     * @brief Whether this exact plugin object is currently loaded and enabled.
     */
    [[nodiscard]] virtual bool isPluginEnabled(const endstone::Plugin &plugin) const = 0;

    /**
     * @brief Logs a message on PAPI's behalf.
     *
     * Never called while the registry lock is held.
     */
    virtual void log(endstone::Logger::Level level, std::string_view message) = 0;

    /**
     * @brief Dispatches a synchronous Endstone event.
     *
     * Never called while the registry lock is held. Implementations may drop the
     * call once the event system is no longer available.
     */
    virtual void callEvent(endstone::Event &event) = 0;
};

}  // namespace papi::detail
