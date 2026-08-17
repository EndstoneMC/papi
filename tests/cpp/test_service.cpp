// Native service behavior, including error containment and thread policy.

#include <thread>

#include <gtest/gtest.h>

#include "endstone_papi/events.h"

#include "core/service/placeholder_api_impl.h"
#include "fake_player.h"
#include "fakes.h"

namespace {

using papi::UnregisterReason;
using papi::detail::ErrorThrottleWindow;
using papi::detail::PlaceholderApiImpl;
using papi::testing::FakeExpansion;
using papi::testing::FakePlatform;
using papi::testing::FakePlayer;
using papi::testing::FakePlugin;
using papi::testing::LifetimeTrackingExpansion;

class ServiceTest : public ::testing::Test {
protected:
    ServiceTest()
    {
        platform_ = std::make_shared<FakePlatform>();
        platform_->enable(papi_plugin_);
        platform_->enable(owner_);
        platform_->enable(other_owner_);
        service_ = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    std::shared_ptr<FakeExpansion> add(const std::string &identifier, FakePlugin &owner)
    {
        auto expansion = std::make_shared<FakeExpansion>(identifier);
        EXPECT_TRUE(service_->registerExpansion(owner, expansion));
        return expansion;
    }

    [[nodiscard]] std::size_t countEventsNamed(const std::string_view name) const
    {
        std::size_t count = 0;
        for (const auto &event : platform_->events) {
            if (event.name == name) {
                ++count;
            }
        }
        return count;
    }

    std::shared_ptr<FakePlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
    FakePlugin papi_plugin_{"papi"};
    FakePlugin owner_{"provider"};
    FakePlugin other_owner_{"other"};
    FakePlayer alice_{"Alice", endstone::UUID{{1}}};
};

TEST_F(ServiceTest, StartsActive)
{
    EXPECT_TRUE(service_->isActive());
    EXPECT_TRUE(service_->getExpansions().empty());
    EXPECT_TRUE(service_->getRegisteredIdentifiers().empty());
}

TEST_F(ServiceTest, ResolvesThroughARegisteredExpansion)
{
    auto expansion = add("player", owner_);
    expansion->value = "Alex";

    EXPECT_EQ(service_->setPlaceholders(nullptr, "hello {player.name}!"), "hello Alex!");
    EXPECT_EQ(expansion->request_calls, 1);
    EXPECT_EQ(expansion->last_params, "name");
    EXPECT_EQ(expansion->last_player, nullptr);
}

TEST_F(ServiceTest, UnknownIdentifiersAndDeclinedValuesStayLiteral)
{
    auto expansion = add("player", owner_);
    expansion->value = std::nullopt;

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "{player.x}");
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{missing.x}"), "{missing.x}");
    EXPECT_EQ(expansion->request_calls, 1);
}

TEST_F(ServiceTest, ExposesSortedMetadataCopies)
{
    add("zulu", owner_);
    add("alpha", other_owner_);

    const auto infos = service_->getExpansions();
    ASSERT_EQ(infos.size(), 2U);
    EXPECT_EQ(infos[0].identifier, "alpha");
    EXPECT_EQ(infos[1].identifier, "zulu");

    const auto identifiers = service_->getRegisteredIdentifiers();
    ASSERT_EQ(identifiers.size(), 2U);
    EXPECT_EQ(identifiers[0], "alpha");
}

TEST_F(ServiceTest, ContainsIsPurelyLexical)
{
    EXPECT_TRUE(service_->containsPlaceholders("{}"));
    EXPECT_TRUE(service_->containsPlaceholders("{unknown.x}"));
    EXPECT_FALSE(service_->containsPlaceholders("}{"));
    EXPECT_FALSE(service_->containsPlaceholders(""));
}

TEST_F(ServiceTest, IsRegisteredRejectsInvalidQueries)
{
    add("demo", owner_);
    EXPECT_TRUE(service_->isRegistered("DEMO"));
    EXPECT_FALSE(service_->isRegistered("demo_x"));
    EXPECT_FALSE(service_->isRegistered("demo.x"));
    EXPECT_FALSE(service_->isRegistered(""));
    EXPECT_FALSE(service_->isRegistered("{demo}"));
}

TEST_F(ServiceTest, EmitsOneRegisteredEventOnlyOnSuccess)
{
    add("demo", owner_);
    EXPECT_EQ(countEventsNamed("ExpansionRegisteredEvent"), 1U);

    auto duplicate = std::make_shared<FakeExpansion>("demo");
    EXPECT_FALSE(service_->registerExpansion(other_owner_, duplicate));
    auto refused = std::make_shared<FakeExpansion>("other");
    refused->can_register = false;
    EXPECT_FALSE(service_->registerExpansion(owner_, refused));
    EXPECT_EQ(countEventsNamed("ExpansionRegisteredEvent"), 1U);
}

TEST_F(ServiceTest, MetadataExceptionDuringRegistrationIsAtomic)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->throw_from_identifier = true;
    EXPECT_FALSE(service_->registerExpansion(owner_, expansion));
    EXPECT_FALSE(service_->isRegistered("demo"));
    EXPECT_EQ(countEventsNamed("ExpansionRegisteredEvent"), 0U);
    EXPECT_TRUE(platform_->logger.anyContains("reading its metadata raised an error"));
}

TEST_F(ServiceTest, PreflightExceptionDuringRegistrationIsAtomic)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->throw_from_can_register = true;
    EXPECT_FALSE(service_->registerExpansion(owner_, expansion));
    EXPECT_FALSE(service_->isRegistered("demo"));
    EXPECT_EQ(countEventsNamed("ExpansionRegisteredEvent"), 0U);
    EXPECT_TRUE(platform_->logger.anyContains("raised an error during its registration check"));
}

TEST_F(ServiceTest, SelfParseCycleIsBounded)
{
    auto expansion = std::make_shared<FakeExpansion>("a");
    expansion->on_request = [&](const endstone::OfflinePlayer *, std::string_view) {
        return service_->setPlaceholders(nullptr, "{a.x}");
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    const auto result = service_->setPlaceholders(nullptr, "{a.x}");
    EXPECT_EQ(result, "{a.x}");
    EXPECT_GT(expansion->request_calls, 0);
    EXPECT_TRUE(platform_->logger.anyContains("cycle detected"));
}

TEST_F(ServiceTest, IndirectParseCycleIsBounded)
{
    auto a = std::make_shared<FakeExpansion>("a");
    auto b = std::make_shared<FakeExpansion>("b");
    a->on_request = [&](const endstone::OfflinePlayer *, std::string_view) {
        return service_->setPlaceholders(nullptr, "{b.y}");
    };
    b->on_request = [&](const endstone::OfflinePlayer *, std::string_view) {
        return service_->setPlaceholders(nullptr, "{a.x}");
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, a));
    ASSERT_TRUE(service_->registerExpansion(owner_, b));

    const auto result = service_->setPlaceholders(nullptr, "{a.x}");
    EXPECT_EQ(result, "{a.x}");
    EXPECT_TRUE(platform_->logger.anyContains("cycle detected"));
}

TEST_F(ServiceTest, NestedParsingAcrossServicesDoesNotAliasManagerLocalGenerations)
{
    auto other_service = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    auto outer = std::make_shared<FakeExpansion>("outer");
    auto inner = std::make_shared<FakeExpansion>("inner");
    outer->on_request = [other_service](const endstone::OfflinePlayer *, std::string_view) {
        return other_service->setPlaceholders(nullptr, "{inner.x}");
    };
    inner->value = "leaf";

    ASSERT_TRUE(service_->registerExpansion(owner_, outer));
    ASSERT_TRUE(other_service->registerExpansion(owner_, inner));

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{outer.x}"), "leaf");
    EXPECT_EQ(outer->request_calls, 1);
    EXPECT_EQ(inner->request_calls, 1);
    EXPECT_FALSE(platform_->logger.anyContains("cycle detected"));
}

TEST_F(ServiceTest, ParseDepthBudgetIsEnforced)
{
    constexpr int chain_length = 12;
    std::vector<std::shared_ptr<FakeExpansion>> chain;
    for (int i = 0; i < chain_length; ++i) {
        auto exp = std::make_shared<FakeExpansion>("e" + std::to_string(i));
        const auto next_token = (i + 1 < chain_length) ? "{e" + std::to_string(i + 1) + ".x}" : "leaf";
        exp->on_request = [&, next_token](const endstone::OfflinePlayer *, std::string_view) {
            return service_->setPlaceholders(nullptr, next_token);
        };
        ASSERT_TRUE(service_->registerExpansion(owner_, exp));
        chain.push_back(exp);
    }

    const auto result = service_->setPlaceholders(nullptr, "{e0.x}");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(platform_->logger.anyContains("parse depth budget exceeded"));
}

TEST_F(ServiceTest, EmitsUnregisteredEventAfterCleanup)
{
    auto expansion = add("demo", owner_);
    platform_->events.clear();

    EXPECT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_EQ(expansion->unregister_calls, 1);
    ASSERT_EQ(countEventsNamed("ExpansionUnregisteredEvent"), 1U);
    EXPECT_EQ(expansion->last_unregister_reason, UnregisterReason::Explicit);
}

TEST_F(ServiceTest, UnregisterRequiresTheRightOwner)
{
    auto expansion = add("demo", owner_);

    EXPECT_FALSE(service_->unregisterExpansion(other_owner_, "demo"));
    EXPECT_EQ(expansion->unregister_calls, 0);
    EXPECT_TRUE(service_->isRegistered("demo"));
}

TEST_F(ServiceTest, BulkUnregisterReportsAndCleansEveryEntry)
{
    auto a = add("alpha", owner_);
    auto b = add("beta", owner_);
    add("theirs", other_owner_);

    EXPECT_EQ(service_->unregisterExpansions(owner_), 2U);
    EXPECT_EQ(a->unregister_calls, 1);
    EXPECT_EQ(b->unregister_calls, 1);
    EXPECT_TRUE(service_->isRegistered("theirs"));
}

TEST_F(ServiceTest, ProviderExceptionsArePreservedAsLiteralTokens)
{
    auto expansion = add("player", owner_);
    expansion->throw_from_request = true;

    EXPECT_EQ(service_->setPlaceholders(nullptr, "a{player.x}b"), "a{player.x}b");
    EXPECT_TRUE(service_->isActive());
    EXPECT_TRUE(platform_->logger.anyContains("player"));
    EXPECT_TRUE(platform_->logger.anyContains("ordinary"));
}

TEST_F(ServiceTest, RepeatedProviderFailuresAreThrottled)
{
    std::chrono::steady_clock::time_point now{};
    service_->setErrorClock([&now] { return now; });

    auto expansion = add("player", owner_);
    expansion->throw_from_request = true;

    for (int i = 0; i < 100; ++i) {
        (void)service_->setPlaceholders(nullptr, "{player.x}");
    }
    EXPECT_EQ(platform_->logger.countAtLeast(endstone::Logger::Error), 1U);

    now += ErrorThrottleWindow;
    (void)service_->setPlaceholders(nullptr, "{player.x}");
    EXPECT_EQ(platform_->logger.countAtLeast(endstone::Logger::Error), 2U);
    EXPECT_TRUE(platform_->logger.anyContains("suppressed"));
}

TEST_F(ServiceTest, ParsingOffThePrimaryThreadPreservesInputAndDoesNotCallProviders)
{
    auto expansion = add("player", owner_);
    platform_->primary_thread = false;

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "{player.x}");
    EXPECT_EQ(expansion->request_calls, 0);
    EXPECT_TRUE(platform_->logger.anyContains("server thread"));
}

TEST_F(ServiceTest, MutationOffThePrimaryThreadFailsWithoutTouchingProviders)
{
    add("existing", owner_);
    platform_->primary_thread = false;

    auto expansion = std::make_shared<FakeExpansion>("demo");
    EXPECT_FALSE(service_->registerExpansion(owner_, expansion));
    EXPECT_EQ(expansion->identifier_calls, 0);
    EXPECT_FALSE(service_->unregisterExpansion(owner_, "existing"));
    EXPECT_EQ(service_->unregisterExpansions(owner_), 0U);

    platform_->primary_thread = true;
    EXPECT_TRUE(service_->isRegistered("existing"));
    EXPECT_FALSE(service_->isRegistered("demo"));
}

TEST_F(ServiceTest, PureQueriesAreSafeFromOtherThreads)
{
    add("demo", owner_);
    platform_->primary_thread = false;

    std::thread worker([&] {
        EXPECT_TRUE(service_->containsPlaceholders("{demo.x}"));
        EXPECT_TRUE(service_->isActive());
        EXPECT_TRUE(service_->isRegistered("demo"));
        EXPECT_EQ(service_->getExpansions().size(), 1U);
    });
    worker.join();
}

TEST_F(ServiceTest, RetainedServiceBecomesInertAfterShutdown)
{
    auto expansion = add("player", owner_);
    expansion->value = "Alex";
    ASSERT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "Alex");

    service_->shutdown();

    EXPECT_FALSE(service_->isActive());
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "{player.x}");
    EXPECT_TRUE(service_->getExpansions().empty());
    EXPECT_TRUE(service_->getRegisteredIdentifiers().empty());
    EXPECT_FALSE(service_->isRegistered("player"));

    auto fresh = std::make_shared<FakeExpansion>("fresh");
    EXPECT_FALSE(service_->registerExpansion(owner_, fresh));
    EXPECT_EQ(fresh->identifier_calls, 0);
    EXPECT_FALSE(service_->unregisterExpansion(owner_, "player"));
    EXPECT_EQ(service_->unregisterExpansions(owner_), 0U);

    EXPECT_TRUE(service_->containsPlaceholders("{a.b}"));
}

TEST_F(ServiceTest, InertServiceNeverReachesTheProviderOrThePlatform)
{
    auto expansion = add("player", owner_);
    service_->shutdown();

    const auto calls_after_shutdown = expansion->request_calls;
    const auto logs_after_shutdown = platform_->logger.records.size();

    for (int i = 0; i < 10; ++i) {
        (void)service_->setPlaceholders(nullptr, "{player.x}");
        (void)service_->getExpansions();
        (void)service_->isRegistered("player");
    }

    EXPECT_EQ(expansion->request_calls, calls_after_shutdown);
    EXPECT_EQ(platform_->logger.records.size(), logs_after_shutdown);
}

TEST_F(ServiceTest, ShutdownCleansEveryEntryAndReleasesProviders)
{
    int destroyed = 0;
    {
        auto tracked = std::make_shared<LifetimeTrackingExpansion>("tracked", destroyed);
        ASSERT_TRUE(service_->registerExpansion(owner_, tracked));
    }
    auto observed = add("observed", other_owner_);

    EXPECT_EQ(destroyed, 0);
    service_->shutdown();

    EXPECT_EQ(observed->unregister_calls, 1);
    EXPECT_EQ(observed->last_unregister_reason, UnregisterReason::PapiShutdown);
    EXPECT_EQ(destroyed, 1);
    EXPECT_TRUE(service_->getExpansions().empty());
}

TEST_F(ServiceTest, ShutdownSuppressesUnregisterEvents)
{
    add("demo", owner_);
    platform_->events.clear();

    service_->shutdown();

    EXPECT_EQ(countEventsNamed("ExpansionUnregisteredEvent"), 0U);
}

TEST_F(ServiceTest, ShutdownIsIdempotent)
{
    auto expansion = add("demo", owner_);

    service_->shutdown();
    service_->shutdown();
    service_->shutdown();

    EXPECT_EQ(expansion->unregister_calls, 1);
    EXPECT_FALSE(service_->isActive());
}

TEST_F(ServiceTest, ShutdownSplitLeavesServiceNonOperationalBeforeDraining)
{
    auto expansion = add("demo", owner_);

    EXPECT_TRUE(service_->beginShutdown());
    EXPECT_FALSE(service_->isActive());
    EXPECT_EQ(expansion->unregister_calls, 0);

    EXPECT_FALSE(service_->beginShutdown());

    bool callback_saw_active = true;
    expansion->on_unregister = [&](UnregisterReason) {
        callback_saw_active = service_->isActive();
    };

    service_->finishShutdown();

    EXPECT_EQ(expansion->unregister_calls, 1);
    EXPECT_EQ(expansion->last_unregister_reason, UnregisterReason::PapiShutdown);
    EXPECT_FALSE(callback_saw_active);
    EXPECT_FALSE(service_->isActive());
    EXPECT_FALSE(service_->isRegistered("demo"));

    service_->finishShutdown();
    EXPECT_EQ(expansion->unregister_calls, 1);
}

TEST_F(ServiceTest, OwnerDisableRemovesOnlyThatOwnersExpansions)
{
    auto mine_a = add("alpha", owner_);
    auto mine_b = add("beta", owner_);
    auto theirs = add("theirs", other_owner_);

    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);

    EXPECT_EQ(mine_a->unregister_calls, 1);
    EXPECT_EQ(mine_a->last_unregister_reason, UnregisterReason::OwnerDisabled);
    EXPECT_EQ(mine_b->unregister_calls, 1);
    EXPECT_EQ(theirs->unregister_calls, 0);
    EXPECT_TRUE(service_->isRegistered("theirs"));
    EXPECT_FALSE(service_->isRegistered("alpha"));
}

TEST_F(ServiceTest, DependencyDisableRemovesDependentsOwnedElsewhere)
{
    FakePlugin economy{"economy"};
    platform_->enable(economy);

    auto dependent = std::make_shared<FakeExpansion>("shop");
    dependent->required_plugin = "economy";
    ASSERT_TRUE(service_->registerExpansion(owner_, dependent));
    auto unrelated = add("unrelated", owner_);

    platform_->disable(economy);
    service_->handlePluginDisabled(economy);

    EXPECT_EQ(dependent->unregister_calls, 1);
    EXPECT_EQ(dependent->last_unregister_reason, UnregisterReason::RequiredPluginDisabled);
    EXPECT_EQ(unrelated->unregister_calls, 0);
    EXPECT_TRUE(service_->isRegistered("unrelated"));
}

TEST_F(ServiceTest, DependencyReturningDoesNotAutoRegisterAnything)
{
    FakePlugin economy{"economy"};
    platform_->enable(economy);
    auto dependent = std::make_shared<FakeExpansion>("shop");
    dependent->required_plugin = "economy";
    ASSERT_TRUE(service_->registerExpansion(owner_, dependent));

    platform_->disable(economy);
    service_->handlePluginDisabled(economy);
    ASSERT_FALSE(service_->isRegistered("shop"));

    platform_->enable(economy);
    EXPECT_FALSE(service_->isRegistered("shop"));
}

TEST_F(ServiceTest, CleanupExceptionsDoNotStopRemainingCleanups)
{
    auto exploding = std::make_shared<FakeExpansion>("alpha");
    exploding->throw_from_unregister = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, exploding));
    auto healthy = add("beta", owner_);

    platform_->disable(owner_);
    EXPECT_NO_THROW(service_->handlePluginDisabled(owner_));

    EXPECT_EQ(exploding->unregister_calls, 1);
    EXPECT_EQ(healthy->unregister_calls, 1);
    EXPECT_EQ(service_->getExpansions().size(), 0U);
}

TEST_F(ServiceTest, ProactiveUnregisterThenOwnerDisableIsIdempotent)
{
    auto expansion = add("demo", owner_);

    ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_EQ(expansion->unregister_calls, 1);

    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_EQ(expansion->unregister_calls, 1);
}

TEST_F(ServiceTest, PlayerQuitReachesOnlyExpansionsThatOptedIn)
{
    auto opted_in = std::make_shared<FakeExpansion>("cleanup");
    opted_in->player_cleanup = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, opted_in));
    auto plain = add("plain", owner_);

    service_->handlePlayerQuit(alice_);
    EXPECT_EQ(opted_in->player_quit_calls, 1);
    EXPECT_EQ(plain->player_quit_calls, 0);
    EXPECT_TRUE(service_->isRegistered("cleanup"));
}

TEST_F(ServiceTest, PlayerQuitExceptionsAreContainedAndThrottled)
{
    auto exploding = std::make_shared<FakeExpansion>("cleanup");
    exploding->player_cleanup = true;
    exploding->throw_from_player_quit = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, exploding));

    for (int i = 0; i < 20; ++i) {
        EXPECT_NO_THROW(service_->handlePlayerQuit(alice_));
    }
    EXPECT_EQ(exploding->player_quit_calls, 20);
    EXPECT_EQ(platform_->logger.countAtLeast(endstone::Logger::Error), 1U);
}

TEST_F(ServiceTest, LifecycleHooksDoNothingOnAnInertService)
{
    auto expansion = add("demo", owner_);
    service_->shutdown();
    const auto calls = expansion->unregister_calls;

    EXPECT_NO_THROW(service_->handlePluginDisabled(owner_));
    EXPECT_NO_THROW(service_->handlePlayerQuit(alice_));
    EXPECT_EQ(expansion->unregister_calls, calls);
}

TEST_F(ServiceTest, ExpansionCanUnregisterItselfFromInsideItsCallback)
{
    auto expansion = std::make_shared<FakeExpansion>("player");
    auto *service = service_.get();
    auto *owner = &owner_;
    expansion->on_request = [service, owner](const endstone::OfflinePlayer *,
                                             std::string_view) -> std::optional<std::string> {
        service->unregisterExpansion(*owner, "player");
        return "value";
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "{player.x}");
    EXPECT_FALSE(service_->isRegistered("player"));
    EXPECT_EQ(expansion->unregister_calls, 1);

    const auto calls = expansion->request_calls;
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player.x}"), "{player.x}");
    EXPECT_EQ(expansion->request_calls, calls);
}

TEST_F(ServiceTest, ProviderIsDestroyedBeforeItsModuleCouldUnload)
{
    int destroyed = 0;
    {
        auto tracked = std::make_shared<LifetimeTrackingExpansion>("tracked", destroyed);
        ASSERT_TRUE(service_->registerExpansion(owner_, tracked));
    }
    EXPECT_EQ(destroyed, 0);

    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_EQ(destroyed, 1);
}

TEST_F(ServiceTest, PipeSyntaxIsNoLongerRecognized)
{
    auto expansion = add("player", owner_);

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player|name}"), "{player|name}");
    EXPECT_EQ(expansion->request_calls, 0);
}

TEST_F(ServiceTest, UnderscoreSeparatorIsNotRecognized)
{
    auto expansion = add("player", owner_);

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{player_name}"), "{player_name}");
    EXPECT_EQ(expansion->request_calls, 0);
}

}  // namespace
