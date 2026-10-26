#include "palette.h"

#include <algorithm>
#include <array>

#include "color.h"

namespace {

// Cool: a machine at rest. Deep purple through magenta into red.
// Deliberately never reaches a bright top -- an idle lamp should look like it
// is smouldering, not burning.
//
// The darkest entry is a visible purple rather than near-black on purpose,
// and is deliberately above level ~47 per channel. Roughly 70% of cells sit
// in the lowest band, and anything darker than that quantises into xterm's
// GREY ramp (232-255) rather than its colour cube -- which made the screen
// read as an empty black terminal instead of a lamp full of dark fluid.
constexpr std::array<Rgb, 6> kCoolControl = {{
    Rgb{64, 16, 76},
    Rgb{86, 16, 96},
    Rgb{84, 14, 88},
    Rgb{142, 20, 92},
    Rgb{190, 34, 62},
    Rgb{226, 62, 40},
}};

// Hot: a machine under load. Dark maroon through orange and amber to a
// near-white core. Same reasoning as above for the darkest entry.
constexpr std::array<Rgb, 7> kHotControl = {{
    Rgb{80, 22, 26},
    Rgb{112, 24, 14},
    Rgb{140, 24, 8},
    Rgb{204, 70, 10},
    Rgb{240, 140, 22},
    Rgb{252, 218, 88},
    Rgb{255, 250, 232},
}};

// Samples a control-point array at u in [0, 1], interpolating perceptually
// between the two bracketing points.
template <std::size_t N>
Rgb sample_control(const std::array<Rgb, N>& control, double u) {
  const double scaled = std::clamp(u, 0.0, 1.0) * static_cast<double>(N - 1);
  const auto lower = static_cast<std::size_t>(scaled);
  if (lower >= N - 1) return control[N - 1];
  return lerp_rgb(control[lower], control[lower + 1], scaled - static_cast<double>(lower));
}

}  // namespace

Ramp build_ramp(double temperature01, std::size_t entries) {
  const double heat = std::clamp(temperature01, 0.0, 1.0);

  Ramp ramp;
  if (entries == 0) return ramp;
  ramp.indices.reserve(entries);

  // Monotonic luminance is a hard requirement, and nearest-colour
  // quantisation does not preserve it: the xterm cube is coarse enough in the
  // saturated reds and purples that two adjacent ramp samples can land on
  // indices whose luminances are inverted. So the chosen index is constrained
  // to never be perceptually darker than its predecessor -- when the nearest
  // match would step backwards, keep the previous index instead. Repeating an
  // index is harmless here; an inversion would show as a visible seam running
  // through every blob.
  double previous_luminance = -1.0;

  for (std::size_t i = 0; i < entries; ++i) {
    const double u = entries == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(entries - 1);

    const Rgb cool = sample_control(kCoolControl, u);
    const Rgb hot = sample_control(kHotControl, u);
    const Rgb blended = lerp_rgb(cool, hot, heat);

    std::uint8_t index = quantize_to_xterm256(blended);
    double luminance = rgb_to_oklab(xterm256_to_rgb(index)).L;

    if (!ramp.indices.empty() && luminance < previous_luminance) {
      index = ramp.indices.back();
      luminance = previous_luminance;
    }

    ramp.indices.push_back(index);
    previous_luminance = luminance;
  }

  return ramp;
}
