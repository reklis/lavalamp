#include "color.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

double srgb_to_linear(std::uint8_t channel) {
  const double c = channel / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::uint8_t linear_to_srgb(double c) {
  const double s = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
  const double scaled = std::round(std::clamp(s, 0.0, 1.0) * 255.0);
  return static_cast<std::uint8_t>(scaled);
}

}  // namespace

Oklab rgb_to_oklab(Rgb c) {
  const double r = srgb_to_linear(c.r);
  const double g = srgb_to_linear(c.g);
  const double b = srgb_to_linear(c.b);

  const double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
  const double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
  const double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

  const double l_ = std::cbrt(l);
  const double m_ = std::cbrt(m);
  const double s_ = std::cbrt(s);

  return Oklab{
      0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
      1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
      0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_,
  };
}

Rgb oklab_to_rgb(Oklab c) {
  const double l_ = c.L + 0.3963377774 * c.a + 0.2158037573 * c.b;
  const double m_ = c.L - 0.1055613458 * c.a - 0.0638541728 * c.b;
  const double s_ = c.L - 0.0894841775 * c.a - 1.2914855480 * c.b;

  const double l = l_ * l_ * l_;
  const double m = m_ * m_ * m_;
  const double s = s_ * s_ * s_;

  return Rgb{
      linear_to_srgb(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
      linear_to_srgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
      linear_to_srgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
  };
}

double oklab_distance_sq(const Oklab& a, const Oklab& b) {
  const double dL = a.L - b.L;
  const double da = a.a - b.a;
  const double db = a.b - b.b;
  return dL * dL + da * da + db * db;
}

Rgb xterm256_to_rgb(std::uint8_t index) {
  if (index >= 232) {
    const int level = 8 + 10 * (index - 232);
    const auto v = static_cast<std::uint8_t>(level);
    return Rgb{v, v, v};
  }
  if (index >= 16) {
    const int offset = index - 16;
    return Rgb{
        static_cast<std::uint8_t>(kCubeLevels[offset / 36]),
        static_cast<std::uint8_t>(kCubeLevels[(offset / 6) % 6]),
        static_cast<std::uint8_t>(kCubeLevels[offset % 6]),
    };
  }
  // 0-15 are theme-dependent and deliberately unsupported; report black so
  // callers that stray here get something defined rather than garbage.
  return Rgb{0, 0, 0};
}

Rgb color_code_to_rgb(ColorCode code) {
  if ((code & kRgbColorFlag) == 0)
    return xterm256_to_rgb(static_cast<std::uint8_t>(code));
  return Rgb{static_cast<std::uint8_t>(code >> 16),
             static_cast<std::uint8_t>(code >> 8),
             static_cast<std::uint8_t>(code)};
}

std::uint8_t quantize_to_xterm256(Rgb c) {
  const Oklab target = rgb_to_oklab(c);

  std::uint8_t best_index = 16;
  double best_distance = oklab_distance_sq(target, rgb_to_oklab(xterm256_to_rgb(16)));

  for (int i = 17; i <= 255; ++i) {
    const auto index = static_cast<std::uint8_t>(i);
    const double distance = oklab_distance_sq(target, rgb_to_oklab(xterm256_to_rgb(index)));
    if (distance < best_distance) {
      best_distance = distance;
      best_index = index;
    }
  }

  return best_index;
}

Rgb lerp_rgb(Rgb a, Rgb b, double t) {
  // Endpoints are returned verbatim. Round-tripping sRGB -> Oklab -> sRGB is
  // accurate but not bit-exact, and a palette's first and last entries should
  // be exactly the colours that were authored.
  if (t <= 0.0) return a;
  if (t >= 1.0) return b;

  const Oklab x = rgb_to_oklab(a);
  const Oklab y = rgb_to_oklab(b);
  return oklab_to_rgb(Oklab{
      x.L + (y.L - x.L) * t,
      x.a + (y.a - x.a) * t,
      x.b + (y.b - x.b) * t,
  });
}
