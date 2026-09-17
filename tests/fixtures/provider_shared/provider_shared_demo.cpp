// Provider-owned shared_ptr fixture built from public SDK headers only.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <endstone_papi/placeholder_api.h>
#include <endstone_papi/unregister_reason.h>

#ifdef _WIN32
#define PAPI_SHARED_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PAPI_SHARED_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

bool g_destroyed = false;
papi::UnregisterReason g_last_reason{};

class SharedExpansion final : public papi::PlaceholderExpansion {
public:
    ~SharedExpansion() override { g_destroyed = true; }

    [[nodiscard]] std::string getIdentifier() const override { return "shared-demo"; }
    [[nodiscard]] std::string getAuthor() const override { return "shared-fixture"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer * /*player*/,
                                                       std::string_view params) override
    {
        return std::string{"shared-value-"} + std::string{params};
    }

    void onUnregister(papi::UnregisterReason reason) override { g_last_reason = reason; }
};

}  // namespace

// NOLINTBEGIN(readability-identifier-naming)
extern "C" {

PAPI_SHARED_FIXTURE_EXPORT bool papi_register_shared_expansion(papi::PlaceholderAPI *api, endstone::Plugin *owner)
{
    g_destroyed = false;
    g_last_reason = {};
    return api != nullptr && owner != nullptr && api->registerExpansion(*owner, std::make_shared<SharedExpansion>());
}

PAPI_SHARED_FIXTURE_EXPORT bool papi_is_shared_expansion_destroyed()
{
    return g_destroyed;
}

PAPI_SHARED_FIXTURE_EXPORT papi::UnregisterReason papi_shared_expansion_last_reason()
{
    return g_last_reason;
}

}  // extern "C"
// NOLINTEND(readability-identifier-naming)
