// Ownership, teardown ordering, exception containment, and module-unload guarantees.

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fake_player.h"
#include "fakes.h"
#include "platform/endstone/bootstrap.h"

namespace {

using papi::UnregisterReason;
using papi::detail::PlaceholderApiImpl;
using papi::testing::FakeExpansion;
using papi::testing::FakePlatform;
using papi::testing::FakePlayer;
using papi::testing::FakePlugin;

/**
 * @brief An expansion that models code living in a separately loaded module.
 *
 * A real C++ provider's virtual functions live in its own DLL or shared object. Once
 * Endstone unloads that module, calling into the object is undefined behavior, so this
 * fake makes the same mistake detectable: unload() marks the module gone, and any
 * later call fails the test instead of silently appearing to work.
 */
class ModuleBackedExpansion final : public papi::PlaceholderExpansion {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    ModuleBackedExpansion(std::string identifier, bool &module_loaded, int &destructions)
        : identifier_(std::move(identifier)), module_loaded_(module_loaded), destructions_(destructions)
    {
    }

    ~ModuleBackedExpansion() override  // NOLINT(bugprone-exception-escape)
    {
        EXPECT_TRUE(module_loaded_) << "expansion destroyed after its module was unloaded";
        ++destructions_;
    }

    [[nodiscard]] std::string getIdentifier() const override { return identifier_; }
    [[nodiscard]] std::string getAuthor() const override { return "author"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *, const std::string_view) override
    {
        EXPECT_TRUE(module_loaded_) << "onRequest called after its module was unloaded";
        return "value";
    }

    void onUnregister(UnregisterReason) override
    {
        EXPECT_TRUE(module_loaded_) << "onUnregister called after its module was unloaded";
    }

private:
    std::string identifier_;
    bool &module_loaded_;
    int &destructions_;
};

/**
 * @brief An expansion that throws something that is not a std::exception.
 */
class NonStandardThrowingExpansion final : public papi::PlaceholderExpansion {
public:
    [[nodiscard]] std::string getIdentifier() const override { return "rogue"; }
    [[nodiscard]] std::string getAuthor() const override { return "author"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *, const std::string_view) override
    {
        throw 42;
    }

    void onUnregister(UnregisterReason) override { throw std::string("not a std::exception"); }
};

class LifecycleTest : public ::testing::Test {
protected:
    LifecycleTest()
    {
        platform_ = std::make_shared<FakePlatform>();
        platform_->enable(papi_plugin_);
        platform_->enable(owner_);
        service_ = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    std::shared_ptr<FakePlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
    FakePlugin papi_plugin_{"papi"};
    FakePlugin owner_{"provider"};
};

// With module semantics, the provider object must be gone before its module.
TEST_F(LifecycleTest, ProviderIsReleasedBeforeItsModuleUnloads)
{
    bool module_loaded = true;
    int destructions = 0;
    {
        auto expansion = std::make_shared<ModuleBackedExpansion>("demo", module_loaded, destructions);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }
    ASSERT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "value");

    // Endstone fires the disable event, then later unloads the module.
    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_EQ(destructions, 1);

    module_loaded = false;
    // Nothing may reach the provider from here on.
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");
}

TEST_F(LifecycleTest, PapiShutdownReleasesProvidersBeforeModulesUnload)
{
    bool module_loaded = true;
    int destructions = 0;
    {
        auto expansion = std::make_shared<ModuleBackedExpansion>("demo", module_loaded, destructions);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }

    service_->shutdown();
    EXPECT_EQ(destructions, 1);

    module_loaded = false;
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");
}

TEST_F(LifecycleTest, ExplicitUnregisterReleasesTheProviderImmediately)
{
    bool module_loaded = true;
    int destructions = 0;
    {
        auto expansion = std::make_shared<ModuleBackedExpansion>("demo", module_loaded, destructions);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }

    ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_EQ(destructions, 1);
    module_loaded = false;
}

// A server reload disables and clears every plugin, then enables them again.
TEST_F(LifecycleTest, ServerReloadLeavesNoCallableBehindAndAllowsFreshRegistration)
{
    bool first_module = true;
    int first_destructions = 0;
    {
        auto expansion = std::make_shared<ModuleBackedExpansion>("demo", first_module, first_destructions);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }

    // Disable the provider, then PAPI, in the reverse order Endstone uses.
    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_EQ(first_destructions, 1);

    service_->shutdown();
    platform_->disable(papi_plugin_);
    first_module = false;

    // Reload: fresh PAPI service, fresh provider instance.
    platform_->enable(papi_plugin_);
    platform_->enable(owner_);
    auto reloaded = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());

    bool second_module = true;
    int second_destructions = 0;
    {
        auto expansion = std::make_shared<ModuleBackedExpansion>("demo", second_module, second_destructions);
        ASSERT_TRUE(reloaded->registerExpansion(owner_, expansion));
    }
    EXPECT_EQ(reloaded->setPlaceholders(nullptr, "{demo_x}"), "value");

    // The old service stayed inert throughout and never touched the new registration.
    EXPECT_FALSE(service_->isActive());
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");

    reloaded->shutdown();
    EXPECT_EQ(second_destructions, 1);
    second_module = false;
}

// PAPI's own disable must not depend on receiving its own disable event.
TEST_F(LifecycleTest, PapiTearsDownWithoutRelyingOnItsOwnDisableEvent)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    // Endstone runs onDisable first, which is where PAPI tears down directly.
    service_->shutdown();
    EXPECT_EQ(expansion->unregister_calls, 1);
    EXPECT_FALSE(service_->isActive());

    // The disable event arrives afterwards, once PAPI is already inactive. It must be
    // a harmless no-op rather than a second teardown.
    platform_->disable(papi_plugin_);
    EXPECT_NO_THROW(service_->handlePluginDisabled(papi_plugin_));
    EXPECT_EQ(expansion->unregister_calls, 1);
}

// A metadata query must not wait on provider code.
TEST_F(LifecycleTest, MetadataQueriesCompleteWhileAProviderCallbackIsBlocked)
{
    std::atomic<bool> inside_callback{false};
    std::atomic<bool> may_return{false};

    auto expansion = std::make_shared<FakeExpansion>("slow");
    expansion->on_request = [&](const endstone::OfflinePlayer *, std::string_view) -> std::optional<std::string> {
        inside_callback.store(true, std::memory_order_release);
        while (!may_return.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return "value";
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    std::thread parser([&] { EXPECT_EQ(service_->setPlaceholders(nullptr, "{slow_x}"), "value"); });

    while (!inside_callback.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // The provider is mid-callback and therefore holds a call lease. Queries must
    // still complete, which proves no registry lock spans provider code.
    EXPECT_EQ(service_->getExpansions().size(), 1U);
    EXPECT_EQ(service_->getRegisteredIdentifiers().size(), 1U);
    EXPECT_TRUE(service_->isRegistered("slow"));
    EXPECT_TRUE(service_->containsPlaceholders("{slow_x}"));

    may_return.store(true, std::memory_order_release);
    parser.join();
}

TEST_F(LifecycleTest, UnregisterIsVisibleImmediatelyEvenWhileACallbackRuns)
{
    std::atomic<bool> inside_callback{false};
    std::atomic<bool> may_return{false};

    auto expansion = std::make_shared<FakeExpansion>("slow");
    expansion->on_request = [&](const endstone::OfflinePlayer *, std::string_view) -> std::optional<std::string> {
        inside_callback.store(true, std::memory_order_release);
        while (!may_return.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return "value";
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    std::thread parser([&] {
        // The entry retires mid-call, so the answer is discarded.
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{slow_x}"), "{slow_x}");
    });

    while (!inside_callback.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    EXPECT_TRUE(service_->unregisterExpansion(owner_, "slow"));
    EXPECT_FALSE(service_->isRegistered("slow"));
    // Cleanup waits for the in-flight callback rather than running underneath it.
    EXPECT_EQ(expansion->unregister_calls, 0);

    may_return.store(true, std::memory_order_release);
    parser.join();
    EXPECT_EQ(expansion->unregister_calls, 1);
}

// The ExpansionUnregisteredEvent must fire after onUnregister and provider
// release, not before them. When cleanup is deferred by an active call lease, the
// event is dispatched from the deferred cleanup continuation, so a listener that
// checks cleanup state sees a fully torn-down expansion.
TEST_F(LifecycleTest, UnregisterEventFiresAfterDeferredCleanup)
{
    std::atomic<bool> inside_callback{false};
    std::atomic<bool> may_return{false};

    auto expansion = std::make_shared<FakeExpansion>("slow");
    expansion->on_request = [&](const endstone::OfflinePlayer *, std::string_view) -> std::optional<std::string> {
        inside_callback.store(true, std::memory_order_release);
        while (!may_return.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return "value";
    };
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    std::thread parser([&] { EXPECT_EQ(service_->setPlaceholders(nullptr, "{slow_x}"), "{slow_x}"); });

    while (!inside_callback.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    platform_->events.clear();
    EXPECT_TRUE(service_->unregisterExpansion(owner_, "slow"));
    // Cleanup is deferred: the event must not have fired yet.
    EXPECT_EQ(expansion->unregister_calls, 0);
    EXPECT_TRUE(platform_->events.empty());

    may_return.store(true, std::memory_order_release);
    parser.join();

    // After the callback returns, the deferred cleanup runs onUnregister and then
    // dispatches the event.
    EXPECT_EQ(expansion->unregister_calls, 1);
    ASSERT_EQ(platform_->events.size(), 1U);
    EXPECT_EQ(platform_->events[0].name, "ExpansionUnregisteredEvent");
    EXPECT_EQ(platform_->events[0].reason, UnregisterReason::Explicit);
}
TEST_F(LifecycleTest, NonStandardExceptionsAreContained)
{
    auto expansion = std::make_shared<NonStandardThrowingExpansion>();
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rogue_x}"), "{rogue_x}");
    EXPECT_TRUE(platform_->logger.anyContains("unknown C++ exception"));

    // A throwing cleanup must also not escape, and removal must still complete.
    EXPECT_NO_THROW(service_->shutdown());
    EXPECT_TRUE(service_->getExpansions().empty());
}

TEST_F(LifecycleTest, ServiceStaysUsableAfterAProviderFails)
{
    auto broken = std::make_shared<FakeExpansion>("broken");
    broken->throw_from_request = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, broken));
    auto healthy = std::make_shared<FakeExpansion>("healthy");
    healthy->value = "ok";
    ASSERT_TRUE(service_->registerExpansion(owner_, healthy));

    // One broken expansion must not take the rest of the parse down with it.
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{broken_a}/{healthy_b}"), "{broken_a}/ok");
    EXPECT_TRUE(service_->isActive());
}

// Throttle state is released with its entry at the service level.
TEST_F(LifecycleTest, ThrottleStateDoesNotAccumulateAcrossRegistrations)
{
    for (int round = 0; round < 200; ++round) {
        auto expansion = std::make_shared<FakeExpansion>("demo");
        expansion->throw_from_request = true;
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
        (void)service_->setPlaceholders(nullptr, "{demo_x}");
        ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    }

    // Every generation logged once and then had its state dropped, so memory stays
    // bounded no matter how often providers churn.
    EXPECT_EQ(service_->getExpansions().size(), 0U);
    EXPECT_GT(platform_->logger.countAtLeast(endstone::Logger::Error), 0U);
}

TEST_F(LifecycleTest, PlayerCleanupUsesRealPlayerIdentity)
{
    FakePlayer alice{"Alice", endstone::UUID{{1}}};
    FakePlayer bob{"Bob", endstone::UUID{{2}}};

    auto expansion = std::make_shared<FakeExpansion>("cleanup");
    expansion->player_cleanup = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    service_->handlePlayerQuit(alice);
    ASSERT_EQ(expansion->player_quit_calls, 1);
    EXPECT_EQ(expansion->last_quit_player, &alice);
    EXPECT_EQ(expansion->last_quit_player->getName(), "Alice");

    service_->handlePlayerQuit(bob);
    EXPECT_EQ(expansion->last_quit_player, &bob);
}

TEST_F(LifecycleTest, ProviderReceivesTheOfflinePlayerItWasGiven)
{
    FakePlayer alice{"Alice", endstone::UUID{{1}}};
    auto expansion = std::make_shared<FakeExpansion>("player");
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    (void)service_->setPlaceholders(&alice, "{player_name}");
    ASSERT_NE(expansion->last_player, nullptr);
    EXPECT_EQ(expansion->last_player->getName(), "Alice");
    EXPECT_EQ(expansion->last_player->getUniqueId(), alice.getUniqueId());

    // a null player is a supported input, not an error.
    (void)service_->setPlaceholders(nullptr, "{player_name}");
    EXPECT_EQ(expansion->last_player, nullptr);
}

// The containment boundary is documented honestly. Only C++ exceptions are
// caught; access violations, signals, and undefined behavior are not claimed.
TEST_F(LifecycleTest, ContainmentCoversExceptionsOnly)
{
    auto std_thrower = std::make_shared<FakeExpansion>("stdthrow");
    std_thrower->throw_from_request = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, std_thrower));
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{stdthrow_x}"), "{stdthrow_x}");

    auto non_std = std::make_shared<NonStandardThrowingExpansion>();
    ASSERT_TRUE(service_->registerExpansion(owner_, non_std));
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rogue_x}"), "{rogue_x}");

    EXPECT_TRUE(service_->isActive());
}

}  // namespace
