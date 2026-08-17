#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <endstone/plugin/plugin_description.h>
#include <endstone/plugin/service_manager.h>
#include <pybind11/native_enum.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "endstone_papi/events.h"
#include "endstone_papi/expansion_info.h"
#include "endstone_papi/placeholder_api.h"
#include "endstone_papi/placeholder_expansion.h"
#include "endstone_papi/unregister_reason.h"
#include "endstone_papi/version.h"

#include "platform/endstone/bootstrap.h"
#include "platform/endstone/service_publication.h"
#include "python/expansion_trampoline.h"
#include "python/gil_safe_proxy.h"

#ifdef PAPI_TEST_BINDINGS
#include "fake_player.h"
#endif

namespace py = pybind11;

namespace {

// Python-backed providers require GIL-safe callbacks and final release.
bool registerExpansionFromPython(papi::PlaceholderAPI &service, endstone::Plugin &owner, const py::object &expansion)
{
    auto *native = expansion.cast<papi::PlaceholderExpansion *>();
    if (native == nullptr) {
        throw py::type_error("expansion must be a PlaceholderExpansion");
    }

    auto proxy = std::make_shared<papi::python::GilSafeExpansionProxy>(expansion, *native);

    // Provider callbacks reacquire the GIL through the proxy.
    const py::gil_scoped_release release;
    return service.registerExpansion(owner, std::move(proxy));
}

#ifdef PAPI_TEST_BINDINGS
// In-memory platform for Python binding tests.
class TestPlatform final : public papi::detail::Platform {
public:
    [[nodiscard]] bool isPrimaryThread() const override { return true; }
    [[nodiscard]] bool isPluginEnabled(std::string_view /*name*/) const override { return true; }
    [[nodiscard]] bool isPluginEnabled(const endstone::Plugin & /*plugin*/) const override { return true; }

    void log(const endstone::Logger::Level level, std::string_view message) override
    {
        records.push_back({level, std::string(message)});
    }

    void callEvent(endstone::Event & /*event*/) override {}

    struct LogRecord {
        endstone::Logger::Level level;
        std::string message;
    };

    std::vector<LogRecord> records;
};

class TestPlugin final : public endstone::Plugin {
public:
    explicit TestPlugin(std::string name) : description_(std::move(name), "1.0.0") {}

    [[nodiscard]] const endstone::PluginDescription &getDescription() const override { return description_; }

private:
    endstone::PluginDescription description_;
};

// Native service fixture for Python binding tests.
class TestService {
public:
    explicit TestService(std::string name)
        : plugin_(std::make_shared<TestPlugin>(std::move(name))), platform_(std::make_shared<TestPlatform>()),
          service_(std::make_shared<papi::detail::PlaceholderApiImpl>(platform_, plugin_->getDescription().getName()))
    {
    }

    ~TestService()
    {
        if (service_) {
            service_->shutdown();
        }
    }

    [[nodiscard]] std::shared_ptr<papi::PlaceholderAPI> service() const { return service_; }

    [[nodiscard]] endstone::Plugin &plugin() const { return *plugin_; }

    bool registerExpansion(const py::object &expansion)
    {
        return registerExpansionFromPython(*service_, *plugin_, expansion);
    }

    void shutdown() { service_->shutdown(); }

    bool unregisterExpansion(std::string_view identifier)
    {
        return service_->unregisterExpansion(*plugin_, identifier);
    }

    std::size_t unregisterExpansions() { return service_->unregisterExpansions(*plugin_); }

    [[nodiscard]] std::string setRelationalPlaceholders(std::string_view text) const
    {
        return service_->setRelationalPlaceholders(alice_, bob_, text);
    }

    void handlePlayerQuit() { service_->handlePlayerQuit(alice_); }

    [[nodiscard]] std::vector<std::string> warningMessages() const
    {
        std::vector<std::string> result;
        for (const auto &record : platform_->records) {
            if (record.level >= endstone::Logger::Warning) {
                result.push_back(record.message);
            }
        }
        return result;
    }

private:
    std::shared_ptr<TestPlugin> plugin_;
    std::shared_ptr<TestPlatform> platform_;
    std::shared_ptr<papi::detail::PlaceholderApiImpl> service_;
    papi::testing::FakePlayer alice_{"Alice", endstone::UUID{{1}}};
    papi::testing::FakePlayer bob_{"Bob", endstone::UUID{{2}}};
};
#endif  // PAPI_TEST_BINDINGS

}  // namespace

PYBIND11_MODULE(_papi, m)
{
    m.doc() = "Native core of the Endstone PlaceholderAPI framework.";

    py::module_::import("endstone");
    py::module_::import("endstone.plugin");

    m.attr("__version__") = std::string(papi::getVersion());
    m.attr("SERVICE_NAME") = std::string(papi::PlaceholderAPI::ServiceName);

    py::native_enum<papi::UnregisterReason>(m, "UnregisterReason", "enum.Enum",
                                            "Why an expansion was removed from the registry.")
        .value("EXPLICIT", papi::UnregisterReason::Explicit)
        .value("OWNER_DISABLED", papi::UnregisterReason::OwnerDisabled)
        .value("REQUIRED_PLUGIN_DISABLED", papi::UnregisterReason::RequiredPluginDisabled)
        .value("PAPI_SHUTDOWN", papi::UnregisterReason::PapiShutdown)
        .finalize();

    py::class_<papi::ExpansionInfo>(m, "ExpansionInfo",
                                    "An immutable copy of a registered expansion's metadata. Stays valid after the "
                                    "expansion it describes is unregistered.")
        .def_readonly("identifier", &papi::ExpansionInfo::identifier, "The canonical, lowercase identifier.")
        .def_readonly("name", &papi::ExpansionInfo::name, "The expansion's display name.")
        .def_readonly("author", &papi::ExpansionInfo::author, "The expansion's author.")
        .def_readonly("version", &papi::ExpansionInfo::version, "The expansion's version.")
        .def_readonly("owner", &papi::ExpansionInfo::owner, "The plugin that registered the expansion.")
        .def_readonly("required_plugin", &papi::ExpansionInfo::required_plugin,
                      "The plugin the expansion requires, or None.")
        .def_readonly("relational", &papi::ExpansionInfo::relational,
                      "Whether the expansion handles relational placeholders.")
        .def("__eq__",
             [](const papi::ExpansionInfo &self, const py::object &other) {
                 if (!py::isinstance<papi::ExpansionInfo>(other)) {
                     return false;
                 }
                 return self == other.cast<papi::ExpansionInfo>();
             })
        .def("__repr__", [](const papi::ExpansionInfo &self) {
            return "ExpansionInfo(identifier='" + self.identifier + "', version='" + self.version + "', owner='" +
                   self.owner + "')";
        });

    // smart_holder preserves Python-derived providers held by the native registry.
    py::class_<papi::PlaceholderExpansion, papi::python::PyPlaceholderExpansion, py::smart_holder>(
        m, "PlaceholderExpansion",
        "Base class for placeholder providers. Subclass this and register it with the "
        "PlaceholderAPI service.")
        .def(py::init<>())
        .def_property_readonly("identifier", &papi::PlaceholderExpansion::getIdentifier,
                               "The identifier this expansion answers to.")
        .def_property_readonly("author", &papi::PlaceholderExpansion::getAuthor, "The expansion's author.")
        .def_property_readonly("version", &papi::PlaceholderExpansion::getVersion, "The expansion's version.")
        .def_property_readonly("name", &papi::PlaceholderExpansion::getName, "The expansion's display name.")
        .def_property_readonly("required_plugin", &papi::PlaceholderExpansion::getRequiredPlugin,
                               "The plugin this expansion requires, or None.")
        .def("can_register", &papi::PlaceholderExpansion::canRegister, "Final chance to refuse registration.")
        .def("supports_relational_placeholders", &papi::PlaceholderExpansion::supportsRelationalPlaceholders,
             "Whether this expansion answers relational placeholders.")
        .def("supports_player_cleanup", &papi::PlaceholderExpansion::supportsPlayerCleanup,
             "Whether this expansion wants to be told when a player quits.")
        .def("on_request", &papi::PlaceholderExpansion::onRequest, py::arg("player"), py::arg("params"),
             "Resolves an ordinary placeholder. Return a str, or None to leave the placeholder untouched.")
        .def("on_relational_request", &papi::PlaceholderExpansion::onRelationalRequest, py::arg("one"), py::arg("two"),
             py::arg("params"), "Resolves a relational placeholder between two online players.")
        .def("on_player_quit", &papi::PlaceholderExpansion::onPlayerQuit, py::arg("player"),
             "Called when a player leaves, if this expansion opted into cleanup.")
        .def("on_unregister", &papi::PlaceholderExpansion::onUnregister, py::arg("reason"),
             "Called once after this expansion has been removed from the registry.");

    // Python consumes the native service but cannot subclass it.
    py::class_<papi::PlaceholderAPI, endstone::Service, std::shared_ptr<papi::PlaceholderAPI>>(
        m, "PlaceholderAPI", "Resolves placeholders through registered expansions.", py::is_final())
        .def_static(
            "load",
            [](const endstone::ServiceManager &manager) { return papi::detail::ServicePublication::load(manager); },
            py::arg("service_manager"),
            "Loads the active native PlaceholderAPI service. Returns None when it is unavailable or shadowed.")
        .def_property_readonly("active", &papi::PlaceholderAPI::isActive,
                               "Whether this service is still usable. False once PAPI has been disabled.")
        .def("set_placeholders", &papi::PlaceholderAPI::setPlaceholders, py::arg("player"), py::arg("text"),
             "Replaces every resolvable {identifier.params} in text.")
        .def("set_relational_placeholders", &papi::PlaceholderAPI::setRelationalPlaceholders, py::arg("one"),
             py::arg("two"), py::arg("text"), "Replaces every resolvable {rel.identifier_params} in text.")
        .def("contains_placeholders", &papi::PlaceholderAPI::containsPlaceholders, py::arg("text"),
             "Whether text lexically contains a placeholder-shaped substring.")
        .def("is_registered", &papi::PlaceholderAPI::isRegistered, py::arg("identifier"),
             "Whether an identifier currently has an expansion registered.")
        .def_property_readonly(
            "registered_identifiers",
            [](const papi::PlaceholderAPI &self) { return py::tuple(py::cast(self.getRegisteredIdentifiers())); },
            "Every registered identifier, sorted canonically as a frozen tuple snapshot.")
        .def_property_readonly(
            "expansions", [](const papi::PlaceholderAPI &self) { return py::tuple(py::cast(self.getExpansions())); },
            "Metadata for every registered expansion, sorted by identifier as a frozen tuple snapshot.")
        .def("register_expansion", &registerExpansionFromPython, py::arg("owner"), py::arg("expansion"),
             "Registers an expansion on behalf of a plugin.")
        .def("unregister_expansion", &papi::PlaceholderAPI::unregisterExpansion, py::arg("owner"),
             py::arg("identifier"), "Unregisters one expansion owned by a plugin.")
        .def("unregister_expansions", &papi::PlaceholderAPI::unregisterExpansions, py::arg("owner"),
             "Unregisters every expansion owned by a plugin.");

    py::class_<papi::ExpansionRegisteredEvent, endstone::Event>(
        m, "ExpansionRegisteredEvent", "Fired after an expansion has been added to the registry.")
        .def_property_readonly("expansion_info", &papi::ExpansionRegisteredEvent::getExpansionInfo,
                               "Metadata describing the expansion that was registered.");

    py::class_<papi::ExpansionUnregisteredEvent, endstone::Event>(
        m, "ExpansionUnregisteredEvent", "Fired after an expansion has been removed from the registry.")
        .def_property_readonly("expansion_info", &papi::ExpansionUnregisteredEvent::getExpansionInfo,
                               "Metadata describing the expansion that was removed.")
        .def_property_readonly("reason", &papi::ExpansionUnregisteredEvent::getReason,
                               "Why the expansion was removed.");

    py::class_<papi::detail::PapiBootstrap>(m, "_PapiBootstrap", "Internal native lifecycle state for the PAPI plugin.")
        .def(py::init<>())
        .def("start", &papi::detail::PapiBootstrap::start, py::arg("plugin"),
             "Internal: builds the service and registers it with Endstone.")
        .def("stop", &papi::detail::PapiBootstrap::stop, "Internal: tears the service down. Idempotent.")
        .def_property_readonly("service", &papi::detail::PapiBootstrap::getService,
                               "Internal: the published service, or None.");

#ifdef PAPI_TEST_BINDINGS
    // Use the production cast and smart-holder path in proxy tests.
    m.def("_test_make_proxy", [](const py::object &expansion) -> std::shared_ptr<papi::PlaceholderExpansion> {
        auto *native = expansion.cast<papi::PlaceholderExpansion *>();
        if (native == nullptr) {
            throw py::type_error("expansion must be a PlaceholderExpansion");
        }
        return std::make_shared<papi::python::GilSafeExpansionProxy>(expansion, *native);
    });

    py::class_<TestService>(m, "_TestService", "Test-only: PlaceholderAPI backed by an in-memory platform.")
        .def(py::init<std::string>(), py::arg("plugin_name"))
        .def_property_readonly("service", &TestService::service, "The native PlaceholderAPI service.")
        .def_property_readonly("plugin", &TestService::plugin, "The test plugin (owner for registrations).")
        .def("register_expansion", &TestService::registerExpansion, py::arg("expansion"),
             "Registers a Python expansion through the GIL-safe proxy path.")
        .def("shutdown", &TestService::shutdown,
             "Shuts down the service so subsequent registrations are rejected as inactive.")
        .def("unregister_expansion", &TestService::unregisterExpansion, py::arg("identifier"),
             "Unregisters one expansion owned by the test plugin.")
        .def("unregister_expansions", &TestService::unregisterExpansions,
             "Unregisters every expansion owned by the test plugin.")
        .def("set_relational_placeholders", &TestService::setRelationalPlaceholders, py::arg("text"),
             "Dispatches relational placeholders with two in-memory test players.")
        .def("handle_player_quit", &TestService::handlePlayerQuit,
             "Dispatches player cleanup with an in-memory test player.")
        .def_property_readonly("warnings", &TestService::warningMessages,
                               "Every warning-or-above message logged by the service.");
#endif
}
