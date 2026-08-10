#include "python/gil_safe_proxy.h"

#include <stdexcept>
#include <utility>

#include <pybind11/stl.h>

namespace py = pybind11;

namespace papi::python {
namespace {

/**
 * @brief Formats the active Python exception into a native string.
 *
 * The caller must hold the GIL. The traceback is turned into plain text here, while
 * Python is still safe to touch, so the message can be logged later without any
 * Python state outliving this call.
 */
[[nodiscard]] std::string describePythonError(py::error_already_set &error)
{
    try {
        // trace() renders the full traceback, which is what makes a provider bug
        // actually diagnosable.
        return error.trace() ? py::str(error.trace()).cast<std::string>() + "\n" + error.what() : error.what();
    }
    catch (...) {
        // Formatting itself failed; fall back to something rather than throwing from
        // an error path.
        return "Python error (traceback unavailable)";
    }
}

/**
 * @brief Exact-type check for ``str`` -- rejects subclasses.
 *
 * The frozen contract accepts *exact* ``str`` only.  ``py::isinstance`` accepts
 * subclasses, so we compare the type object directly.
 */
[[nodiscard]] bool isExactStr(const py::handle &value)
{
    return Py_TYPE(value.ptr()) == &PyUnicode_Type;
}

/**
 * @brief Converts a Python return value to an optional string, strictly.
 *
 * The caller must hold the GIL.
 *
 * @param value the value the override returned
 * @param error set when the value is neither None nor a str
 */
[[nodiscard]] std::optional<std::string> convertResult(const py::object &value, std::string &error)
{
    if (value.is_none()) {
        return std::nullopt;
    }
    if (!isExactStr(value)) {
        // Deliberately not coerced with str(): a wrong type is a provider bug, and
        // quietly stringifying it would hide the mistake.
        error = "expected str or None, got " + py::str(py::type::handle_of(value)).cast<std::string>();
        return std::nullopt;
    }
    // Copied into a native string while the GIL is held, so the result does not depend
    // on the Python object staying alive.
    return value.cast<std::string>();
}

}  // namespace

GilSafeExpansionProxy::GilSafeExpansionProxy(py::object handle, PlaceholderExpansion &expansion)
    : handle_(std::move(handle)), target_(&expansion)
{
    // Ownership-only: no metadata reads. The native core reads metadata through the
    // virtual methods, inside its canOperate gate and invokeProvider containment, so
    // off-thread or inactive-service registration performs zero provider callbacks and
    // a metadata exception surfaces as an atomic registration failure.
}

GilSafeExpansionProxy::~GilSafeExpansionProxy()  // NOLINT(bugprone-exception-escape)
{
    if (!handle_) {
        return;
    }
    // The registry may drop the last reference from anywhere, so the Python object is
    // released under the GIL rather than at whatever the ambient state happens to be.
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
        // A null player crosses as None; pybind11 does the conversion, so nothing is
        // dereferenced on the way in.
        const auto value = target_->onRequest(player, params);
        return value;
    }
    catch (py::error_already_set &error) {
        // Rethrown as a plain C++ exception so no Python state escapes into the
        // native error path, where the GIL is no longer guaranteed.
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
