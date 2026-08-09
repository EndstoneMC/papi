// Relational placeholder dispatch: test matrix REL-001 through REL-008.

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fake_player.h"
#include "fakes.h"

namespace {

using papi::detail::PlaceholderApiImpl;
using papi::testing::FakeExpansion;
using papi::testing::FakePlatform;
using papi::testing::FakePlayer;
using papi::testing::FakePlugin;

class RelationalTest : public ::testing::Test {
protected:
    RelationalTest()
    {
        platform_ = std::make_shared<FakePlatform>();
        platform_->enable(papi_plugin_);
        platform_->enable(owner_);
        service_ = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    /**
     * @brief Registers a relational expansion and returns it.
     */
    std::shared_ptr<FakeExpansion> addRelational(const std::string &identifier)
    {
        auto expansion = std::make_shared<FakeExpansion>(identifier);
        expansion->relational = true;
        EXPECT_TRUE(service_->registerExpansion(owner_, expansion));
        return expansion;
    }

    std::shared_ptr<FakePlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
    FakePlugin papi_plugin_{"papi"};
    FakePlugin owner_{"provider"};
    FakePlayer alice_{"Alice", endstone::UUID{{1}}};
    FakePlayer bob_{"Bob", endstone::UUID{{2}}};
};

// REL-001
TEST_F(RelationalTest, ResolvesWithBothExactPlayers)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "since-yesterday";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_since}"), "since-yesterday");

    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "since");
    // The two players arrive as the exact objects the caller supplied, in order.
    EXPECT_EQ(expansion->last_relational_one, &alice_);
    EXPECT_EQ(expansion->last_relational_two, &bob_);
    EXPECT_EQ(expansion->last_relational_one->getName(), "Alice");
    EXPECT_EQ(expansion->last_relational_two->getName(), "Bob");
}

TEST_F(RelationalTest, PlayerOrderIsPreserved)
{
    auto expansion = addRelational("friends");

    (void)service_->setRelationalPlaceholders(bob_, alice_, "{rel_friends_x}");
    EXPECT_EQ(expansion->last_relational_one, &bob_);
    EXPECT_EQ(expansion->last_relational_two, &alice_);
}

// REL-002
TEST_F(RelationalTest, IdentifierIsLowercasedAndParamsPreserved)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "value";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_FRIENDS_Since_When}"), "value");
    ASSERT_EQ(expansion->relational_calls, 1);
    // Only the identifier is canonicalized; parameters keep their exact bytes.
    EXPECT_EQ(expansion->last_relational_params, "Since_When");
}

// REL-003
TEST_F(RelationalTest, UnknownIdentifierStaysLiteral)
{
    addRelational("friends");

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_missing_x}"), "{rel_missing_x}");
}

// REL-004: a registered but non-relational expansion must not be consulted at all.
TEST_F(RelationalTest, NonRelationalExpansionIsNotDispatched)
{
    auto ordinary = std::make_shared<FakeExpansion>("friends");
    ordinary->relational = false;
    ordinary->value = "ordinary-value";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_since}"), "{rel_friends_since}");
    // Neither callback runs: capability is a copied flag, so the ordinary path is not a
    // fallback for a relational request.
    EXPECT_EQ(ordinary->relational_calls, 0);
    EXPECT_EQ(ordinary->request_calls, 0);
}

// REL-005
TEST_F(RelationalTest, NullValueAndExceptionsStayLiteral)
{
    auto declining = addRelational("friends");
    declining->relational_value = std::nullopt;
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}"), "{rel_friends_x}");

    auto throwing = addRelational("family");
    throwing->throw_from_relational = true;
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_family_x}"), "{rel_family_x}");

    EXPECT_TRUE(service_->isActive());
    EXPECT_TRUE(platform_->logger.anyContains("relational"));
}

TEST_F(RelationalTest, RepeatedRelationalFailuresAreThrottled)
{
    std::chrono::steady_clock::time_point now{};
    service_->setErrorClock([&now] { return now; });

    auto expansion = addRelational("friends");
    expansion->throw_from_relational = true;

    for (int i = 0; i < 50; ++i) {
        (void)service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}");
    }
    EXPECT_EQ(platform_->logger.countAtLeast(endstone::Logger::Error), 1U);
}

// REL-006
TEST_F(RelationalTest, MalformedRelationalTokensStayLiteral)
{
    addRelational("friends");

    const std::vector<std::string> malformed = {
        "{rel_friends}",    // no parameters after the identifier
        "{rel__x}",         // empty identifier
        "{rel_}",           // nothing after the prefix
        "{rel}",            // no separator at all
        "{rel_friends_x",   // missing close
        "rel_friends_x}",   // missing open
        "{rel_ friends_x}"  // space before the separator
    };

    for (const auto &text : malformed) {
        EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, text), text) << "text='" << text << "'";
    }
}

// REL-007: the two namespaces must not cross-dispatch.
TEST_F(RelationalTest, OrdinaryParsingNeverInvokesTheRelationalCallback)
{
    auto expansion = addRelational("friends");

    // No expansion named "rel" is registered, so the ordinary scan finds nothing.
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rel_friends_since}"), "{rel_friends_since}");
    EXPECT_EQ(expansion->relational_calls, 0);
    EXPECT_EQ(expansion->request_calls, 0);
}

TEST_F(RelationalTest, OrdinaryIdentifierNamedRelIsTreatedNormally)
{
    // "rel" is a perfectly ordinary identifier; registering it must not turn ordinary
    // parsing into relational dispatch.
    auto rel = std::make_shared<FakeExpansion>("rel");
    rel->value = "ordinary";
    ASSERT_TRUE(service_->registerExpansion(owner_, rel));
    auto relational = addRelational("friends");

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rel_friends_since}"), "ordinary");
    ASSERT_EQ(rel->request_calls, 1);
    EXPECT_EQ(rel->last_params, "friends_since");
    EXPECT_EQ(relational->relational_calls, 0);
}

TEST_F(RelationalTest, RelationalParsingNeverInvokesTheOrdinaryCallback)
{
    auto expansion = addRelational("friends");
    expansion->value = "ordinary-value";
    expansion->relational_value = "relational-value";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}"), "relational-value");
    EXPECT_EQ(expansion->request_calls, 0);
}

TEST_F(RelationalTest, OrdinaryTokensAreUntouchedByRelationalParsing)
{
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ordinary->value = "Alex";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    // A relational parse resolves only relational tokens; an ordinary one beside it is
    // left exactly as written.
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{player_name}"), "{player_name}");
    EXPECT_EQ(ordinary->request_calls, 0);
}

TEST_F(RelationalTest, MixedTextResolvesOnlyRelationalTokens)
{
    auto relational = addRelational("friends");
    relational->relational_value = "yes";
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ordinary->value = "Alex";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "a{rel_friends_x}b{player_name}c"),
              "ayesb{player_name}c");
}

TEST_F(RelationalTest, SeveralRelationalTokensResolveInOrder)
{
    auto friends = addRelational("friends");
    friends->relational_value = "F";
    auto family = addRelational("family");
    family->relational_value = "K";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_a}{rel_family_b}"), "FK");
    EXPECT_EQ(friends->last_relational_params, "a");
    EXPECT_EQ(family->last_relational_params, "b");
}

TEST_F(RelationalTest, RelationalValueIsNotReparsed)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "{rel_friends_again}";

    // One pass only: the returned text is output, never fed back to the scanner.
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}"), "{rel_friends_again}");
    EXPECT_EQ(expansion->relational_calls, 1);
}

TEST_F(RelationalTest, EmptyParametersAfterTheIdentifierAreDispatched)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "empty";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_}"), "empty");
    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "");
}

// Relational parsing obeys the same thread and lifecycle policy as ordinary parsing.
TEST_F(RelationalTest, RelationalParsingRequiresThePrimaryThread)
{
    auto expansion = addRelational("friends");
    platform_->primary_thread = false;

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}"), "{rel_friends_x}");
    EXPECT_EQ(expansion->relational_calls, 0);
    EXPECT_TRUE(platform_->logger.anyContains("server thread"));
}

TEST_F(RelationalTest, InertServiceReturnsRelationalInputUnchanged)
{
    auto expansion = addRelational("friends");
    service_->shutdown();

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel_friends_x}"), "{rel_friends_x}");
    EXPECT_EQ(expansion->relational_calls, 0);
}

TEST_F(RelationalTest, RelationalCapabilityAppearsInMetadata)
{
    addRelational("friends");
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    const auto infos = service_->getExpansions();
    ASSERT_EQ(infos.size(), 2U);
    // Sorted by identifier: "friends" then "player".
    EXPECT_TRUE(infos[0].relational);
    EXPECT_FALSE(infos[1].relational);
}

}  // namespace
