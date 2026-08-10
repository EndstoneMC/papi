// Verifies each public header is self-contained: including it alone must compile.
// Deprecated members are declared in these headers, so warnings are silenced here
// rather than at the declaration sites.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "endstone_papi/events.h"
#include "endstone_papi/expansion_info.h"
#include "endstone_papi/papi.h"
#include "endstone_papi/placeholder_api.h"
#include "endstone_papi/placeholder_expansion.h"
#include "endstone_papi/unregister_reason.h"
#include "endstone_papi/version.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <memory>
#include <type_traits>

#include <endstone/plugin/service.h>

#include <gtest/gtest.h>

namespace {

// REG-015: metadata copies must be plain values that outlive their expansion.
static_assert(std::is_copy_constructible_v<papi::ExpansionInfo>);
static_assert(std::is_move_constructible_v<papi::ExpansionInfo>);
static_assert(std::is_copy_assignable_v<papi::ExpansionInfo>);

// The service must be usable as an Endstone service and shared with consumers.
static_assert(std::is_base_of_v<endstone::Service, papi::PlaceholderAPI>);
static_assert(std::has_virtual_destructor_v<papi::PlaceholderAPI>);
static_assert(std::has_virtual_destructor_v<papi::PlaceholderExpansion>);

// Providers hand ownership to PAPI through a shared pointer.
static_assert(std::is_same_v<decltype(std::declval<papi::PlaceholderAPI &>().registerExpansion(
                                 std::declval<endstone::Plugin &>(),
                                 std::declval<std::shared_ptr<papi::PlaceholderExpansion>>())),
                             bool>);

// An expansion must not be silently copied out of the registry.
static_assert(!std::is_copy_constructible_v<papi::PlaceholderExpansion>);

TEST(PublicApi, ServiceNameIsStable)
{
    EXPECT_EQ(papi::PlaceholderAPI::ServiceName, "PlaceholderAPI");
}

TEST(PublicApi, UnregisterReasonNamesAreStable)
{
    EXPECT_EQ(toString(papi::UnregisterReason::Explicit), "Explicit");
    EXPECT_EQ(toString(papi::UnregisterReason::OwnerDisabled), "OwnerDisabled");
    EXPECT_EQ(toString(papi::UnregisterReason::RequiredPluginDisabled), "RequiredPluginDisabled");
    EXPECT_EQ(toString(papi::UnregisterReason::PapiShutdown), "PapiShutdown");
}

TEST(PublicApi, ExpansionInfoComparesByValue)
{
    const papi::ExpansionInfo a{"demo", "Demo", "author", "1.0", "owner", std::nullopt, false};
    papi::ExpansionInfo b = a;
    EXPECT_EQ(a, b);

    b.version = "2.0";
    EXPECT_NE(a, b);
}

TEST(PublicApi, ExpansionInfoCopySurvivesSourceDestruction)
{
    papi::ExpansionInfo copy;
    {
        const papi::ExpansionInfo original{"demo", "Demo", "author", "1.0", "owner", "dependency", true};
        copy = original;
    }
    EXPECT_EQ(copy.identifier, "demo");
    EXPECT_EQ(copy.required_plugin, "dependency");
    EXPECT_TRUE(copy.relational);
}

// A provider that implements only the required members, to pin down the defaults.
class MinimalExpansion final : public papi::PlaceholderExpansion {
public:
    [[nodiscard]] std::string getIdentifier() const override { return "Demo"; }
    [[nodiscard]] std::string getAuthor() const override { return "author"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        last_player_was_null = player == nullptr;
        last_params = std::string(params);
        return "value";
    }

    bool last_player_was_null = false;
    std::string last_params;
};

TEST(PlaceholderExpansion, NameDefaultsToIdentifier)
{
    const MinimalExpansion expansion;
    EXPECT_EQ(expansion.getName(), "Demo");
    EXPECT_EQ(expansion.getName(), expansion.getIdentifier());
}

TEST(PlaceholderExpansion, OptionalCapabilitiesDefaultOff)
{
    const MinimalExpansion expansion;
    EXPECT_FALSE(expansion.getRequiredPlugin().has_value());
    EXPECT_TRUE(expansion.canRegister());
    EXPECT_FALSE(expansion.supportsRelationalPlaceholders());
    EXPECT_FALSE(expansion.supportsPlayerCleanup());
}

TEST(PlaceholderExpansion, RelationalRequestDefaultsToUnresolved)
{
    MinimalExpansion expansion;
    // Defaulted overload must not dereference the players it is handed.
    const endstone::Player *none = nullptr;
    (void)none;
    EXPECT_FALSE(expansion.supportsRelationalPlaceholders());
}

TEST(PlaceholderExpansion, PlayerQuitAndUnregisterDefaultToNoOps)
{
    MinimalExpansion expansion;
    EXPECT_NO_THROW(expansion.onUnregister(papi::UnregisterReason::Explicit));
}

// PAR-017: a null player is a supported input, not an error.
TEST(PlaceholderExpansion, RequestAcceptsNullPlayer)
{
    MinimalExpansion expansion;
    const auto value = expansion.onRequest(nullptr, "name");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    EXPECT_TRUE(expansion.last_player_was_null);
    EXPECT_EQ(expansion.last_params, "name");
}

TEST(Events, CarryOnlyCopiedMetadata)
{
    const papi::ExpansionInfo info{"demo", "Demo", "author", "1.0", "owner", std::nullopt, false};

    const papi::ExpansionRegisteredEvent registered{info};
    EXPECT_EQ(registered.getExpansionInfo(), info);
    EXPECT_EQ(registered.getEventName(), "ExpansionRegisteredEvent");
    EXPECT_FALSE(registered.isAsynchronous());

    const papi::ExpansionUnregisteredEvent unregistered{info, papi::UnregisterReason::OwnerDisabled};
    EXPECT_EQ(unregistered.getExpansionInfo(), info);
    EXPECT_EQ(unregistered.getReason(), papi::UnregisterReason::OwnerDisabled);
    EXPECT_EQ(unregistered.getEventName(), "ExpansionUnregisteredEvent");
    EXPECT_FALSE(unregistered.isAsynchronous());
}

}  // namespace
