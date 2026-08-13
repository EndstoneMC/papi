// A separately built provider DSO that compiles against the installed
// public headers only.  The test harness loads this module, calls the factory,
// and drives the full register -> request -> unregister -> unload lifecycle.
//
// The factory returns a raw pointer and pairs with a destroy function so the
// object's destructor and deallocation both execute inside this DSO.  This is
// the C-ABI-safe pattern for crossing a DSO boundary without relying on a
// shared C++ runtime's shared_ptr layout.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <endstone/offline_player.h>

#include <endstone_papi/placeholder_expansion.h>
#include <endstone_papi/unregister_reason.h>

#ifdef _WIN32
#define PAPI_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PAPI_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

// Module-static state that the test reads before unloading.  The expansion
// writes to these in its destructor / onUnregister so the test can observe
// lifecycle transitions without reaching back into the object after death.
bool g_destroyed = false;
papi::UnregisterReason g_last_reason{};

class DemoExpansion final : public papi::PlaceholderExpansion {
public:
    DemoExpansion() = default;

    ~DemoExpansion() override { g_destroyed = true; }

    [[nodiscard]] std::string getIdentifier() const override { return "demo"; }
    [[nodiscard]] std::string getAuthor() const override { return "fixture"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer * /*player*/,
                                                       std::string_view params) override
    {
        return std::string{"value-"} + std::string{params};
    }

    void onUnregister(papi::UnregisterReason reason) override { g_last_reason = reason; }
};

}  // namespace

// C-linkage factory functions use snake_case by C-ABI convention.
// NOLINTBEGIN(readability-identifier-naming)
extern "C" {

PAPI_FIXTURE_EXPORT papi::PlaceholderExpansion *papi_create_demo_expansion()
{
    g_destroyed = false;
    g_last_reason = {};
    return new DemoExpansion();
}

PAPI_FIXTURE_EXPORT void papi_destroy_expansion(papi::PlaceholderExpansion *expansion)
{
    delete expansion;
}

PAPI_FIXTURE_EXPORT bool papi_is_demo_destroyed()
{
    return g_destroyed;
}

PAPI_FIXTURE_EXPORT papi::UnregisterReason papi_demo_last_reason()
{
    return g_last_reason;
}

}  // extern "C"
// NOLINTEND(readability-identifier-naming)
