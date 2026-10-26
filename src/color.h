#pragma once

#include <cstdint>

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

inline bool operator==(const Rgb& a, const Rgb& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}
inline bool operator!=(const Rgb& a, const Rgb& b) { return !(a == b); }

struct Oklab {
  double L = 0.0;
  double a = 0.0;
  double b = 0.0;
};

Oklab rgb_to_oklab(Rgb c);
Rgb oklab_to_rgb(Oklab c);

// Squared Oklab distance. Squared is enough for nearest-colour search and
// avoids 240 square roots per lookup.
double oklab_distance_sq(const Oklab& a, const Oklab& b);

// The 6x6x6 colour cube (16-231) and the 24-step grey ramp (232-255).
// Indices 0-15 are not addressable here: they are remapped by terminal
// themes, so the renderer never uses them.
Rgb xterm256_to_rgb(std::uint8_t index);

// Nearest xterm-256 index by perceptual distance. Always returns >= 16.
std::uint8_t quantize_to_xterm256(Rgb c);

// Interpolates perceptually (in Oklab) and returns sRGB. Interpolating in
// sRGB instead darkens and desaturates through the middle, which would show
// up as a muddy band across every blob.
Rgb lerp_rgb(Rgb a, Rgb b, double t);
