#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>
#include <pybind11/trampoline_self_life_support.h>

#include "endstone_papi/placeholder_expansion.h"

namespace papi::python {

/**
 * @brief The native side of a Python PlaceholderExpansion subclass.
 *
 * Python subclasses of the expansion contract are represented in the one native
 * registry through this trampoline, so a consumer never learns which language a
 * provider was written in.
 *
 * It inherits py::trampoline_self_life_support because the registry can hold the last
 * reference to a Python-derived object: that base keeps the Python instance alive for
 * as long as the C++ side owns it, and releases it under the GIL.
 *
 * Overrides are dispatched by hand rather than with PYBIND11_OVERRIDE so that a wrong
 * return type produces a specific, actionable error. Only None or an exact str is
 * accepted; anything else is a provider error rather than something silently coerced.
 */
class PyPlaceholderExpansion final : public PlaceholderExpansion, public pybind11::trampoline_self_life_support {
public:
    using PlaceholderExpansion::PlaceholderExpansion;

    [[nodiscard]] std::string getIdentifier() const override;
    [[nodiscard]] std::string getAuthor() const override;
    [[nodiscard]] std::string getVersion() const override;
    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::optional<std::string> getRequiredPlugin() const override;
    [[nodiscard]] bool canRegister() const override;
    [[nodiscard]] bool supportsRelationalPlaceholders() const override;
    [[nodiscard]] bool supportsPlayerCleanup() const override;

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       std::string_view params) override;
    [[nodiscard]] std::optional<std::string> onRelationalRequest(const endstone::Player &one,
                                                                 const endstone::Player &two,
                                                                 std::string_view params) override;
    void onPlayerQuit(const endstone::Player &player) override;
    void onUnregister(UnregisterReason reason) override;
};

}  // namespace papi::python
