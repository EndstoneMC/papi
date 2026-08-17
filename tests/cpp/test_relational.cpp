// Relational placeholder dispatch: test matrix.

#include <chrono>
#include <vector>

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

TEST_F(RelationalTest, ResolvesWithBothExactPlayers)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "since-yesterday";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:since}"), "since-yesterday");

    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "since");
    EXPECT_EQ(expansion->last_relational_one, &alice_);
    EXPECT_EQ(expansion->last_relational_two, &bob_);
    EXPECT_EQ(expansion->last_relational_one->getName(), "Alice");
    EXPECT_EQ(expansion->last_relational_two->getName(), "Bob");
}

TEST_F(RelationalTest, PlayerOrderIsPreserved)
{
    auto expansion = addRelational("friends");

    (void)service_->setRelationalPlaceholders(bob_, alice_, "{rel:friends:x}");
    EXPECT_EQ(expansion->last_relational_one, &bob_);
    EXPECT_EQ(expansion->last_relational_two, &alice_);
}

TEST_F(RelationalTest, IdentifierIsLowercasedAndParamsPreserved)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "value";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{REL:FRIENDS:Since_When}"), "value");
    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "Since_When");
}

TEST_F(RelationalTest, ParamsPreserveDotsUnderscoresAndLaterColons)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "value";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:a_b.c:d}"), "value");
    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "a_b.c:d");
}

TEST_F(RelationalTest, UnknownIdentifierStaysLiteral)
{
    addRelational("friends");

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:missing:x}"), "{rel:missing:x}");
}

TEST_F(RelationalTest, NonRelationalExpansionIsNotDispatched)
{
    auto ordinary = std::make_shared<FakeExpansion>("friends");
    ordinary->relational = false;
    ordinary->value = "ordinary-value";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:since}"), "{rel:friends:since}");
    EXPECT_EQ(ordinary->relational_calls, 0);
    EXPECT_EQ(ordinary->request_calls, 0);
}

TEST_F(RelationalTest, NullValueAndExceptionsStayLiteral)
{
    auto declining = addRelational("friends");
    declining->relational_value = std::nullopt;
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}"), "{rel:friends:x}");

    auto throwing = addRelational("family");
    throwing->throw_from_relational = true;
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:family:x}"), "{rel:family:x}");

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
        (void)service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}");
    }
    EXPECT_EQ(platform_->logger.countAtLeast(endstone::Logger::Error), 1U);
}

TEST_F(RelationalTest, MalformedRelationalTokensStayLiteral)
{
    addRelational("friends");

    const std::vector<std::string> malformed = {
        "{rel:friends}",    // no relational params after the identifier
        "{rel::x}",         // empty relational identifier
        "{rel:}",           // nothing after the rel namespace
        "{rel}",            // no colon separator
        "{rel:friends:x",   // missing close
        "rel:friends:x}",   // missing open
        "{rel: friends:x}"  // invalid relational identifier
    };

    for (const auto &text : malformed) {
        EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, text), text) << "text='" << text << "'";
    }
}

TEST_F(RelationalTest, LegacyRelationalSeparatorsStayLiteral)
{
    auto expansion = addRelational("friends");

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel.friends_since}"), "{rel.friends_since}");
    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends_since}"), "{rel:friends_since}");
    EXPECT_EQ(expansion->relational_calls, 0);
}

TEST_F(RelationalTest, OrdinaryParsingNeverInvokesTheRelationalCallback)
{
    auto expansion = addRelational("friends");

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rel:friends:since}"), "{rel:friends:since}");
    EXPECT_EQ(expansion->relational_calls, 0);
    EXPECT_EQ(expansion->request_calls, 0);
}

TEST_F(RelationalTest, RelIsReservedAndCannotShadowRelationalSyntax)
{
    auto rel = std::make_shared<FakeExpansion>("rel");
    EXPECT_FALSE(service_->registerExpansion(owner_, rel));

    auto relational = addRelational("friends");
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{rel:friends:since}"), "{rel:friends:since}");
    EXPECT_EQ(relational->relational_calls, 0);
}

TEST_F(RelationalTest, RelationalParsingNeverInvokesTheOrdinaryCallback)
{
    auto expansion = addRelational("friends");
    expansion->value = "ordinary-value";
    expansion->relational_value = "relational-value";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}"), "relational-value");
    EXPECT_EQ(expansion->request_calls, 0);
}

TEST_F(RelationalTest, OrdinaryTokensAreUntouchedByRelationalParsing)
{
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ordinary->value = "Alex";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{player:name}"), "{player:name}");
    EXPECT_EQ(ordinary->request_calls, 0);
}

TEST_F(RelationalTest, MixedTextResolvesOnlyRelationalTokens)
{
    auto relational = addRelational("friends");
    relational->relational_value = "yes";
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ordinary->value = "Alex";
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "a{rel:friends:x}b{player:name}c"),
              "ayesb{player:name}c");
}

TEST_F(RelationalTest, SeveralRelationalTokensResolveInOrder)
{
    auto friends = addRelational("friends");
    friends->relational_value = "F";
    auto family = addRelational("family");
    family->relational_value = "K";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:a}{rel:family:b}"), "FK");
    EXPECT_EQ(friends->last_relational_params, "a");
    EXPECT_EQ(family->last_relational_params, "b");
}

TEST_F(RelationalTest, RelationalValueIsNotReparsed)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "{rel:friends:again}";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}"), "{rel:friends:again}");
    EXPECT_EQ(expansion->relational_calls, 1);
}

TEST_F(RelationalTest, EmptyParametersAfterTheIdentifierAreDispatched)
{
    auto expansion = addRelational("friends");
    expansion->relational_value = "empty";

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:}"), "empty");
    ASSERT_EQ(expansion->relational_calls, 1);
    EXPECT_EQ(expansion->last_relational_params, "");
}

TEST_F(RelationalTest, RelationalParsingRequiresThePrimaryThread)
{
    auto expansion = addRelational("friends");
    platform_->primary_thread = false;

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}"), "{rel:friends:x}");
    EXPECT_EQ(expansion->relational_calls, 0);
    EXPECT_TRUE(platform_->logger.anyContains("server thread"));
}

TEST_F(RelationalTest, InertServiceReturnsRelationalInputUnchanged)
{
    auto expansion = addRelational("friends");
    service_->shutdown();

    EXPECT_EQ(service_->setRelationalPlaceholders(alice_, bob_, "{rel:friends:x}"), "{rel:friends:x}");
    EXPECT_EQ(expansion->relational_calls, 0);
}

TEST_F(RelationalTest, RelationalCapabilityAppearsInMetadata)
{
    addRelational("friends");
    auto ordinary = std::make_shared<FakeExpansion>("player");
    ASSERT_TRUE(service_->registerExpansion(owner_, ordinary));

    const auto infos = service_->getExpansions();
    ASSERT_EQ(infos.size(), 2U);
    EXPECT_TRUE(infos[0].relational);
    EXPECT_FALSE(infos[1].relational);
}

}  // namespace
