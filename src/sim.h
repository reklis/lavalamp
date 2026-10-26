#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "field.h"
#include "metrics/heat.h"
#include "vec2.h"

// One glob of wax. Each carries its own temperature, which is what makes the
// convection cycle emergent rather than scripted: nothing tells a blob to
// turn around at the top, it just cools until gravity wins.
struct Blob {
  Vec2 pos;
  Vec2 vel;
  double radius = 1.0;
  double temp = 0.0;
};

// A blob system with thermal buoyancy, in lamp coordinates where y grows
// downward (so "rising" is negative y, matching the field and the terminal).
//
// Deliberately not a grid fluid solver. Real lava lamps are about discrete
// immiscible globs that merge and split, and a metaball field reproduces that
// iconography directly, whereas a grid solver drifts toward smoke-like
// diffusion -- which is the spec's "gradient mush" anti-goal.
class LavaSim {
 public:
  LavaSim(double width, double height, std::uint32_t seed);

  void resize(double width, double height);

  // cpu -> convection rate and target blob count; gpu -> turbulence and
  // tearing; net -> droplet impulses from the floor. `temp` is deliberately
  // unused here: it drives the palette, not the motion. An unavailable
  // channel contributes its neutral value, so the lamp stays alive with any
  // subset of the metrics present.
  void set_heat(const Heat& heat);

  void step(double dt);

  // Metaball sum per field pixel, normalized into [0, 1]. Uses per-blob
  // bounding boxes so cost scales with coverage rather than with
  // pixels x blobs -- the spec makes "must not burn a core" a correctness
  // requirement, since the lamp's own load is one of its inputs.
  void sample_into(Field& field) const;

  const std::vector<Blob>& blobs() const { return blobs_; }

 private:
  void integrate(double dt);
  void resolve_merges();
  void resolve_splits();
  void emit_impulses(double dt);
  void enforce_bounds();
  double base_radius() const;

  double width_;
  double height_;
  double time_ = 0.0;
  double impulse_credit_ = 0.0;

  Heat heat_{};
  std::mt19937 rng_;
  std::vector<Blob> blobs_;
};
