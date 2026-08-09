#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "endstone_papi/placeholder_expansion.h"

namespace papi::python {

/**
 * @brief Wraps a Python-backed expansion so the native core can call it safely.
 *
 * The registry is native and knows nothing about Python. Every crossing therefore has
 * to be mediated: this proxy acquires the GIL before touching the wrapped object,
 * converts arguments and results, contains Python exceptions, and — critically —
 * releases its reference to the Python object under the GIL when the registry drops
 * it.
 *
 * Return values are deliberately strict. Only None or an exact str is accepted; None
 * means unresolved and anything else is reported as a provider error rather than
 * coerced with str(), because silently stringifying a wrong type hides bugs in the
 * expansion.
 *
 * Metadata and capabilities are read once during construction, on the thread that
 * registers the expansion, and cached. After that the registry can answer metadata
 * questions without entering Python at all.
 *
 * Everything native is wrapped the same way, including a native expansion that happens
 * to be handed in from Python. The extra GIL acquisition costs a little but removes an
 * entire class of "which side owns this" mistakes.
 */
class GilSafeExpansionProxy final : public PlaceholderExpansion {
public:
    /**
     * @brief Reads and caches the wrapped expansion's metadata.
     *
     * @param handle the Python object implementing the expansion contract
     * @param expansion the same object viewed as the native contract; borrowed for
     *        construction only
     *
     * The caller must hold the GIL. Any Python exception raised while reading metadata
     * propagates as py::error_already_set so registration can fail cleanly.
     */
    GilSafeExpansionProxy(pybind11::object handle, PlaceholderExpansion &expansion);

    ~GilSafeExpansionProxy() override;

    [[nodiscard]] std::string getIdentifier() const override { return identifier_; }
    [[nodiscard]] std::string getAuthor() const override { return author_; }
    [[nodiscard]] std::string getVersion() const override { return version_; }
    [[nodiscard]] std::string getName() const override { return name_; }
    [[nodiscard]] std::optional<std::string> getRequiredPlugin() const override { return required_plugin_; }
    [[nodiscard]] bool canRegister() const override { return can_register_; }
    [[nodiscard]] bool supportsRelationalPlaceholders() const override { return relational_; }
    [[nodiscard]] bool supportsPlayerCleanup() const override { return player_cleanup_; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       std::string_view params) override;
    [[nodiscard]] std::optional<std::string> onRelationalRequest(const endstone::Player &one,
                                                                 const endstone::Player &two,
                                                                 std::string_view params) override;
    void onPlayerQuit(const endstone::Player &player) override;
    void onUnregister(UnregisterReason reason) override;

private:
    /**
     * @brief The wrapped object, viewed natively. Null once released.
     *
     * Only valid while handle_ is alive, so both are read together under the GIL.
     */
    [[nodiscard]] PlaceholderExpansion *target() const noexcept { return target_; }

    pybind11::object handle_;
    PlaceholderExpansion *target_ = nullptr;

    std::string identifier_;
    std::string author_;
    std::string version_;
    std::string name_;
    std::optional<std::string> required_plugin_;
    bool can_register_ = true;
    bool relational_ = false;
    bool player_cleanup_ = false;
};

}  // namespace papi::python
