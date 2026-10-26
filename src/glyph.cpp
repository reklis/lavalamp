#include "glyph.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct Banded {
  std::size_t band;
  double residual;
};

Banded band_of(double value, std::size_t entries) {
  const double scaled = std::clamp(value, 0.0, 1.0) * static_cast<double>(entries - 1);
  const auto band = std::min(static_cast<std::size_t>(scaled), entries - 1);
  return Banded{band, scaled - static_cast<double>(band)};
}

const char* shade_for(double residual) {
  if (residual < 0.2) return kShades[0];
  if (residual < 0.4) return kShades[1];
  if (residual < 0.6) return kShades[2];
  if (residual < 0.8) return kShades[3];
  return kShades[4];
}

}  // namespace

bool operator==(const Cell& a, const Cell& b) {
  // Pointer equality is the common case -- every glyph originates from the
  // constants above -- but fall back to a content compare so correctness does
  // not depend on which translation unit produced the Cell.
  const bool same_glyph = a.glyph == b.glyph || std::strcmp(a.glyph, b.glyph) == 0;
  return same_glyph && a.fg == b.fg && a.bg == b.bg;
}

Cell select_cell(double top, double bottom, const Ramp& ramp) {
  if (ramp.size() == 0) return Cell{};
  if (ramp.size() == 1) return Cell{kShades[0], ramp[0], ramp[0]};

  const Banded hi = band_of(top, ramp.size());
  const Banded lo = band_of(bottom, ramp.size());

  if (hi.band != lo.band) {
    // An edge runs through this cell: draw it sharply.
    return Cell{kUpperHalf, ramp[hi.band], ramp[lo.band]};
  }

  // Interior: dither between this band and the next.
  const double residual = (hi.residual + lo.residual) * 0.5;
  const std::size_t next = std::min(hi.band + 1, ramp.size() - 1);
  return Cell{shade_for(residual), ramp[next], ramp[hi.band]};
}
