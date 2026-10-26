#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A ramp is the lamp's whole colour vocabulary: `entries` xterm-256 indices
// running from the dark surrounding fluid up to the brightest blob core.
// Luminance increases along it, which is what makes blobs read as emissive
// rather than as flat patches.
struct Ramp {
  std::vector<std::uint8_t> indices;

  std::size_t size() const { return indices.size(); }
  std::uint8_t operator[](std::size_t i) const { return indices[i]; }
};

constexpr std::size_t kDefaultRampEntries = 32;

// `temperature01` blends between the cool palette (deep purples and magentas,
// an idle machine) and the hot one (orange through to white-hot). Values
// outside [0, 1] are clamped. Quantisation makes this comparatively
// expensive, so callers cache the result and only rebuild when the
// temperature has moved materially.
Ramp build_ramp(double temperature01, std::size_t entries);
