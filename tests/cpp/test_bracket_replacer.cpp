// Ordinary parser and contains behavior: test matrix PAR-001 through PAR-024.

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

// PAR-001
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

    EXPECT_EQ(parse("{player_name}", resolver), "Alex");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "name");
}

// PAR-002
TEST(BracketReplacer, LowercasesTheIdentifierAndPreservesParameterCase)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{PLAYER_NaMe}", resolver), "<NaMe>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "NaMe");
}

// PAR-003
TEST(BracketReplacer, SplitsOnlyAtTheFirstUnderscore)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player_name_first}", resolver), "<name_first>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "name_first");

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player___}", resolver), "<__>");
    EXPECT_EQ(resolver.lookups[0].params, "__");
}

// PAR-004
TEST(BracketReplacer, RequiresAnUnderscore)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player}", resolver), "{player}");
    EXPECT_TRUE(resolver.lookups.empty());
}

// PAR-005
TEST(BracketReplacer, DispatchesWithEmptyParameters)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, const std::string_view params) -> std::optional<std::string> {
        return params.empty() ? std::optional<std::string>("empty") : std::nullopt;
    };

    EXPECT_EQ(parse("{player_}", resolver), "empty");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "");
}

// PAR-006
TEST(BracketReplacer, EmptyIdentifierAndEmptyBracesStayLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{_x}", resolver), "{_x}");
    EXPECT_EQ(parse("{}", resolver), "{}");
    EXPECT_EQ(parse("{_}", resolver), "{_}");
    EXPECT_TRUE(resolver.lookups.empty());
}

// PAR-007
TEST(BracketReplacer, UnknownIdentifierStaysLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{unknown_x}", resolver), "{unknown_x}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "unknown");
}

// PAR-008
TEST(BracketReplacer, DeclinedValueKeepsTheOriginalToken)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };

    EXPECT_EQ(parse("{player_x}", resolver), "{player_x}");
    EXPECT_EQ(resolver.lookups.size(), 1U);
}

TEST(BracketReplacer, EmptyStringIsAValidValue)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::string();
    };

    // An empty replacement is a real value and removes the token entirely, which is
    // what distinguishes it from declining to resolve.
    EXPECT_EQ(parse("a{player_x}b", resolver), "ab");
}

// PAR-009
TEST(BracketReplacer, ReplacesSeveralPlaceholdersInOrderAndKeepsSurroundingText)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("A{player_x}B{player_y}C", resolver), "A<x>B<y>C");
    ASSERT_EQ(resolver.lookups.size(), 2U);
    EXPECT_EQ(resolver.lookups[0].params, "x");
    EXPECT_EQ(resolver.lookups[1].params, "y");
}

// PAR-010
TEST(BracketReplacer, AdjacentReplacementsConcatenate)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player_x}{player_y}", resolver), "<x><y>");
    EXPECT_EQ(resolver.lookups.size(), 2U);
}

// PAR-011
TEST(BracketReplacer, MalformedBracesStayLiteral)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player_x", resolver), "{player_x");
    EXPECT_EQ(parse("player_x}", resolver), "player_x}");
    EXPECT_EQ(parse("}{", resolver), "}{");
    EXPECT_EQ(parse("abc}", resolver), "abc}");
    EXPECT_EQ(parse("abc{player_x", resolver), "abc{player_x");
    EXPECT_EQ(parse("{", resolver), "{");
    EXPECT_EQ(parse("}", resolver), "}");
    EXPECT_EQ(parse("}}}", resolver), "}}}");
    EXPECT_EQ(parse("{{{", resolver), "{{{");
    EXPECT_TRUE(resolver.lookups.empty());
}

// PAR-012
TEST(BracketReplacer, LeadingClosingBraceDoesNotBlockALaterToken)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("}{player_x}", resolver), "}<x>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].params, "x");
}

// PAR-013
TEST(BracketReplacer, SpaceBeforeTheSeparatorInvalidatesTheToken)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{ player_x}", resolver), "{ player_x}");
    EXPECT_EQ(parse("{play er_x}", resolver), "{play er_x}");
    EXPECT_EQ(parse("{player _x}", resolver), "{player _x}");
    EXPECT_EQ(parse("{\tplayer_x}", resolver), "{\tplayer_x}");
    EXPECT_TRUE(resolver.lookups.empty());
}

// PAR-014
TEST(BracketReplacer, ParametersPreserveSpacesAndPunctuation)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("{player_hello world}", resolver), "<hello world>");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].params, "hello world");

    resolver.lookups.clear();
    EXPECT_EQ(parse("{player_a:b%c-d.e/f}", resolver), "<a:b%c-d.e/f>");
    EXPECT_EQ(resolver.lookups[0].params, "a:b%c-d.e/f");

    resolver.lookups.clear();
    (void)parse("{player_ }", resolver);
    EXPECT_EQ(resolver.lookups[0].params, " ");
}

// PAR-015: returned text is output, never input.
TEST(BracketReplacer, ReplacementValueIsNotReparsed)
{
    RecordingResolver resolver;
    resolver.handler = [](const std::string_view identifier, std::string_view) -> std::optional<std::string> {
        if (identifier == "player") {
            return "{other_x}";
        }
        return "SHOULD NOT HAPPEN";
    };

    EXPECT_EQ(parse("{player_x}", resolver), "{other_x}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
}

// PAR-016
TEST(BracketReplacer, BraceRichReplacementsSurviveVerbatim)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return "{{}}{a_b}}}{{";
    };

    EXPECT_EQ(parse("{player_x}", resolver), "{{}}{a_b}}}{{");
    EXPECT_EQ(resolver.lookups.size(), 1U);
}

// PAR-018
TEST(BracketReplacer, NestingIsNotRecognized)
{
    RecordingResolver resolver;

    // The candidate is "{player_x", whose identifier "{player" is invalid.
    EXPECT_EQ(parse("{{player_x}", resolver), "{{player_x}");
    EXPECT_TRUE(resolver.lookups.empty());

    // The first closing brace ends the candidate, so params carry the inner opening
    // brace and the trailing brace is ordinary text.
    resolver.lookups.clear();
    EXPECT_EQ(parse("{player_{other_x}}", resolver), "<{other_x>}");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "player");
    EXPECT_EQ(resolver.lookups[0].params, "{other_x");
}

// PAR-019
TEST(BracketReplacer, InputWithoutCandidatesIsReturnedUnchangedWithoutLookups)
{
    RecordingResolver resolver;
    EXPECT_EQ(parse("", resolver), "");
    EXPECT_EQ(parse("plain text", resolver), "plain text");
    EXPECT_EQ(parse("no braces at all _ % : ", resolver), "no braces at all _ % : ");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(BracketReplacer, BinaryAndUtf8BytesPassThroughUnchanged)
{
    RecordingResolver resolver;
    const std::string text = std::string("caf\xc3\xa9 \xff\xfe\0", 9) + std::string("\0mid", 4);
    EXPECT_EQ(parse(text, resolver), text);

    // Multi-byte parameters are forwarded byte for byte.
    resolver.lookups.clear();
    EXPECT_EQ(parse("{player_caf\xc3\xa9}", resolver), "<caf\xc3\xa9>");
    EXPECT_EQ(resolver.lookups[0].params, "caf\xc3\xa9");
}

// PAR-020
TEST(BracketReplacer, LongAdversarialInputCompletesInLinearTime)
{
    RecordingResolver resolver;

    // Mixes resolvable tokens, declined tokens, unmatched opens, and stray closes.
    std::string text;
    text.reserve(4'000'000);
    for (int i = 0; i < 100'000; ++i) {
        text += "{player_x}";
        text += "{unknown_y}";
        text += "{no_close";
        text += "}}";
        text += "{{";
    }

    const auto start = std::chrono::steady_clock::now();
    const auto output = replacePlaceholders(text, resolver);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(output.empty());
    // A quadratic scan of this input would take minutes; a linear one is milliseconds.
    EXPECT_LT(elapsed, std::chrono::seconds{10});

    // Spot-check that resolution still happened correctly at scale.
    EXPECT_NE(output.find("<x>"), std::string::npos);
    EXPECT_NE(output.find("{unknown_y}"), std::string::npos);
}

TEST(BracketReplacer, ManyOpeningBracesDoNotDegradeToQuadratic)
{
    RecordingResolver resolver;

    // Worst case for a naive rescan: a long run of opening braces before any close.
    const std::string text = std::string(500'000, '{') + "player_x}" + std::string(500'000, '{');

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

    // With every lookup declined the output must equal the input byte for byte, which
    // proves the scan neither drops nor duplicates any part of the text.
    const std::vector<std::string> inputs = {
        "{player_x}",     "a{player_x}b",    "{a}{b}{c}", "{{{a_b}}}", "}{}{", "{a_b}{c_d}{e_f}",
        "{no_close{a_b}", "text{a_b}text{c", "{}{}{}",    "{_}{_}",    "{a_b", "}}}{{{ ",
    };
    for (const auto &input : inputs) {
        EXPECT_EQ(replacePlaceholders(input, resolver), input) << "input='" << input << "'";
    }
}

// PAR-021
TEST(Contains, TrueWheneverAnOpeningBraceHasALaterClosingBrace)
{
    EXPECT_TRUE(containsPlaceholders("{}"));
    EXPECT_TRUE(containsPlaceholders("{player}"));
    EXPECT_TRUE(containsPlaceholders("{unknown_x}"));
    EXPECT_TRUE(containsPlaceholders("{player_name}"));
    EXPECT_TRUE(containsPlaceholders("a{}b"));
    EXPECT_TRUE(containsPlaceholders("{ }"));
    EXPECT_TRUE(containsPlaceholders("{{}}"));
    EXPECT_TRUE(containsPlaceholders("{no_close{a}"));
}

// PAR-022
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
    // Deliberately lexical: it answers "does this look like it has a placeholder",
    // not "will anything resolve".
    EXPECT_TRUE(containsPlaceholders("{_}"));
    EXPECT_TRUE(containsPlaceholders("{ player_x}"));
    EXPECT_TRUE(containsPlaceholders("{plugin:demo_x}"));
    EXPECT_TRUE(containsPlaceholders("{日本_x}"));
}

}  // namespace
