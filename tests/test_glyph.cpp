#include "glyph.h"

#include <algorithm>
#include <string>

#include "palette.h"
#include "test_harness.h"

TEST(differing_bands_use_half_block) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.0, 1.0, r);
  CHECK_EQ(std::string(c.glyph), "▀");
  CHECK_EQ(c.fg, r[0]);              // fg is the TOP sample
  CHECK_EQ(c.bg, r[r.size() - 1]);   // bg is the BOTTOM sample
}

TEST(same_band_dithers_between_adjacent_entries) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.5, 0.5, r);
  std::string g(c.glyph);
  CHECK(g != "▀");
  // dither pair must be two ADJACENT ramp entries, never a wider jump
  std::size_t band = 0;
  while (band + 1 < r.size() && r[band] != c.bg) ++band;
  CHECK_EQ(c.fg, r[std::min(band + 1, r.size() - 1)]);
}

TEST(residual_selects_shade_density) {
  Ramp r = build_ramp(0.5, 32);
  // within one band, rising residual must pick denser glyphs in order
  double step = 1.0 / (r.size() - 1);
  std::string low = select_cell(step * 4 + step * 0.05, step * 4 + step * 0.05, r).glyph;
  std::string mid = select_cell(step * 4 + step * 0.50, step * 4 + step * 0.50, r).glyph;
  std::string high = select_cell(step * 4 + step * 0.95, step * 4 + step * 0.95, r).glyph;
  CHECK_EQ(low, " ");
  CHECK_EQ(mid, "▒");
  CHECK_EQ(high, "█");
}

TEST(field_values_are_clamped) {
  Ramp r = build_ramp(0.5, 32);
  CHECK_EQ(select_cell(-3.0, -3.0, r), select_cell(0.0, 0.0, r));
  CHECK_EQ(select_cell(7.0, 7.0, r), select_cell(1.0, 1.0, r));
}

TEST(top_band_does_not_overflow_ramp) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(1.0, 1.0, r);  // residual lands in the last band
  CHECK(c.fg == r[r.size() - 1] && c.bg == r[r.size() - 1]);
}

TEST(empty_space_is_dark_fluid_not_terminal_black) {
  // spec: the terminal is FILLED with goo. Even zero field must paint a
  // real palette color, never fall through to the terminal default.
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.0, 0.0, r);
  CHECK(c.bg >= 16);
  CHECK(c.fg >= 16);
}
