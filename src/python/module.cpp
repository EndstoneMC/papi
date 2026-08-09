#include <memory>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "endstone_papi/placeholder_api.h"
#include "endstone_papi/version.h"

#include "platform/endstone/bootstrap.h"

namespace py = pybind11;

namespace {

/**
 * @brief Owns the native service on behalf of the Python PAPI plugin.
 */
class PapiHost {
public:
    bool start(endstone::Plugin &plugin) { return bootstrap_.start(plugin); }
    void stop() { bootstrap_.stop(); }
    [[nodiscard]] std::shared_ptr<papi::PlaceholderAPI> getService() const { return bootstrap_.getService(); }

private:
    papi::detail::PapiBootstrap bootstrap_;
};

}  // namespace

PYBIND11_MODULE(_papi, m)
{
    m.doc() = "Native core of the Endstone PlaceholderAPI framework.";

    py::module_::import("endstone");
    py::module_::import("endstone.plugin");

    m.attr("__version__") = std::string(papi::getVersion());
    m.attr("SERVICE_NAME") = std::string(papi::PlaceholderAPI::ServiceName);

    // The service is native and final: Python consumes it but never implements it.
    py::class_<papi::PlaceholderAPI, endstone::Service, std::shared_ptr<papi::PlaceholderAPI>>(
        m, "PlaceholderAPI", "Resolves placeholders through registered expansions.", py::is_final())
        .def_property_readonly("active", &papi::PlaceholderAPI::isActive, "Whether this service is still usable.")
        .def("set_placeholders", &papi::PlaceholderAPI::setPlaceholders, py::arg("player"), py::arg("text"),
             "Replaces every resolvable {identifier_params} in text.")
        .def("set_relational_placeholders", &papi::PlaceholderAPI::setRelationalPlaceholders, py::arg("one"),
             py::arg("two"), py::arg("text"), "Replaces every resolvable {rel_identifier_params} in text.")
        .def("contains_placeholders", &papi::PlaceholderAPI::containsPlaceholders, py::arg("text"),
             "Whether text lexically contains a placeholder-shaped substring.")
        .def("is_registered", &papi::PlaceholderAPI::isRegistered, py::arg("identifier"),
             "Whether an identifier currently has an expansion registered.")
        .def_property_readonly("registered_identifiers", &papi::PlaceholderAPI::getRegisteredIdentifiers,
                               "Every registered identifier, sorted canonically.")
        .def_property_readonly("expansions", &papi::PlaceholderAPI::getExpansions,
                               "Metadata for every registered expansion, sorted by identifier.")
        .def("unregister_expansion", &papi::PlaceholderAPI::unregisterExpansion, py::arg("owner"),
             py::arg("identifier"), "Unregisters one expansion owned by a plugin.")
        .def("unregister_expansions", &papi::PlaceholderAPI::unregisterExpansions, py::arg("owner"),
             "Unregisters every expansion owned by a plugin.");

    // Private bootstrap surface. Only the PAPI plugin uses it.
    py::class_<PapiHost>(m, "_PapiHost", "Internal: owns the native PlaceholderAPI service.")
        .def(py::init<>())
        .def("start", &PapiHost::start, py::arg("plugin"),
             "Internal: builds the service and registers it with Endstone.")
        .def("stop", &PapiHost::stop, "Internal: tears the service down. Idempotent.")
        .def_property_readonly("service", &PapiHost::getService, "Internal: the published service, or None.");
}
