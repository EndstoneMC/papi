// Command and event ordering: EVT-001 through EVT-005.

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fake_player.h"
#include "fakes.h"

namespace {

using papi::detail::PlaceholderApiImpl;
using papi::testing::FakeExpansion;
using papi::testing::FakePlatform;
using papi::testing::FakePlugin;

class EventsTest : public ::testing::Test {
protected:
    EventsTest()
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

// EVT-003: at dispatch time, a listener already sees the committed state and cannot
// deadlock against the registry.
TEST_F(EventsTest, RegisteredEventFiresAfterCommit)
{
    bool saw_registered = false;
    platform_->on_event = [&](const papi::testing::RecordedEvent &event) {
        if (event.name == "ExpansionRegisteredEvent") {
            saw_registered = true;
            // The registry must already reflect the commit, so the new expansion is
            // visible while the event is being dispatched.
            ASSERT_EQ(service_->getExpansions().size(), 1U);
            ASSERT_EQ(service_->getExpansions()[0].identifier, "demo");
            ASSERT_TRUE(service_->isRegistered("demo"));
        }
    };

    auto expansion = std::make_shared<FakeExpansion>("demo");
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    EXPECT_TRUE(saw_registered);
}

TEST_F(EventsTest, UnregisteredEventFiresAfterRemoval)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    bool saw_unregistered = false;
    platform_->on_event = [&](const papi::testing::RecordedEvent &event) {
        if (event.name == "ExpansionUnregisteredEvent") {
            saw_unregistered = true;
            // Removed before the event, so a listener querying the registry no longer
            // observes the expansion.
            ASSERT_TRUE(service_->getExpansions().empty());
            ASSERT_FALSE(service_->isRegistered("demo"));
        }
    };

    ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_TRUE(saw_unregistered);
}

// EVT-005: a throwing listener cannot prevent cleanup from finishing.
TEST_F(EventsTest, ThrowingCleanupDoesNotPreventRemoval)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->throw_from_unregister = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    // Even though cleanup itself throws, removal completes and the registry is empty.
    EXPECT_NO_THROW(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_FALSE(service_->isRegistered("demo"));
    EXPECT_EQ(service_->getExpansions().size(), 0U);
}

TEST_F(EventsTest, ThrowingCleanupDoesNotStopTheUnregisteredEvent)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->throw_from_unregister = true;
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    platform_->events.clear();

    ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    // The event still fires after a throwing cleanup, because the removal is committed
    // before cleanup runs and its failure is contained.
    ASSERT_EQ(platform_->events.size(), 1U);
    EXPECT_EQ(platform_->events[0].name, "ExpansionUnregisteredEvent");
    EXPECT_EQ(platform_->events[0].reason, papi::UnregisterReason::Explicit);
}

// EVT-002: a failed registration emits no event.
TEST_F(EventsTest, FailedRegistrationEmitsNoEvent)
{
    platform_->events.clear();

    // Duplicate: register once, then try again.
    auto first = std::make_shared<FakeExpansion>("demo");
    ASSERT_TRUE(service_->registerExpansion(owner_, first));
    platform_->events.clear();

    auto duplicate = std::make_shared<FakeExpansion>("DEMO");
    ASSERT_FALSE(service_->registerExpansion(owner_, duplicate));
    EXPECT_TRUE(platform_->events.empty());

    // Refused preflight.
    auto refused = std::make_shared<FakeExpansion>("other");
    refused->can_register = false;
    ASSERT_FALSE(service_->registerExpansion(owner_, refused));
    EXPECT_TRUE(platform_->events.empty());

    // Invalid identifier.
    auto invalid = std::make_shared<FakeExpansion>("bad id");
    ASSERT_FALSE(service_->registerExpansion(owner_, invalid));
    EXPECT_TRUE(platform_->events.empty());
}

TEST_F(EventsTest, EventsCarryCopiedMetadataOnly)
{
    auto expansion = std::make_shared<FakeExpansion>("Demo");
    expansion->author = "Endstone";
    expansion->version = "2.0";
    ASSERT_TRUE(service_->registerExpansion(owner_, expansion));

    ASSERT_EQ(platform_->events.size(), 1U);
    const auto &event = platform_->events.back();
    EXPECT_EQ(event.name, "ExpansionRegisteredEvent");
    ASSERT_TRUE(event.info.has_value());
    EXPECT_EQ(event.info->identifier, "demo");
    EXPECT_EQ(event.info->author, "Endstone");
    EXPECT_EQ(event.info->version, "2.0");
    EXPECT_EQ(event.info->owner, "provider");

    // Destroy the provider; the snapshot in the event must not dangle.
    ASSERT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_EQ(platform_->events.back().info->identifier, "demo");
    EXPECT_EQ(platform_->events.back().info->version, "2.0");
}

TEST_F(EventsTest, BulkUnregisterEmitsEventsForEachEntry)
{
    ASSERT_TRUE(service_->registerExpansion(owner_, std::make_shared<FakeExpansion>("alpha")));
    ASSERT_TRUE(service_->registerExpansion(owner_, std::make_shared<FakeExpansion>("beta")));
    platform_->events.clear();

    EXPECT_EQ(service_->unregisterExpansions(owner_), 2U);
    EXPECT_EQ(platform_->events.size(), 2U);
    EXPECT_EQ(platform_->events[0].info->identifier, "alpha");
    EXPECT_EQ(platform_->events[1].info->identifier, "beta");
}

TEST_F(EventsTest, OwnerDisableEmitsEventsForEachEntry)
{
    ASSERT_TRUE(service_->registerExpansion(owner_, std::make_shared<FakeExpansion>("alpha")));
    ASSERT_TRUE(service_->registerExpansion(owner_, std::make_shared<FakeExpansion>("beta")));
    platform_->events.clear();

    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_EQ(platform_->events.size(), 2U);
    EXPECT_EQ(platform_->events[0].reason, papi::UnregisterReason::OwnerDisabled);
    EXPECT_EQ(platform_->events[1].reason, papi::UnregisterReason::OwnerDisabled);
}

TEST_F(EventsTest, ShutdownSuppressesUnregisteredEvents)
{
    ASSERT_TRUE(service_->registerExpansion(owner_, std::make_shared<FakeExpansion>("demo")));
    platform_->events.clear();

    service_->shutdown();
    // Listeners are being torn down alongside PAPI, so firing events here would be
    // unpredictable rather than useful.
    EXPECT_TRUE(platform_->events.empty());
}

}  // namespace
