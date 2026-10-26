#include "palette.h"

#include <algorithm>
#include <cmath>

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

TEST(palette_fade_starts_at_the_current_palette_not_black) {
  const Ramp target = build_ramp(0.35, 18);
  PaletteFade fade;
  const Ramp& shown = fade.update(target, 0.0);
  CHECK_EQ(shown.size(), target.size());
  for (std::size_t i = 0; i < target.size(); ++i) {
    CHECK((shown[i] & kRgbColorFlag) != 0);
    CHECK_EQ(color_code_to_rgb(shown[i]), color_code_to_rgb(target[i]));
  }
}

TEST(palette_fade_changes_background_in_small_steps_and_settles) {
  const Ramp cold{{17}}, hot{{196}};
  PaletteFade fade;
  fade.update(cold, 0.0);
  Rgb previous = color_code_to_rgb(fade.update(hot, 0.0)[0]);
  CHECK_EQ(previous, color_code_to_rgb(cold[0]));
  int changed_frames = 0;
  int biggest_step = 0;
  for (int frame = 0; frame < 120; ++frame) {
    const Rgb current = color_code_to_rgb(fade.update(hot, 1.0 / 60)[0]);
    changed_frames += current != previous;
    for (int step : {std::abs(int(current.r) - previous.r),
                     std::abs(int(current.g) - previous.g),
                     std::abs(int(current.b) - previous.b)})
      biggest_step = std::max(biggest_step, step);
    previous = current;
  }
  CHECK(changed_frames > 30);
  CHECK(biggest_step < 12);
  CHECK_EQ(previous, color_code_to_rgb(hot[0]));
}

TEST(palette_fade_retargets_without_a_jump) {
  const Ramp cold = build_ramp(0.0, 18), hot = build_ramp(1.0, 18);
  PaletteFade fade;
  fade.update(cold, 0.0);
  for (int frame = 0; frame < 30; ++frame) fade.update(hot, 1.0 / 60);
  const Ramp midway = fade.update(hot, 0.0);
  CHECK_EQ(fade.update(cold, 0.0).indices, midway.indices);
  for (int frame = 0; frame < 120; ++frame) fade.update(cold, 1.0 / 60);
  const Ramp settled = fade.update(cold, 0.0);
  for (std::size_t i = 0; i < cold.size(); ++i)
    CHECK_EQ(color_code_to_rgb(settled[i]), color_code_to_rgb(cold[i]));
}

TEST(palette_fade_is_time_based_not_frame_based) {
  const Ramp cold{{17}}, hot{{196}};
  Rgb reference;
  for (int fps : {30, 60, 120}) {
    PaletteFade fade;
    fade.update(cold, 0.0);
    for (int frame = 0; frame < fps; ++frame) fade.update(hot, 1.0 / fps);
    const Rgb color = color_code_to_rgb(fade.update(hot, 0.0)[0]);
    if (fps == 30) reference = color;
    CHECK_EQ(color, reference);
  }
}

TEST(palette_fade_handles_empty_and_resized_ramps) {
  PaletteFade fade;
  CHECK_EQ(fade.update(Ramp{}, 0.0).size(), 0u);
  fade.update(build_ramp(0.0, 18), 0.0);
  CHECK_EQ(fade.update(Ramp{}, 0.0).size(), 0u);
  CHECK_EQ(fade.update(build_ramp(1.0, 32), 0.0).size(), 32u);
}

TEST(temperature_is_clamped) {
  CHECK_EQ(build_ramp(-5.0, 32).indices, build_ramp(0.0, 32).indices);
  CHECK_EQ(build_ramp(99.0, 32).indices, build_ramp(1.0, 32).indices);
}
