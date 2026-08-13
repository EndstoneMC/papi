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

// Bounds re-entry when a Python override calls its bound base member.
class MemberDispatchGuard {
public:
    MemberDispatchGuard(PyObject *self, std::string_view name)
    {
        auto &stack = activeStack();
        for (const auto &entry : stack) {
            if (entry.first == self && entry.second == name) {
                return;
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

// Search only the subclass portion of the MRO to avoid bound-base re-entry.
[[nodiscard]] py::object subclassMember(const py::handle &self, const char *name)
{
    const auto base = py::type::of<PlaceholderExpansion>();
    const auto mro = py::type::handle_of(self).attr("__mro__");

    for (const auto cls : mro) {
        if (cls.ptr() == base.ptr()) {
            break;
        }
        const auto dict = cls.attr("__dict__");
        if (PyMapping_HasKeyString(dict.ptr(), name) == 1) {
            return py::getattr(self, name);
        }
    }
    return {};
}

// Python str subclasses are not valid provider results.
[[nodiscard]] bool isExactStr(const py::handle &value)
{
    return Py_TYPE(value.ptr()) == &PyUnicode_Type;
}

// The caller must hold the GIL.
[[nodiscard]] std::optional<std::string> asOptionalString(const py::object &value, const char *method)
{
    if (value.is_none()) {
        return std::nullopt;
    }
    if (!isExactStr(value)) {
        throw std::runtime_error(std::string(method) + " must return str or None, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<std::string>();
}

// The caller must hold the GIL.
[[nodiscard]] std::string requiredString(const py::handle &self, const char *name)
{
    const auto value = subclassMember(self, name);
    if (!value) {
        throw std::runtime_error(std::string("expansion must define '") + name + "'");
    }
    if (!isExactStr(value)) {
        throw std::runtime_error(std::string("expansion property '") + name + "' must be str, got " +
                                 py::str(py::type::handle_of(value)).cast<std::string>());
    }
    return value.cast<std::string>();
}

// The caller must hold the GIL.
[[nodiscard]] bool booleanCall(const py::handle &self, const char *name, const bool fallback)
{
    const auto member = subclassMember(self, name);
    if (!member) {
        return fallback;
    }
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
        return getIdentifier();
    }
    const auto value = subclassMember(self, "name");
    if (!value || value.is_none()) {
        return requiredString(self, "identifier");
    }
    if (!isExactStr(value)) {
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
    if (!isExactStr(value)) {
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
        throw std::runtime_error("expansion on_request called super().on_request, but the base class "
                                 "does not define it");
    }
    const auto method = subclassMember(self, "on_request");
    if (!method) {
        throw std::runtime_error("expansion must implement on_request");
    }
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
