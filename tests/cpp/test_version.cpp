#include <gtest/gtest.h>

#include "endstone_papi/version.h"

// The public version header is self-contained (inline); verify it reports a
// non-empty build version without requiring a separate link target.
TEST(Version, IsNotEmpty)
{
    EXPECT_FALSE(papi::getVersion().empty());
}
