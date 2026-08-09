#pragma once

#include <cstdint>
#include <string_view>

namespace papi {

/**
 * @brief Why an expansion was removed from the registry.
 *
 * The reason is passed to PlaceholderExpansion::onUnregister and copied into the
 * unregister event so listeners can distinguish deliberate removal from
 * lifecycle-driven teardown.
 */
enum class UnregisterReason : std::uint8_t {
    /**
     * @brief The owner asked PAPI to unregister the expansion.
     */
    Explicit = 0,
    /**
     * @brief The plugin that registered the expansion was disabled.
     */
    OwnerDisabled = 1,
    /**
     * @brief The plugin the expansion declared as required was disabled.
     */
    RequiredPluginDisabled = 2,
    /**
     * @brief PAPI itself is shutting down.
     */
    PapiShutdown = 3,
};

/**
 * @brief A stable, human-readable name for an UnregisterReason.
 */
[[nodiscard]] constexpr std::string_view toString(const UnregisterReason reason) noexcept
{
    switch (reason) {
    case UnregisterReason::Explicit:
        return "Explicit";
    case UnregisterReason::OwnerDisabled:
        return "OwnerDisabled";
    case UnregisterReason::RequiredPluginDisabled:
        return "RequiredPluginDisabled";
    case UnregisterReason::PapiShutdown:
        return "PapiShutdown";
    }
    return "Unknown";
}

}  // namespace papi
