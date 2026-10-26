#include "palette.h"

#include "color.h"
#include "test_harness.h"

TEST(ramp_has_requested_length) { CHECK_EQ(build_ramp(0.0, 32).size(), 32u); }

TEST(ramp_entries_are_in_safe_range) {
  for (double t : {0.0, 0.5, 1.0})
    for (std::uint8_t i : build_ramp(t, 32).indices) CHECK(i >= 16);
}

TEST(ramp_brightness_increases_monotonically) {
  // luminance must rise from dark fluid to bright blob core, or blobs
  // will not read as emissive
  Ramp r = build_ramp(0.5, 32);
  double prev = -1.0;
  for (std::uint8_t i : r.indices) {
    double L = rgb_to_oklab(xterm256_to_rgb(i)).L;
    CHECK(L >= prev - 0.02);  // allow quantization jitter, forbid reversal
    prev = L;
  }
}

TEST(hot_ramp_top_is_brighter_than_cool_ramp_top) {
  double cool = rgb_to_oklab(xterm256_to_rgb(build_ramp(0.0, 32).indices.back())).L;
  double hot = rgb_to_oklab(xterm256_to_rgb(build_ramp(1.0, 32).indices.back())).L;
  CHECK(hot > cool);
}

TEST(temperature_is_clamped) {
  CHECK_EQ(build_ramp(-5.0, 32).indices, build_ramp(0.0, 32).indices);
  CHECK_EQ(build_ramp(99.0, 32).indices, build_ramp(1.0, 32).indices);
}
