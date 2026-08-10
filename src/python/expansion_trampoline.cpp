#include "python/expansion_trampoline.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/stl.h>

namespace py = pybind11;

namespace papi::python {
namespace {

/**
 * @brief Thread-local guard against same-member re-entry in the trampoline.
 *
 * A Python override that calls ``super().member`` re-enters the pybind base
 * property/method, which dispatches back into this trampoline. Without a guard
 * this is unbounded native/Python recursion that exhausts memory in seconds.
 *
 * The guard tracks a thread-local stack of (PyObject*, member name) pairs. On
 * re-entry - same object and same member already active - it signals the caller
 * to short-circuit: return the C++ default (for methods with one) or throw a
 * contained error (for pure-virtual members that have no default).
 *
 * The guard must live in the trampoline method, not in ``subclassMember``,
 * because re-entry happens when the Python override is *invoked* - after
 * ``subclassMember`` has already returned. RAII pops the entry on destruction,
 * even if an exception propagates.
 */
class MemberDispatchGuard {
public:
    MemberDispatchGuard(PyObject *self, std::string_view name)
    {
        auto &stack = activeStack();
        for (const auto &entry : stack) {
            if (entry.first == self && entry.second == name) {
                return;  // Re-entry: do not push; signal via reEntered().
            }
        }
        stack.emplace_back(self, name);
        pushed_ = true;
    }

    ~MemberDispatchGuard()
    {
        if (pushed_) {
            activeStack().pop_back();
        }
    }

    MemberDispatchGuard(const MemberDispatchGuard &) = delete;
    MemberDispatchGuard &operator=(const MemberDispatchGuard &) = delete;

    [[nodiscard]] bool reEntered() const noexcept { return !pushed_; }

private:
    using Stack = std::vector<std::pair<PyObject *, std::string_view>>;

    static Stack &activeStack()
    {
        thread_local Stack stack;
        return stack;
    }

    bool pushed_ = false;
};

/**
 * @brief Finds a member the Python subclass actually defines.
 *
 * This must not be written as a plain getattr. Every member below is also bound on the
 * base class as a property or method that dispatches straight back into this
 * trampoline, so reading it from the instance would re-enter the same function and
 * recurse until the process runs out of memory.
 *
 * The lookup therefore walks the type's MRO and stops at the pybind base: only classes
 * ahead of it can be genuine overrides. Resolving a descriptor from the type rather
 * than the instance also means nothing is invoked while searching.
 *
 * The caller must hold the GIL.
 *
 * @return the override, or an empty object when only the base defines the member
 */
[[nodiscard]] py::object subclassMember(const py::handle &self, const char *name)
{
    const auto base = py::type::of<PlaceholderExpansion>();
    const auto mro = py::type::handle_of(self).attr("__mro__");

    for (const auto cls : mro) {
        if (cls.ptr() == base.ptr()) {
            // Reached the binding itself; everything past it is pybind and object
            // plumbing, so the subclass did not define this member.
            break;
        }
        const auto dict = cls.attr("__dict__");
        if (PyMapping_HasKeyString(dict.ptr(), name) == 1) {
            // Safe now: the subclass definition shadows the base, so resolving it
            // cannot come back here - unless the override itself calls super().member,
            // which the MemberDispatchGuard in each trampoline method contains.
            return py::getattr(self, name);
        }
    }
    return {};
}

/**
 * @brief Interprets what a Python override returned as an optional value.
 *
 * The caller must hold the GIL.
 *
 * Only None or an exact str is accepted. A wrong type raises, because coercing it with
 * str() would turn a provider bug into plausible-looking output.
 */
[[nodiscard]] std::optional<std::string> asOptionalString(const py::object &value, const char *method)
{
    if (value.is_none()) {
        return std::nullopt;
    }
    if (!py::isinstance<py::str>(value)) {
        throw std::runtime_error(std::string(method) + " must return str or None, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    // Copied while the GIL is held, so the result does not depend on the Python object.
    return value.cast<std::string>();
}

/**
 * @brief Reads a required string member the subclass must define.
 *
 * The caller must hold the GIL.
 */
[[nodiscard]] std::string requiredString(const py::handle &self, const char *name)
{
    const auto value = subclassMember(self, name);
    if (!value) {
        throw std::runtime_error(std::string("expansion must define '") + name + "'");
    }
    if (!py::isinstance<py::str>(value)) {
        throw std::runtime_error(std::string("expansion property '") + name + "' must be str, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<std::string>();
}

/**
 * @brief Reads a boolean capability, falling back when the subclass is silent.
 *
 * The caller must hold the GIL.
 */
[[nodiscard]] bool booleanCall(const py::handle &self, const char *name, const bool fallback)
{
    const auto member = subclassMember(self, name);
    if (!member) {
        return fallback;
    }
    // Tolerate both an overriding method and a plain class attribute, since either
    // expresses the capability clearly in Python.
    const auto value = py::isinstance<py::bool_>(member) ? member : member();
    if (!py::isinstance<py::bool_>(value)) {
        throw std::runtime_error(std::string("expansion method '") + name + "' must return bool, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<bool>();
}

}  // namespace

std::string PyPlaceholderExpansion::getIdentifier() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "identifier");
    if (guard.reEntered()) {
        // Pure virtual: no C++ default. The override called super().identifier, which
        // is not defined on the base class. Contain as a provider error.
        throw std::runtime_error("expansion 'identifier' called super().identifier, but the base class "
                                 "does not define it");
    }
    return requiredString(self, "identifier");
}

std::string PyPlaceholderExpansion::getAuthor() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "author");
    if (guard.reEntered()) {
        throw std::runtime_error("expansion 'author' called super().author, but the base class "
                                 "does not define it");
    }
    return requiredString(self, "author");
}

std::string PyPlaceholderExpansion::getVersion() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "version");
    if (guard.reEntered()) {
        throw std::runtime_error("expansion 'version' called super().version, but the base class "
                                 "does not define it");
    }
    return requiredString(self, "version");
}

std::string PyPlaceholderExpansion::getName() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "name");
    if (guard.reEntered()) {
        // super().name re-entered: fall back to the identifier, same as the C++ default.
        return getIdentifier();
    }
    const auto value = subclassMember(self, "name");
    if (!value || value.is_none()) {
        // No override, or an explicit None: fall back to the identifier.
        return requiredString(self, "identifier");
    }
    if (!py::isinstance<py::str>(value)) {
        throw std::runtime_error("expansion property 'name' must be str or None, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<std::string>();
}

std::optional<std::string> PyPlaceholderExpansion::getRequiredPlugin() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "required_plugin");
    if (guard.reEntered()) {
        return std::nullopt;
    }
    const auto value = subclassMember(self, "required_plugin");
    if (!value || value.is_none()) {
        return std::nullopt;
    }
    if (!py::isinstance<py::str>(value)) {
        throw std::runtime_error("expansion property 'required_plugin' must be str or None, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<std::string>();
}

bool PyPlaceholderExpansion::canRegister() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "can_register");
    if (guard.reEntered()) {
        return true;
    }
    return booleanCall(self, "can_register", true);
}

bool PyPlaceholderExpansion::supportsRelationalPlaceholders() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "supports_relational_placeholders");
    if (guard.reEntered()) {
        return false;
    }
    return booleanCall(self, "supports_relational_placeholders", false);
}

bool PyPlaceholderExpansion::supportsPlayerCleanup() const
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "supports_player_cleanup");
    if (guard.reEntered()) {
        return false;
    }
    return booleanCall(self, "supports_player_cleanup", false);
}

std::optional<std::string> PyPlaceholderExpansion::onRequest(const endstone::OfflinePlayer *player,
                                                             const std::string_view params)
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "on_request");
    if (guard.reEntered()) {
        // No C++ default: the override called super().on_request, which is not defined.
        throw std::runtime_error("expansion on_request called super().on_request, but the base class "
                                 "does not define it");
    }
    const auto method = subclassMember(self, "on_request");
    if (!method) {
        throw std::runtime_error("expansion must implement on_request");
    }
    // A null player is passed to pybind11 as a pointer and arrives in Python as None,
    // so nothing is dereferenced on the way in.
    const auto result = method(py::cast(player), py::str(std::string(params)));
    return asOptionalString(result, "on_request");
}

std::optional<std::string> PyPlaceholderExpansion::onRelationalRequest(const endstone::Player &one,
                                                                       const endstone::Player &two,
                                                                       const std::string_view params)
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "on_relational_request");
    if (guard.reEntered()) {
        return std::nullopt;
    }
    const auto method = subclassMember(self, "on_relational_request");
    if (!method) {
        return std::nullopt;
    }
    const auto result = method(py::cast(&one), py::cast(&two), py::str(std::string(params)));
    return asOptionalString(result, "on_relational_request");
}

void PyPlaceholderExpansion::onPlayerQuit(const endstone::Player &player)
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "on_player_quit");
    if (guard.reEntered()) {
        return;
    }
    const auto method = subclassMember(self, "on_player_quit");
    if (!method) {
        return;
    }
    method(py::cast(&player));
}

void PyPlaceholderExpansion::onUnregister(const UnregisterReason reason)
{
    const py::gil_scoped_acquire gil;
    const auto self = py::cast(this);
    const MemberDispatchGuard guard(self.ptr(), "on_unregister");
    if (guard.reEntered()) {
        return;
    }
    const auto method = subclassMember(self, "on_unregister");
    if (!method) {
        return;
    }
    method(py::cast(reason));
}

}  // namespace papi::python
