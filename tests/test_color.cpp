#include "color.h"

#include "test_harness.h"

TEST(xterm_cube_corners) {
  CHECK_EQ(xterm256_to_rgb(16), Rgb{0, 0, 0});         // cube origin
  CHECK_EQ(xterm256_to_rgb(231), Rgb{255, 255, 255});  // cube max
}

TEST(xterm_grayscale_ramp) {
  CHECK_EQ(xterm256_to_rgb(232), Rgb{8, 8, 8});
  CHECK_EQ(xterm256_to_rgb(255), Rgb{238, 238, 238});
}

TEST(terminal_color_codes_preserve_rgb_and_indexed_colors) {
  for (const Rgb color : {Rgb{0, 0, 0}, Rgb{0, 0, 16}, Rgb{64, 16, 76}, Rgb{255, 255, 255}}) {
    const ColorCode code = rgb_color_code(color);
    CHECK(code > 255);
    CHECK_EQ(color_code_to_rgb(code), color);
  }
  for (ColorCode code = 16; code <= 255; ++code)
    CHECK_EQ(color_code_to_rgb(code), xterm256_to_rgb(static_cast<std::uint8_t>(code)));
}

TEST(quantize_is_inverse_on_palette_colors) {
  // every index in the 6x6x6 cube and gray ramp must round-trip
  for (int i = 16; i <= 255; ++i)
    CHECK_EQ(quantize_to_xterm256(xterm256_to_rgb(static_cast<std::uint8_t>(i))), i);
}

TEST(quantize_never_returns_system_colors) {
  // indices 0-15 are terminal-theme dependent and must never be chosen
  CHECK(quantize_to_xterm256(Rgb{0, 0, 0}) >= 16);
  CHECK(quantize_to_xterm256(Rgb{255, 0, 0}) >= 16);
}

TEST(lerp_endpoints_exact) {
  CHECK_EQ(lerp_rgb(Rgb{10, 20, 30}, Rgb{200, 100, 50}, 0.0), Rgb{10, 20, 30});
  CHECK_EQ(lerp_rgb(Rgb{10, 20, 30}, Rgb{200, 100, 50}, 1.0), Rgb{200, 100, 50});
}

TEST(lerp_midpoint_is_perceptually_halfway) {
  // The property that matters: halfway along the ramp must look halfway
  // bright. Assert it where it is actually defined -- in Oklab -- rather
  // than on the sRGB byte.
  Rgb m = lerp_rgb(Rgb{0, 0, 0}, Rgb{255, 255, 255}, 0.5);
  CHECK_NEAR(rgb_to_oklab(m).L, 0.5, 0.01);
}

TEST(lerp_midpoint_matches_neither_naive_interpolation) {
  // Guards against silently regressing to a cheaper lerp. The three
  // candidates are distinguishable: Oklab gives 99, an sRGB byte lerp gives
  // 127, and a linear-light lerp gives 187.
  Rgb m = lerp_rgb(Rgb{0, 0, 0}, Rgb{255, 255, 255}, 0.5);
  CHECK(m.r > 80 && m.r < 115);
}
