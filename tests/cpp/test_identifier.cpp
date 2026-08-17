#include <clocale>

#include <gtest/gtest.h>

#include "core/parser/identifier.h"

namespace {

using papi::detail::canonicalizeIdentifier;
using papi::detail::isValidIdentifier;

TEST(Identifier, AcceptsAlphanumericAndHyphenatedIdentifiers)
{
    EXPECT_TRUE(isValidIdentifier("demo"));
    EXPECT_TRUE(isValidIdentifier("Demo"));
    EXPECT_TRUE(isValidIdentifier("DEMO"));
    EXPECT_TRUE(isValidIdentifier("d"));
    EXPECT_TRUE(isValidIdentifier("9"));
    EXPECT_TRUE(isValidIdentifier("demo1"));
    EXPECT_TRUE(isValidIdentifier("1demo"));
    EXPECT_TRUE(isValidIdentifier("my-expansion"));
    EXPECT_TRUE(isValidIdentifier("a--b"));
}

TEST(Identifier, RejectsEmptyAndBadFirstCharacter)
{
    EXPECT_FALSE(isValidIdentifier(""));
    EXPECT_FALSE(isValidIdentifier("."));
    EXPECT_FALSE(isValidIdentifier("_"));
    EXPECT_FALSE(isValidIdentifier("-"));
    EXPECT_FALSE(isValidIdentifier(".demo"));
    EXPECT_FALSE(isValidIdentifier("_demo"));
    EXPECT_FALSE(isValidIdentifier("-demo"));
}

TEST(Identifier, RejectsSyntaxSeparators)
{
    EXPECT_FALSE(isValidIdentifier("demo.test"));
    EXPECT_FALSE(isValidIdentifier("demo_"));
    EXPECT_FALSE(isValidIdentifier("de_mo"));
}

TEST(Identifier, RejectsDelimitersAndPunctuation)
{
    EXPECT_FALSE(isValidIdentifier("{demo"));
    EXPECT_FALSE(isValidIdentifier("demo}"));
    EXPECT_FALSE(isValidIdentifier("%demo%"));
    EXPECT_FALSE(isValidIdentifier("plugin:demo"));
}

TEST(Identifier, RejectsWhitespaceControlsAndNonAscii)
{
    EXPECT_FALSE(isValidIdentifier(" demo"));
    EXPECT_FALSE(isValidIdentifier("demo "));
    EXPECT_FALSE(isValidIdentifier("de mo"));
    EXPECT_FALSE(isValidIdentifier("de\tmo"));
    EXPECT_FALSE(isValidIdentifier("de\nmo"));
    EXPECT_FALSE(isValidIdentifier(std::string_view("de\0mo", 5)));
    EXPECT_FALSE(isValidIdentifier("démo"));
    EXPECT_FALSE(isValidIdentifier("日本語"));
    EXPECT_FALSE(isValidIdentifier("\xff\xfe"));
}

TEST(Identifier, CanonicalizesToAsciiLowercase)
{
    EXPECT_EQ(canonicalizeIdentifier("Demo"), "demo");
    EXPECT_EQ(canonicalizeIdentifier("DEMO"), "demo");
    EXPECT_EQ(canonicalizeIdentifier("My-Expansion-V2"), "my-expansion-v2");
}

TEST(Identifier, CanonicalizationIsIdempotent)
{
    const auto once = canonicalizeIdentifier("MiXeD-Case-9");
    EXPECT_EQ(canonicalizeIdentifier(once), once);
}

TEST(Identifier, CanonicalizationIgnoresProcessLocale)
{
    const char *previous = std::setlocale(LC_ALL, nullptr);
    const std::string saved = previous ? previous : "C";

    if (std::setlocale(LC_ALL, "tr_TR.UTF-8") == nullptr && std::setlocale(LC_ALL, "Turkish") == nullptr) {
        GTEST_SKIP() << "no Turkish locale available on this host";
    }

    EXPECT_EQ(canonicalizeIdentifier("INDEX"), "index");
    EXPECT_TRUE(isValidIdentifier("INDEX"));

    std::setlocale(LC_ALL, saved.c_str());
}

}  // namespace
