#include <memory>
#include <optional>
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

#include "core/deprecation.h"
#include "platform/endstone/bootstrap.h"
#include "python/expansion_trampoline.h"
#include "python/gil_safe_proxy.h"

#ifdef PAPI_TEST_BINDINGS
#include "fake_player.h"
#endif

namespace py = pybind11;

namespace {

/**
 * @brief Owns the native service on behalf of the Python PAPI plugin.
 *
 * The Python plugin holds one of these and does nothing else. All framework state lives
 * natively, so a torn-down Python object cannot leave the registry in a bad state:
 * shutdown is driven explicitly from on_disable while the interpreter and every
 * provider module are still loaded.
 */
class PapiHost {
public:
    bool start(endstone::Plugin &plugin) { return bootstrap_.start(plugin); }

    void stop() { bootstrap_.stop(); }

    [[nodiscard]] std::shared_ptr<papi::PlaceholderAPI> getService() const { return bootstrap_.getService(); }

private:
    papi::detail::PapiBootstrap bootstrap_;
};

/**
 * @brief Deprecated Python callback adapter for register_placeholder.
 *
 * Wraps a Python callable as a PlaceholderExpansion so it participates in the normal
 * owner-aware registry. Unlike the C++ Processor adapter, the callable may return
 * None to leave the placeholder unresolved (the original token is preserved).
 *
 * Every crossing into Python acquires the GIL, and the final release of the stored
 * handle happens under the GIL so the registry can drop the last reference from
 * anywhere safely.
 */
class PythonLegacyExpansion final : public papi::PlaceholderExpansion {
public:
    PythonLegacyExpansion(std::string identifier, std::string owner, py::object callback)
        : identifier_(std::move(identifier)), owner_(std::move(owner)), callback_(std::move(callback))
    {
    }

    ~PythonLegacyExpansion() override  // NOLINT(bugprone-exception-escape)
    {
        if (callback_) {
            clearCallbackUnderGil();
        }
    }

    PythonLegacyExpansion(const PythonLegacyExpansion &) = delete;
    PythonLegacyExpansion(PythonLegacyExpansion &&) = delete;
    PythonLegacyExpansion &operator=(const PythonLegacyExpansion &) = delete;
    PythonLegacyExpansion &operator=(PythonLegacyExpansion &&) = delete;

    [[nodiscard]] std::string getIdentifier() const override { return identifier_; }
    [[nodiscard]] std::string getAuthor() const override { return owner_; }
    [[nodiscard]] std::string getVersion() const override { return "legacy"; }
    [[nodiscard]] std::string getName() const override { return identifier_; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        if (!callback_) {
            return std::nullopt;
        }
        const py::gil_scoped_acquire gil;
        if (!callback_) {
            return std::nullopt;
        }
        try {
            // A null player arrives in Python as None, matching on_request semantics.
            const auto result = callback_(py::cast(player), py::str(std::string(params)));
            if (result.is_none()) {
                return std::nullopt;
            }
            // Exact str check: reject str subclasses per the frozen provider contract.
            if (Py_TYPE(result.ptr()) != &PyUnicode_Type) {
                throw std::runtime_error("register_placeholder callback must return str or None, got " +
                                         py::str(py::type::handle_of(result)).cast<std::string>());
            }
            return result.cast<std::string>();
        }
        catch (py::error_already_set &error) {
            // Rethrown as a plain C++ exception so no Python state escapes into the
            // native error path, where the GIL is no longer guaranteed.
            throw std::runtime_error(error.what());
        }
    }

    void onUnregister(papi::UnregisterReason) override
    {
        // Drop the callback here rather than at destruction so it cannot survive into a
        // window where its plugin's code may already be gone.
        clearCallbackUnderGil();
    }

private:
    /**
     * @brief Releases the stored Python callback under the GIL, leaving callback_ empty.
     *
     * Setting callback_ to an empty py::object (not py::none()) ensures the member
     * destructor is a no-op: dec_ref() is never called after the local GIL guard is
     * destroyed. On CPython 3.10/3.11 Py_None is reference-counted, so releasing it
     * without the GIL would be undefined.
     */
    void clearCallbackUnderGil()
    {
        const py::gil_scoped_acquire gil;
        callback_ = py::object();
    }

    std::string identifier_;
    std::string owner_;
    py::object callback_;
};

/**
 * @brief Deprecated adapter: registers a Python callable as a placeholder.
 *
 * The callable is wrapped in a PythonLegacyExpansion and registered through the normal
 * owner-aware registry, so it participates in lifecycle, duplicate rejection, and
 * error containment exactly like a full PlaceholderExpansion. Returns true if the
 * placeholder was registered.
 */
bool registerPlaceholderFromPython(papi::PlaceholderAPI &service, endstone::Plugin &owner,
                                   const std::string &identifier, const py::object &callback)
{
    // Non-executing callable check: PyCallable_Check reads the tp_call slot directly, so
    // custom __getattribute__/descriptor machinery cannot run before the native gate
    // rejects an inactive or off-thread caller.
    if (PyCallable_Check(callback.ptr()) == 0) {
        throw py::type_error("register_placeholder callback must be callable");
    }

    // Reject an inactive service before constructing the adapter, so no provider-
    // associated work runs after PAPI has begun shutting down. The full thread/state
    // gate is enforced inside registerExpansion.
    if (!service.isActive()) {
        return false;
    }

    auto expansion = std::make_shared<PythonLegacyExpansion>(identifier, owner.getName(), callback);

    // Registration may invoke the expansion (metadata, preflight), which reacquires the
    // GIL. The GIL is released here so a provider callback on the server thread cannot
    // deadlock against Python code on another thread.
    const py::gil_scoped_release release;
    return service.registerExpansion(owner, std::move(expansion));
}

/**
 * @brief Registers an expansion supplied from Python.
 *
 * The provider is wrapped in a GIL-safe proxy before it reaches the registry, so every
 * later callback and the final release happen with the GIL held. A native expansion
 * passed in from Python is wrapped identically: the extra acquisition is cheap next to
 * getting the ownership rules wrong.
 */
bool registerExpansionFromPython(papi::PlaceholderAPI &service, endstone::Plugin &owner, const py::object &expansion)
{
    auto *native = expansion.cast<papi::PlaceholderExpansion *>();
    if (native == nullptr) {
        throw py::type_error("expansion must be a PlaceholderExpansion");
    }

    // Ownership-only construction: the proxy stores the handle but reads no metadata,
    // so every Python entry point lands behind the core's canOperate gate and
    // invokeProvider containment.
    auto proxy = std::make_shared<papi::python::GilSafeExpansionProxy>(expansion, *native);

    // Registration calls into the registry and may invoke the proxy, which reacquires
    // the GIL as needed. The GIL is released here so a provider callback running on the
    // server thread cannot deadlock against Python code on another thread.
    const py::gil_scoped_release release;
    return service.registerExpansion(owner, std::move(proxy));
}

#ifdef PAPI_TEST_BINDINGS
/**
 * @brief In-memory Platform for Python regression tests, mirroring FakePlatform.
 *
 * Reports the calling thread as primary and every plugin as enabled, records log
 * messages so tests can assert on reentrancy diagnostics, and no-ops event dispatch.
 */
class TestPlatform final : public papi::detail::Platform {
public:
    [[nodiscard]] bool isPrimaryThread() const override { return true; }
    [[nodiscard]] bool isPluginEnabled(std::string_view /*name*/) const override { return true; }
    [[nodiscard]] bool isPluginEnabled(const endstone::Plugin & /*plugin*/) const override { return true; }
    [[nodiscard]] const endstone::Player *getOnlinePlayer(endstone::UUID /*id*/) const override { return nullptr; }

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

/**
 * @brief Minimal endstone::Plugin identity for Python regression tests.
 */
class TestPlugin final : public endstone::Plugin {
public:
    explicit TestPlugin(std::string name) : description_(std::move(name), "1.0.0") {}

    [[nodiscard]] const endstone::PluginDescription &getDescription() const override { return description_; }

private:
    endstone::PluginDescription description_;
};

/**
 * @brief Owns a PlaceholderApiImpl backed by TestPlatform/TestPlugin.
 *
 * Lets Python regression tests exercise parse-reentrancy and cycle detection through
 * the same GilSafeExpansionProxy + trampoline path production uses, without a running
 * Bedrock server. Compiled out of release wheels.
 */
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

    bool registerPlaceholder(const std::string &identifier, const py::object &callback)
    {
        return registerPlaceholderFromPython(*service_, *plugin_, identifier, callback);
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

    // Subclassable from Python. smart_holder plus trampoline_self_life_support let the
    // native registry hold the last reference to a Python-derived object safely.
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

    // The service is native and final: Python consumes it but never implements it.
    // py::is_final makes a Python subclass a TypeError rather than something that
    // silently appears to work while bypassing the framework's lifecycle.
    py::class_<papi::PlaceholderAPI, endstone::Service, std::shared_ptr<papi::PlaceholderAPI>>(
        m, "PlaceholderAPI", "Resolves placeholders through registered expansions.", py::is_final())
        .def_static(
            "load",
            [](const endstone::ServiceManager &manager) {
                return manager.load<papi::PlaceholderAPI>(std::string(papi::PlaceholderAPI::ServiceName));
            },
            py::arg("service_manager"),
            "Loads the typed PlaceholderAPI service. Returns None when PAPI is unavailable.")
        .def_property_readonly("active", &papi::PlaceholderAPI::isActive,
                               "Whether this service is still usable. False once PAPI has been disabled.")
        .def("set_placeholders", &papi::PlaceholderAPI::setPlaceholders, py::arg("player"), py::arg("text"),
             "Replaces every resolvable {identifier_params} in text.")
        .def("set_relational_placeholders", &papi::PlaceholderAPI::setRelationalPlaceholders, py::arg("one"),
             py::arg("two"), py::arg("text"), "Replaces every resolvable {rel_identifier_params} in text.")
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
             "Unregisters every expansion owned by a plugin.")
        // Deprecated one-release Python compatibility adapters (T-013 / A-003).
        // register_placeholder wraps a Python callable in an internal expansion so it
        // shares the owner-aware registry, rejects duplicate identifiers, and does not
        // resurrect colon fallback. The callable may return None to leave the
        // placeholder unresolved, unlike the C++ Processor adapter.
        .def("register_placeholder", &registerPlaceholderFromPython, py::arg("owner"), py::arg("identifier"),
             py::arg("callback"), "Deprecated: register a callable as a placeholder. Prefer register_expansion.")
        // placeholder_pattern returns the historical bracket regex for source
        // compatibility. It is not used by the parser and describes neither the current
        // syntax nor the current implementation.
        .def_property_readonly(
            "placeholder_pattern",
            [](const papi::PlaceholderAPI &self) {
                PAPI_SUPPRESS_DEPRECATED_BEGIN
                return self.getPlaceholderPattern();
                PAPI_SUPPRESS_DEPRECATED_END
            },
            "Deprecated: the historical bracket regex [{]([^{}]+)[}]. Not used by the parser.");

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

    // Private bootstrap surface. Only the PAPI plugin uses it; it is deliberately not
    // part of the documented API and grants no way to implement the service.
    py::class_<PapiHost>(m, "_PapiHost", "Internal: owns the native PlaceholderAPI service.")
        .def(py::init<>())
        .def("start", &PapiHost::start, py::arg("plugin"),
             "Internal: builds the service and registers it with Endstone.")
        .def("stop", &PapiHost::stop, "Internal: tears the service down. Idempotent.")
        .def_property_readonly("service", &PapiHost::getService, "Internal: the published service, or None.");

#ifdef PAPI_TEST_BINDINGS
    // Test-only: lets Python regression tests construct the proxy directly so its
    // ownership-only contract (no metadata reads in the constructor) can be verified
    // without a running server. Compiled out of release wheels.
    //
    // The factory mirrors registerExpansionFromPython exactly: the native pointer is
    // obtained via py::cast, not via the PlaceholderExpansion& type caster. Returns
    // shared_ptr<PlaceholderExpansion> so pybind11 wraps it through the base class's
    // smart_holder, avoiding a holder-type mismatch.
    m.def("_test_make_proxy", [](const py::object &expansion) -> std::shared_ptr<papi::PlaceholderExpansion> {
        auto *native = expansion.cast<papi::PlaceholderExpansion *>();
        if (native == nullptr) {
            throw py::type_error("expansion must be a PlaceholderExpansion");
        }
        return std::make_shared<papi::python::GilSafeExpansionProxy>(expansion, *native);
    });

    // Test-only: a service backed by an in-memory platform and plugin, so Python
    // regression tests can exercise reentrancy and cycle detection through the real
    // GilSafeExpansionProxy + trampoline dispatch path without a running server.
    py::class_<TestService>(m, "_TestService", "Test-only: PlaceholderAPI backed by an in-memory platform.")
        .def(py::init<std::string>(), py::arg("plugin_name"))
        .def_property_readonly("service", &TestService::service, "The native PlaceholderAPI service.")
        .def_property_readonly("plugin", &TestService::plugin, "The test plugin (owner for registrations).")
        .def("register_expansion", &TestService::registerExpansion, py::arg("expansion"),
             "Registers a Python expansion through the GIL-safe proxy path.")
        .def("register_placeholder", &TestService::registerPlaceholder, py::arg("identifier"), py::arg("callback"),
             "Registers a deprecated Python callback placeholder through the native service.")
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
