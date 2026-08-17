#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/parser/bracket_replacer.h"

namespace {

using papi::detail::PlaceholderResolver;
using papi::detail::replacePlaceholders;

class RecordingResolver final : public PlaceholderResolver {
public:
    struct Lookup {
        std::string identifier;
        std::string params;
    };

    [[nodiscard]] std::optional<std::string> resolve(const std::string_view identifier,
                                                     const std::string_view params) override
    {
        lookups.push_back({std::string(identifier), std::string(params)});
        if (handler) {
            return handler(identifier, params);
        }
        return std::string(params);
    }

    std::function<std::optional<std::string>(std::string_view, std::string_view)> handler;
    std::vector<Lookup> lookups;
};

TEST(DotSyntax, SplitsAtFirstDot)
{
    RecordingResolver resolver;

    EXPECT_EQ(replacePlaceholders("{spark.cpu_process_1m}", resolver), "cpu_process_1m");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "spark");
    EXPECT_EQ(resolver.lookups[0].params, "cpu_process_1m");
}

TEST(DotSyntax, PreservesKeyExactly)
{
    RecordingResolver resolver;

    EXPECT_EQ(replacePlaceholders("{SPARK.Cpu_Process_1M}", resolver), "Cpu_Process_1M");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_EQ(resolver.lookups[0].identifier, "spark");
    EXPECT_EQ(resolver.lookups[0].params, "Cpu_Process_1M");
}

TEST(DotSyntax, EmptyKeyIsValid)
{
    RecordingResolver resolver;

    EXPECT_EQ(replacePlaceholders("{spark.}", resolver), "");
    ASSERT_EQ(resolver.lookups.size(), 1U);
    EXPECT_TRUE(resolver.lookups[0].params.empty());
}

TEST(DotSyntax, MissingDotStaysLiteral)
{
    RecordingResolver resolver;

    EXPECT_EQ(replacePlaceholders("{spark_cpu_process_1m}", resolver), "{spark_cpu_process_1m}");
    EXPECT_TRUE(resolver.lookups.empty());
}

TEST(DotSyntax, DotInKeyIsPreservedAndUnderscoreIdentifierIsRejected)
{
    RecordingResolver resolver;

    EXPECT_EQ(replacePlaceholders("{spark.cpu.value}", resolver), "cpu.value");
    EXPECT_EQ(replacePlaceholders("{spark_cpu.value}", resolver), "{spark_cpu.value}");
    EXPECT_EQ(replacePlaceholders("{spark.cpu_process_1m}", resolver), "cpu_process_1m");
}

TEST(DotSyntax, DeclinedValuePreservesOriginalToken)
{
    RecordingResolver resolver;
    resolver.handler = [](std::string_view, std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };

    EXPECT_EQ(replacePlaceholders("{spark.cpu_process_1m}", resolver), "{spark.cpu_process_1m}");
}

}  // namespace
