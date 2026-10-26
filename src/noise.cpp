#include "noise.h"

#include <cmath>
#include <cstdint>

namespace {

// Finite-difference step for the curl. Deliberately a named constant: callers
// that differentiate the curl again (the divergence test does) cancel exactly
// when their step matches this one.
constexpr double kEpsilon = 1e-3;

std::uint32_t hash_lattice(std::int32_t x, std::int32_t y, std::int32_t z) {
  std::uint32_t h = 2166136261u;
  h = (h ^ static_cast<std::uint32_t>(x)) * 16777619u;
  h = (h ^ static_cast<std::uint32_t>(y)) * 16777619u;
  h = (h ^ static_cast<std::uint32_t>(z)) * 16777619u;
  // Avalanche, so neighbouring lattice points are uncorrelated.
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  h *= 3266489917u;
  h ^= h >> 16;
  return h;
}

double lattice_value(std::int32_t x, std::int32_t y, std::int32_t z) {
  return static_cast<double>(hash_lattice(x, y, z)) / 2147483647.5 - 1.0;
}

double fade(double t) { return t * t * (3.0 - 2.0 * t); }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

}  // namespace

double value_noise(double x, double y, double z) {
  const double fx = std::floor(x);
  const double fy = std::floor(y);
  const double fz = std::floor(z);

  const auto ix = static_cast<std::int32_t>(fx);
  const auto iy = static_cast<std::int32_t>(fy);
  const auto iz = static_cast<std::int32_t>(fz);

  // Smoothstep rather than linear: the lattice is only continuous under
  // linear interpolation, and visible grid creases would give the whole
  // field away as procedural.
  const double tx = fade(x - fx);
  const double ty = fade(y - fy);
  const double tz = fade(z - fz);

  const double c000 = lattice_value(ix, iy, iz);
  const double c100 = lattice_value(ix + 1, iy, iz);
  const double c010 = lattice_value(ix, iy + 1, iz);
  const double c110 = lattice_value(ix + 1, iy + 1, iz);
  const double c001 = lattice_value(ix, iy, iz + 1);
  const double c101 = lattice_value(ix + 1, iy, iz + 1);
  const double c011 = lattice_value(ix, iy + 1, iz + 1);
  const double c111 = lattice_value(ix + 1, iy + 1, iz + 1);

  const double x00 = lerp(c000, c100, tx);
  const double x10 = lerp(c010, c110, tx);
  const double x01 = lerp(c001, c101, tx);
  const double x11 = lerp(c011, c111, tx);

  const double y0 = lerp(x00, x10, ty);
  const double y1 = lerp(x01, x11, ty);

  return lerp(y0, y1, tz);
}

Vec2 curl_noise(double x, double y, double t) {
  // curl of a scalar potential in 2D: (dpsi/dy, -dpsi/dx).
  const double dpsi_dy =
      (value_noise(x, y + kEpsilon, t) - value_noise(x, y - kEpsilon, t)) / (2.0 * kEpsilon);
  const double dpsi_dx =
      (value_noise(x + kEpsilon, y, t) - value_noise(x - kEpsilon, y, t)) / (2.0 * kEpsilon);

  return Vec2{dpsi_dy, -dpsi_dx};
}
