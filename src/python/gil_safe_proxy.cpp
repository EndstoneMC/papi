#include "python/gil_safe_proxy.h"

#include <stdexcept>
#include <utility>

#include <pybind11/stl.h>

namespace py = pybind11;

namespace papi::python {
namespace {

// The caller must hold the GIL.
[[nodiscard]] std::string describePythonError(py::error_already_set &error)
{
    try {
        return error.trace() ? py::str(error.trace()).cast<std::string>() + "\n" + error.what() : error.what();
    }
    catch (...) {
        return "Python error (traceback unavailable)";
    }
}

// Python str subclasses are not valid provider results.
[[nodiscard]] bool isExactStr(const py::handle &value)
{
    return Py_TYPE(value.ptr()) == &PyUnicode_Type;
}

// The caller must hold the GIL.
[[nodiscard]] std::optional<std::string> convertResult(const py::object &value, std::string &error)
{
    if (value.is_none()) {
        return std::nullopt;
    }
    if (!isExactStr(value)) {
        error = "expected str or None, got " + py::str(py::type::handle_of(value)).cast<std::string>();
        return std::nullopt;
    }
    return value.cast<std::string>();
}

}  // namespace

GilSafeExpansionProxy::GilSafeExpansionProxy(py::object handle, PlaceholderExpansion &expansion)
    : handle_(std::move(handle)), target_(&expansion)
{
    // Construction must not invoke provider metadata.
}

GilSafeExpansionProxy::~GilSafeExpansionProxy()  // NOLINT(bugprone-exception-escape)
{
    if (!handle_) {
        return;
    }
    // Final Python ownership release requires the GIL.
    const py::gil_scoped_acquire gil;
    target_ = nullptr;
    handle_ = py::object();
}

std::string GilSafeExpansionProxy::getIdentifier() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return {};
    }
    try {
        return target_->getIdentifier();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

std::string GilSafeExpansionProxy::getAuthor() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return {};
    }
    try {
        return target_->getAuthor();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

std::string GilSafeExpansionProxy::getVersion() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return {};
    }
    try {
        return target_->getVersion();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

std::string GilSafeExpansionProxy::getName() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return {};
    }
    try {
        return target_->getName();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

std::optional<std::string> GilSafeExpansionProxy::getRequiredPlugin() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return std::nullopt;
    }
    try {
        return target_->getRequiredPlugin();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

bool GilSafeExpansionProxy::canRegister() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return false;
    }
    try {
        return target_->canRegister();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

bool GilSafeExpansionProxy::supportsRelationalPlaceholders() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return false;
    }
    try {
        return target_->supportsRelationalPlaceholders();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

bool GilSafeExpansionProxy::supportsPlayerCleanup() const
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return false;
    }
    try {
        return target_->supportsPlayerCleanup();
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

std::optional<std::string> GilSafeExpansionProxy::onRequest(const endstone::OfflinePlayer *player,
                                                            const std::string_view params)
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return std::nullopt;
    }

    try {
        const auto value = target_->onRequest(player, params);
        return value;
    }
    catch (py::error_already_set &error) {
        // Python state must not escape the GIL-protected boundary.
        throw std::runtime_error(describePythonError(error));
    }
}

std::optional<std::string> GilSafeExpansionProxy::onRelationalRequest(const endstone::Player &one,
                                                                      const endstone::Player &two,
                                                                      const std::string_view params)
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return std::nullopt;
    }

    try {
        return target_->onRelationalRequest(one, two, params);
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

void GilSafeExpansionProxy::onPlayerQuit(const endstone::Player &player)
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return;
    }

    try {
        target_->onPlayerQuit(player);
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

void GilSafeExpansionProxy::onUnregister(const UnregisterReason reason)
{
    const py::gil_scoped_acquire gil;
    if (!handle_ || !target_) {
        return;
    }

    try {
        target_->onUnregister(reason);
    }
    catch (py::error_already_set &error) {
        throw std::runtime_error(describePythonError(error));
    }
}

}  // namespace papi::python
