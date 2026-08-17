// Ordinary parser and contains behavior: test matrix.

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/parser/bracket_replacer.h"

namespace {

using papi::detail::containsPlaceholders;
using papi::detail::PlaceholderResolver;
using papi::detail::replacePlaceholders;

/**
 * @brief A resolver that records every lookup so dispatch can be asserted exactly.
 */
class RecordingResolver final : public PlaceholderResolver {
public:
    struct Lookup {
        std::string identifier;
        std::string params;
    };

    [[nodiscard]] std::optional<std::string> resolve(const std::string_view canonical_identifier,
                                                     const std::string_view params) override
    {
        lookups.push_back(Lookup{.identifier = std::string(canonical_identifier), .params = std::string(params)});
        if (handler) {
            return handler(canonical_identifier, params);
        }
        if (canonical_identifier != "player") {
            return std::nullopt;
        }
        return "<" + std::string(params) + ">";
    }

    std::function<std::optional<std::string>(std::string_view, std::string_view)> handler;
    std::vector<Lookup> lookups;
};

std::string parse(const std::string_view text, RecordingResolver &resolver)
{
    return replacePlaceholders(text, resolver);
}

TEST(BracketReplacer, ReplacesASimplePlaceholder)
{
    RecordingResolver resolver;
    resolver.handler = [](const std::string_view identifier,
                          const std::string_view params) -> std::optional<std::string> {
        if (identifier == "player" && params == "name") {
            return "Alex";
        }
        return std::nullopt;
    };

    EXPECT_EQ(parse("{player.name}", resolver), "Alex");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "name");
}

TEST(BracketReplacer, LowercasesTheIdentifierAndPreservesParameterCase)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{PLAYER.NaMe}", resolver), "<NaMe>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "NaMe");
}

TEST(BracketReplacer, SplitsOnlyAtTheFirstDot)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player.name_first}", resolver), "<name_first>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "name_first");

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player.___}", resolver), "<___>");
    EXPECT_EQ(resolver.lookups[0].params, "___");
}

TEST(BracketReplacer, RequiresADot)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player}", resolver), "{player}");
    EXPECT_EQ(parse("{player_name}", resolver), "{player_name}");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, DispatchesWithEmptyParameters)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, const std::string_view params) -> std::optional<std::string> {
        return params.empty() ? std::optional<std::string>("empty") : std::nullopt;
    };

    EXPECT_EQ(parse("{player.}", resolver), "empty");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "");
}

TEST(BracketReplacer, EmptyIdentifierAndEmptyBracesStayLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{.x}", resolver), "{.x}");
    EXPECT_EQ(parse("{}", resolver), "{}");
    EXPECT_EQ(parse("{.}", resolver), "{.}");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, UnknownIdentifierStaysLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{unknown.x}", resolver), "{unknown.x}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "unknown");
}

TEST(BracketReplacer, DeclinedValueKeepsTheOriginalToken)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };

    EXPECT_EQ(parse("{player.x}", resolver), "{player.x}");
    EXPECT_EQ(resolver.lookups.size(), 1U);
}

TEST(BracketReplacer, EmptyStringIsAValidValue)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::string();
    };

    EXPECT_EQ(parse("a{player.x}b", resolver), "ab");
}

TEST(BracketReplacer, ReplacesSeveralPlaceholdersInOrderAndKeepsSurroundingText)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("A{player.x}B{player.y}C", resolver), "A<x>B<y>C");
    ASSERT_EQ(resolver.lookups.size(), 2U);
    EXPECT_EQ(resolver.lookups[0].params, "x");
    EXPECT_EQ(resolver.lookups[1].params, "y");
}

TEST(BracketReplacer, AdjacentReplacementsConcatenate)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player.x}{player.y}", resolver), "<x><y>");
    EXPECT_EQ(resolver.lookups.size(), 2U);
}

TEST(BracketReplacer, MalformedBracesStayLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player.x", resolver), "{player.x");
    EXPECT_EQ(parse("player.x}", resolver), "player.x}");
    EXPECT_EQ(parse("}{", resolver), "}{");
    EXPECT_EQ(parse("abc}", resolver), "abc}");
    EXPECT_EQ(parse("abc{player.x", resolver), "abc{player.x");
    EXPECT_EQ(parse("{", resolver), "{");
    EXPECT_EQ(parse("}", resolver), "}");
    EXPECT_EQ(parse("}}}", resolver), "}}}");
    EXPECT_EQ(parse("{{{", resolver), "{{{");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, LeadingClosingBraceDoesNotBlockALaterToken)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("}{player.x}", resolver), "}<x>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].params, "x");
}

TEST(BracketReplacer, SpaceBeforeTheSeparatorInvalidatesTheToken)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{ player.x}", resolver), "{ player.x}");
    EXPECT_EQ(parse("{play er.x}", resolver), "{play er.x}");
    EXPECT_EQ(parse("{player .x}", resolver), "{player .x}");
    EXPECT_EQ(parse("{\tplayer.x}", resolver), "{\tplayer.x}");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, ParametersPreserveSpacesAndPunctuation)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player.hello world}", resolver), "<hello world>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].params, "hello world");

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player.a:b%c-d.e/f}", resolver), "<a:b%c-d.e/f>");
    EXPECT_EQ(resolver.lookups[0].params, "a:b%c-d.e/f");

    resolver.lookups.clear();
    (void)parse("{player. }", resolver);
    EXPECT_EQ(resolver.lookups[0].params, " ");
}

TEST(BracketReplacer, ReplacementValueIsNotReparsed)
{
    RecordingResolver resolver;
    resolver.handler = [](const std::string_view identifier, std::string_view) -> std::optional<std::string> {
        if (identifier == "player") {
            return "{other.x}";
        }
        return "SHOULD NOT HAPPEN";
    };

    EXPECT_EQ(parse("{player.x}", resolver), "{other.x}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
}

TEST(BracketReplacer, BraceRichReplacementsSurviveVerbatim)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return "{{}}{a.b}}}{{";
    };

    EXPECT_EQ(parse("{player.x}", resolver), "{{}}{a.b}}}{{");
    EXPECT_EQ(resolver.lookups.size(), 1U);
}

TEST(BracketReplacer, NestingIsNotRecognized)
{
    RecordingResolver resolver;

    EXPECT_EQ(parse("{{player.x}", resolver), "{{player.x}");
    EXPECT_TRUE(resolver.lookups.empty());

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player.{other.x}}", resolver), "<{other.x>}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "{other.x");
}

TEST(BracketReplacer, InputWithoutCandidatesIsReturnedUnchangedWithoutLookups)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("", resolver), "");
    EXPECT_EQ(parse("plain text", resolver), "plain text");
    EXPECT_EQ(parse("no braces at all _ % : . ", resolver), "no braces at all _ % : . ");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, BinaryAndUtf8BytesPassThroughUnchanged)
{
    RecordingResolver resolver;
    const std::string text = std::string("caf\xc3\xa9 \xff\xfe\0", 9) + std::string("\0mid", 4);
    EXPECT_EQ(parse(text, resolver), text);

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player.caf\xc3\xa9}", resolver), "<caf\xc3\xa9>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].params, "caf\xc3\xa9");
}

TEST(BracketReplacer, LongAdversarialInputCompletesInLinearTime)
{
    RecordingResolver resolver;

    std::string text;
    text.reserve(4'000'000);
    for (int i = 0; i < 100'000; ++i) {
        text += "{player.x}";
        text += "{unknown.y}";
        text += "{no_close";
        text += "}}";
        text += "{{";
    }

    const auto start = std::chrono::steady_clock::now();
    const auto output = replacePlaceholders(text, resolver);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(output.empty());
    EXPECT_LT(elapsed, std::chrono::seconds{10});
    EXPECT_NE(output.find("<x>"), std::string::npos);
    EXPECT_NE(output.find("{unknown.y}"), std::string::npos);
}

TEST(BracketReplacer, ManyOpeningBracesDoNotDegradeToQuadratic)
{
    RecordingResolver resolver;

    const std::string text = std::string(500'000, '{') + "player.x}" + std::string(500'000, '{');

    const auto start = std::chrono::steady_clock::now();
    const auto output = replacePlaceholders(text, resolver);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(output, text);
    EXPECT_LT(elapsed, std::chrono::seconds{10});
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, ScanIsMonotonicAndLosesNoBytes)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };

    const std::vector<std::string> inputs = {
        "{player.x}",      "a{player.x}b",     "{a}{b}{c}",   "{{{a.b}}}", "}{}{", "{a.b}{c.d}{e.f}",
        "{no_close{a.b}",  "text{a.b}text{c", "{}{}{}",      "{.}{.}",      "{a.b", "}}}{{{ ",
    };
    for (const auto &input : inputs) {
        EXPECT_EQ(replacePlaceholders(input, resolver), input) << "input='" << input << "'";
    }
}

TEST(Contains, TrueWheneverAnOpeningBraceHasALaterClosingBrace)
{
    EXPECT_TRUE(containsPlaceholders("{}"));
    EXPECT_TRUE(containsPlaceholders("{player}"));
    EXPECT_TRUE(containsPlaceholders("{unknown.x}"));
    EXPECT_TRUE(containsPlaceholders("{player.name}"));
    EXPECT_TRUE(containsPlaceholders("a{}b"));
    EXPECT_TRUE(containsPlaceholders("{ }"));
    EXPECT_TRUE(containsPlaceholders("{{}}"));
    EXPECT_TRUE(containsPlaceholders("{no_close{a}"));
}

TEST(Contains, FalseWithoutABracePair)
{
    EXPECT_FALSE(containsPlaceholders(""));
    EXPECT_FALSE(containsPlaceholders("}{"));
    EXPECT_FALSE(containsPlaceholders("{open"));
    EXPECT_FALSE(containsPlaceholders("plain"));
    EXPECT_FALSE(containsPlaceholders("}"));
    EXPECT_FALSE(containsPlaceholders("{"));
    EXPECT_FALSE(containsPlaceholders("}}}"));
    EXPECT_FALSE(containsPlaceholders("{{{"));
    EXPECT_FALSE(containsPlaceholders("}text{"));
}

TEST(Contains, IgnoresValidityAndRegistration)
{
    EXPECT_TRUE(containsPlaceholders("{.}"));
    EXPECT_TRUE(containsPlaceholders("{ player.x}"));
    EXPECT_TRUE(containsPlaceholders("{plugin:demo.x}"));
    EXPECT_TRUE(containsPlaceholders("{日本.x}"));
}

}  // namespace
