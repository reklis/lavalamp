#pragma once

#include <cstdint>

#include "palette.h"

// CP437's shading glyphs, in increasing density. These five plus the upper
// half block are the project's entire character vocabulary -- the BBS art
// idiom the spec asks for is built out of nothing else.
inline constexpr const char* kShades[5] = {" ", "░", "▒", "▓", "█"};
inline constexpr const char* kUpperHalf = "▀";

struct Cell {
  const char* glyph = kShades[0];
  std::uint8_t fg = 16;
  std::uint8_t bg = 16;
};

bool operator==(const Cell& a, const Cell& b);
inline bool operator!=(const Cell& a, const Cell& b) { return !(a == b); }

// Converts one cell's two vertically stacked field samples into a glyph and
// a foreground/background pair.
//
// This is where the look is won or lost. Two cases:
//
//   * The samples fall in different palette bands, so there is a silhouette
//     edge running through this cell: emit the upper half block, which
//     renders the boundary at double the terminal's vertical resolution.
//
//   * The samples fall in the same band, so the cell is blob interior with
//     no edge to draw: spend it on texture instead, ordered-dithering
//     between that band and the next with a shading glyph. This both
//     extends the effective colour depth well past the palette size and
//     supplies the chunky grain that reads as hand-made ANSI art.
//
// Values outside [0, 1] are clamped. Both returned indices are always >= 16,
// so every cell paints a real colour -- the terminal is filled with fluid,
// never left showing its own default background.
Cell select_cell(double top, double bottom, const Ramp& ramp);
