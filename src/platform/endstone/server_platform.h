#pragma once

#include <endstone/server.h>

#include "core/platform.h"

namespace papi::detail {

/**
 * @brief The production Platform, forwarding to a live Endstone server.
 *
 * The server reference stays valid while PAPI is enabled. It is cleared at shutdown
 * so a retained inert service can never reach the platform again.
 */
class ServerPlatform final : public Platform {
public:
    explicit ServerPlatform(endstone::Server &server, endstone::Logger &logger);

    [[nodiscard]] bool isPrimaryThread() const override;
    [[nodiscard]] bool isPluginEnabled(std::string_view name) const override;
    [[nodiscard]] bool isPluginEnabled(const endstone::Plugin &plugin) const override;
    [[nodiscard]] const endstone::Player *getOnlinePlayer(endstone::UUID id) const override;
    void log(endstone::Logger::Level level, std::string_view message) override;
    void callEvent(endstone::Event &event) override;

    /**
     * @brief Drops the server and logger references.
     *
     * After this every operation is a safe no-op. Called during PAPI shutdown so a
     * consumer holding an old service cannot reach a torn-down server.
     */
    void detach();

private:
    endstone::Server *server_;
    endstone::Logger *logger_;
};

}  // namespace papi::detail
