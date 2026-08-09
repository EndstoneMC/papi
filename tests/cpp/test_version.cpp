#include <gtest/gtest.h>

#include "endstone_papi/version.h"

// Baseline smoke test: the private core links and reports a build version.
TEST(Version, IsNotEmpty)
{
    EXPECT_FALSE(papi::getVersion().empty());
}
