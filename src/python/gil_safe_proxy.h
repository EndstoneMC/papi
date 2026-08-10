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
 * converts arguments and results, contains Python exceptions, and - critically -
 * releases its reference to the Python object under the GIL when the registry drops
 * it.
 *
 * Return values are deliberately strict. Only None or an exact str is accepted; None
 * means unresolved and anything else is reported as a provider error rather than
 * coerced with str(), because silently stringifying a wrong type hides bugs in the
 * expansion.
 *
 * The constructor is ownership-only: it stores the Python handle and the borrowed
 * native pointer but never calls into Python. Metadata and capabilities are read
 * lazily through the virtual methods, which acquire the GIL and forward to the
 * target. This places every Python entry point - metadata, preflight, and callbacks
 * - behind the native core's canOperate gate and invokeProvider exception boundary,
 * so off-thread or inactive-service registration performs zero provider callbacks
 * and a metadata exception is contained as an atomic registration failure.
 *
 * Everything native is wrapped the same way, including a native expansion that happens
 * to be handed in from Python. The extra GIL acquisition costs a little but removes an
 * entire class of "which side owns this" mistakes.
 */
class GilSafeExpansionProxy final : public PlaceholderExpansion {
public:
    /**
     * @brief Stores the wrapped expansion; performs no Python calls.
     *
     * @param handle the Python object implementing the expansion contract
     * @param expansion the same object viewed as the native contract; borrowed for
     *        construction only
     *
     * The caller must hold the GIL so the Python reference can be taken safely, but no
     * metadata is read here. The native core reads metadata through the virtual
     * methods, inside its own gate and exception-containment boundary.
     */
    GilSafeExpansionProxy(pybind11::object handle, PlaceholderExpansion &expansion);

    ~GilSafeExpansionProxy() override;

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

private:
    /**
     * @brief The wrapped object, viewed natively. Null once released.
     *
     * Only valid while handle_ is alive, so both are read together under the GIL.
     */
    [[nodiscard]] PlaceholderExpansion *target() const noexcept { return target_; }

    pybind11::object handle_;
    PlaceholderExpansion *target_ = nullptr;
};

}  // namespace papi::python
