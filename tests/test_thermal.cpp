#include "metrics/thermal.h"

#include "test_harness.h"

TEST(parses_millidegrees) {
  CHECK_NEAR(*parse_thermal_temp("71000\n"), 71.0, 1e-9);  // the real reading
  CHECK_NEAR(*parse_thermal_temp("45500"), 45.5, 1e-9);
}

TEST(rejects_nonsense) {  // Review Focus 5
  CHECK(!parse_thermal_temp("").has_value());
  CHECK(!parse_thermal_temp("N/A\n").has_value());
  CHECK(!parse_thermal_temp("0\n").has_value());       // 0 degrees is not real
  CHECK(!parse_thermal_temp("-40000\n").has_value());
  CHECK(!parse_thermal_temp("500000\n").has_value());  // 500 C is not real
}

TEST(normalizes_to_the_specified_range) {
  CHECK_NEAR(normalize_temp(40.0), 0.0, 1e-9);
  CHECK_NEAR(normalize_temp(90.0), 1.0, 1e-9);
  CHECK_NEAR(normalize_temp(65.0), 0.5, 1e-9);
}

TEST(normalization_clamps_outside_the_range) {
  CHECK_EQ(normalize_temp(5.0), 0.0);
  CHECK_EQ(normalize_temp(150.0), 1.0);
}
