#pragma once

#include <string_view>

#include <endstone/event/event.h>
#include <endstone/logger.h>
#include <endstone/plugin/plugin.h>

namespace papi::detail {

// Endstone operations used by the native core.
class Platform {
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual bool isPrimaryThread() const = 0;

    [[nodiscard]] virtual bool isPluginEnabled(std::string_view name) const = 0;

    [[nodiscard]] virtual bool isPluginEnabled(const endstone::Plugin &plugin) const = 0;

    // Never called while the registry lock is held.
    virtual void log(endstone::Logger::Level level, std::string_view message) = 0;

    // Never called while the registry lock is held.
    virtual void callEvent(endstone::Event &event) = 0;
};

}  // namespace papi::detail
