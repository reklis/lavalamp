#include "test_harness.h"

TEST(harness_runs) { CHECK_EQ(2 + 2, 4); }
TEST(harness_near) { CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9); }
