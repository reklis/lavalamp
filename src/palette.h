#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "color.h"

// A ramp is the lamp's whole colour vocabulary: `entries` terminal colours
// running from the dark surrounding fluid up to the brightest blob core.
// Luminance increases along it, which is what makes blobs read as emissive
// rather than as flat patches.
struct Ramp {
  std::vector<ColorCode> indices;

  std::size_t size() const { return indices.size(); }
  ColorCode operator[](std::size_t i) const { return indices[i]; }
};

// 18, not 32: wider bands mean adjacent vertical samples land in the SAME
// band more often, which is what makes the dither branch in select_cell fire.
// At 32 the gradient crossed a band almost every pixel and the lamp rendered
// as nothing but half-blocks, with no grain at all.
constexpr std::size_t kDefaultRampEntries = 18;

// `temperature01` blends between the cool palette (deep purples and magentas,
// an idle machine) and the hot one (orange through to white-hot). Values
// outside [0, 1] are clamped. Quantisation makes this comparatively
// expensive, so callers cache the result and only rebuild when the
// temperature has moved materially.
Ramp build_ramp(double temperature01, std::size_t entries);

// Fade the displayed palette, not just its temperature input: nearest-index
// quantisation can jump even for tiny temperature changes. Endpoints retain
// the authored 256-colour bands; intermediate tones use truecolor SGR.
class PaletteFade {
 public:
  const Ramp& update(const Ramp& target, double dt);

 private:
  Ramp target_;
  Ramp displayed_;
  std::vector<Oklab> from_;
  std::vector<Oklab> to_;
  double elapsed_ = 0.0;
};
