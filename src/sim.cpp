#include "sim.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "noise.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// --- Blob population -------------------------------------------------------
// Idle keeps few, fat, lazy globs; load breaks them into more, busier ones.
constexpr std::size_t kMinBlobs = 7;
constexpr std::size_t kMaxBlobs = 18;
// Headroom above kMaxBlobs for network droplets. This caps object count,
// not wax volume: jets must also conserve area when merges free up slots.
constexpr std::size_t kHardMaxBlobs = 44;

// Fraction of the lamp filled with wax. Merges, splits and network jets all
// conserve total area, so even sustained traffic cannot flood the lamp.
constexpr double kFillFraction = 0.22;

// --- Forces ----------------------------------------------------------------
constexpr double kGravity = 8.0;
constexpr double kBuoyancyBase = 18.0;   // at idle: hot wax still rises
constexpr double kBuoyancyGain = 30.0;   // scaled by cpu
constexpr double kDrag = 1.4;

constexpr double kHeatRate = 0.9;        // per second, at the floor
constexpr double kCoolRate = 0.7;        // per second, at the ceiling
// cpu drives the whole convection RATE, not just the force. Scaling only
// buoyancy made a loaded lamp pin every blob against the ceiling: the force
// overwhelmed gravity faster than the blobs could shed heat, so the cycle
// jammed instead of speeding up.
constexpr double kConvectionRateGain = 1.5;
constexpr double kAmbientCoolRate = 0.1;
constexpr double kHeatZoneFraction = 0.25;

// --- Turbulence ------------------------------------------------------------
constexpr double kNoiseScale = 0.06;     // feature size ~16 lamp units
constexpr double kNoiseTimeScale = 0.12;
constexpr double kTurbulenceBase = 2.0;
constexpr double kTurbulenceGain = 28.0;  // scaled by gpu

// --- Merge and split -------------------------------------------------------
constexpr double kMergeFactor = 0.42;    // of the summed radii
constexpr double kMaxRadiusFactor = 1.6; // of base radius, before forced split
constexpr double kMinSplitRadiusFactor = 0.65;
constexpr double kShearSplitThreshold = 14.0;
// Shape changes need their own interpolation: smoothing activity or velocity
// cannot hide the instantaneous replacement of one metaball kernel by two.
constexpr double kShapeTransitionSeconds = 0.35;
// A split separates its children over time instead of placing them apart.
// Speed is in radii per second so the motion looks the same at any lamp size.
// 3.0 was measured by tracking real split pairs, not guessed. The pair
// separates monotonically -- 0.04, 0.42, 0.76, 1.07, 1.36 radii -- crossing
// the 0.84 merge threshold around frame 9, i.e. ~150 ms, which is slow
// enough for the eye to read it as motion rather than a cut.
//
// Both neighbours are worse. At 2.0 the pair hangs on the merge threshold and
// is re-merged within 1-8 frames, so the lamp churns through split/merge
// cycles (244 events per 6000 frames versus a 218 baseline). At 5.0 the kick
// is twice a blob's normal speed and flings the children 7 radii apart in 4
// frames, which is the same jump the fix was meant to remove.
constexpr double kSplitSeparationSpeed = 3.0;
// Long enough for the children to finish their shape morph and clear the
// merge threshold before they can be joined again.
constexpr double kSplitCooldown = 0.45;

// --- Network impulses ------------------------------------------------------
constexpr double kImpulseRatePerSecond = 6.0;
constexpr double kImpulseSpeed = 26.0;
constexpr double kImpulseRadiusFactor = 0.35;

// --- Field sampling --------------------------------------------------------
constexpr double kInfluenceFactor = 1.7;  // influence radius / blob radius
constexpr double kFieldGain = 0.85;        // a lone blob core, before overlap

constexpr double kMaxTimestep = 1.0 / 15.0;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

double channel(double value, bool available, double neutral) {
  return available ? clamp01(value) : neutral;
}

void morph_from(Blob& blob, const ShapeLobe& lobe) {
  blob.shape = ShapeTransition{};
  blob.shape.lobes[0] = lobe;
  blob.shape.count = 1;
}

}  // namespace

std::uint32_t make_seed() {
  // Clock plus a counter: the counter guarantees successive calls differ even
  // if the clock has not ticked between them.
  static std::atomic<std::uint32_t> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto ticks = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());

  std::uint32_t mixed = static_cast<std::uint32_t>(ticks) ^
                        static_cast<std::uint32_t>(ticks >> 32) ^
                        (counter.fetch_add(1) * 2654435761u);
  mixed ^= mixed >> 16;
  mixed *= 2246822519u;
  mixed ^= mixed >> 13;
  return mixed == 0 ? 1u : mixed;
}

LavaSim::LavaSim(double width, double height, std::uint32_t seed)
    : width_(std::max(width, 1.0)), height_(std::max(height, 1.0)), rng_(seed) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  const double radius = base_radius();
  blobs_.reserve(kMinBlobs);

  // Seeded mid-cycle: randomized positions and temperatures, so frame one
  // already looks like a working lamp. Starting from a cold uniform state and
  // waiting for convection to develop makes launch look broken.
  for (std::size_t i = 0; i < kMinBlobs; ++i) {
    Blob blob;
    blob.pos = Vec2{unit(rng_) * width_, unit(rng_) * height_};
    blob.vel = Vec2{(unit(rng_) - 0.5) * 2.0, (unit(rng_) - 0.5) * 2.0};
    blob.radius = radius * (0.8 + 0.4 * unit(rng_));
    blob.temp = unit(rng_);
    blobs_.push_back(blob);
  }
}

double LavaSim::base_radius() const {
  const double total_area = width_ * height_ * kFillFraction;
  return std::sqrt(total_area / (kPi * static_cast<double>(kMinBlobs)));
}

void LavaSim::resize(double width, double height) {
  const double new_width = std::max(width, 1.0);
  const double new_height = std::max(height, 1.0);

  const double scale_x = new_width / width_;
  const double scale_y = new_height / height_;
  // Radii scale by the geometric mean so blobs stay round rather than being
  // stretched by a non-uniform resize.
  const double scale_r = std::sqrt(scale_x * scale_y);

  for (Blob& blob : blobs_) {
    blob.pos.x *= scale_x;
    blob.pos.y *= scale_y;
    blob.radius *= scale_r;
    for (std::size_t i = 0; i < blob.shape.count; ++i) {
      ShapeLobe& lobe = blob.shape.lobes[i];
      lobe.offset.x *= scale_x;
      lobe.offset.y *= scale_y;
      lobe.radius *= scale_r;
    }
  }

  width_ = new_width;
  height_ = new_height;
  enforce_bounds();
}

void LavaSim::set_heat(const Heat& heat) { heat_ = heat; }

void LavaSim::step(double dt) {
  // Clamped so a scheduling hiccup -- or a laptop resuming from sleep --
  // cannot explode the integrator.
  const double clamped = std::clamp(dt, 0.0, kMaxTimestep);
  if (clamped <= 0.0) return;

  time_ += clamped;

  for (Blob& blob : blobs_) {
    blob.merge_cooldown = std::max(blob.merge_cooldown - clamped, 0.0);
    if (blob.shape.count != 0) {
      blob.shape.elapsed += clamped;
      if (blob.shape.elapsed >= kShapeTransitionSeconds) blob.shape = ShapeTransition{};
    }
  }

  integrate(clamped);
  emit_impulses(clamped);
  resolve_merges();
  resolve_splits();
  enforce_bounds();
}

void LavaSim::integrate(double dt) {
  const double cpu = channel(heat_.cpu, heat_.cpu_ok, 0.0);
  const double gpu = channel(heat_.gpu, heat_.gpu_ok, 0.0);

  const double buoyancy = kBuoyancyBase + kBuoyancyGain * cpu;
  const double turbulence = kTurbulenceBase + kTurbulenceGain * gpu;
  const double convection = 1.0 + kConvectionRateGain * cpu;

  const double heat_zone = height_ * (1.0 - kHeatZoneFraction);
  const double cool_zone = height_ * kHeatZoneFraction;

  for (Blob& blob : blobs_) {
    // Heat at the floor, shed at the ceiling, lose a little everywhere.
    if (blob.pos.y > heat_zone) {
      blob.temp += kHeatRate * convection * dt * (1.0 - blob.temp);
    } else if (blob.pos.y < cool_zone) {
      blob.temp -= kCoolRate * convection * dt * blob.temp;
    }
    blob.temp -= kAmbientCoolRate * dt * blob.temp;
    blob.temp = clamp01(blob.temp);

    // y grows downward, so buoyancy is negative and gravity positive. The
    // cycle falls out of this: hot wax rises, cools at the top, sinks,
    // reheats. Nothing scripts the turnaround.
    Vec2 accel{0.0, kGravity - buoyancy * blob.temp};

    const Vec2 swirl = curl_noise(blob.pos.x * kNoiseScale, blob.pos.y * kNoiseScale,
                                  time_ * kNoiseTimeScale);
    accel += swirl * turbulence;

    blob.vel += accel * dt;
    blob.vel = blob.vel * std::exp(-kDrag * dt);
    blob.pos += blob.vel * dt;
  }
}

void LavaSim::emit_impulses(double dt) {
  const double net = channel(heat_.net, heat_.net_ok, 0.0);
  if (net <= 0.0) return;

  // Traffic is spiky, so it gets discrete events rather than a dial: bursts
  // fire jets of droplets up from the floor, reusing wax already in the lamp.
  impulse_credit_ += net * kImpulseRatePerSecond * dt;

  std::uniform_real_distribution<double> unit(0.0, 1.0);
  while (impulse_credit_ >= 1.0) {
    impulse_credit_ -= 1.0;
    if (blobs_.size() >= kHardMaxBlobs || blobs_.empty()) break;

    // Peel each droplet from the largest glob. A population cap alone is not
    // a volume cap: merges keep freeing slots, so adding fresh wax here used
    // to saturate the entire field during long runs. Limit the donation to
    // a quarter of the source's area so a fragmented lamp keeps a viable
    // source glob, too.
    auto source = blobs_.end();
    for (auto it = blobs_.begin(); it != blobs_.end(); ++it) {
      if (it->shape.count != 0) continue;
      if (source == blobs_.end() || it->radius > source->radius) source = it;
    }
    if (source == blobs_.end()) break;  // don't interrupt a shape in flight
    morph_from(*source, ShapeLobe{Vec2{}, source->radius, 1.0, 1.0});
    const double droplet_radius = std::min(base_radius() * kImpulseRadiusFactor,
                                          source->radius * 0.5);
    source->radius = std::sqrt(source->radius * source->radius -
                               droplet_radius * droplet_radius);

    Blob droplet;
    droplet.pos = Vec2{unit(rng_) * width_, height_ * 0.98};
    droplet.vel = Vec2{(unit(rng_) - 0.5) * kImpulseSpeed * 0.3,
                       -kImpulseSpeed * (0.7 + 0.6 * unit(rng_))};
    droplet.radius = droplet_radius;
    droplet.temp = 1.0;
    morph_from(droplet, ShapeLobe{Vec2{}, droplet_radius * 0.25, 0.0, 1.0});
    blobs_.push_back(droplet);
  }
}

void LavaSim::resolve_merges() {
  for (std::size_t i = 0; i < blobs_.size(); ++i) {
    for (std::size_t j = i + 1; j < blobs_.size();) {
      const Blob& a = blobs_[i];
      const Blob& b = blobs_[j];

      if (a.merge_cooldown > 0.0 || b.merge_cooldown > 0.0 ||
          a.shape.count != 0 || b.shape.count != 0) {
        ++j;
        continue;
      }

      const double distance = (a.pos - b.pos).length();
      if (distance >= (a.radius + b.radius) * kMergeFactor) {
        ++j;
        continue;
      }

      // Mass goes as area, so r = sqrt(r1^2 + r2^2) conserves it exactly.
      const double mass_a = a.radius * a.radius;
      const double mass_b = b.radius * b.radius;
      const double mass = mass_a + mass_b;

      Blob merged;
      merged.radius = std::sqrt(mass);
      merged.pos = (a.pos * mass_a + b.pos * mass_b) * (1.0 / mass);
      merged.vel = (a.vel * mass_a + b.vel * mass_b) * (1.0 / mass);
      merged.temp = (a.temp * mass_a + b.temp * mass_b) / mass;
      merged.merge_cooldown = 0.0;
      // At t=0 these are exactly the two original kernels, including their
      // brightness. They move together and grow into the single merged
      // kernel; their final weights sum to one, not two.
      morph_from(merged, ShapeLobe{a.pos - merged.pos, a.radius, 1.0, mass_a / mass});
      merged.shape.lobes[1] = ShapeLobe{b.pos - merged.pos, b.radius, 1.0, mass_b / mass};
      merged.shape.count = 2;

      blobs_[i] = merged;
      blobs_.erase(blobs_.begin() + static_cast<std::ptrdiff_t>(j));
    }
  }
}

void LavaSim::resolve_splits() {
  const double cpu = channel(heat_.cpu, heat_.cpu_ok, 0.0);
  const double gpu = channel(heat_.gpu, heat_.gpu_ok, 0.0);

  const auto target = static_cast<std::size_t>(
      std::lround(kMinBlobs + (kMaxBlobs - kMinBlobs) * cpu));

  const double radius = base_radius();
  const double max_radius = radius * kMaxRadiusFactor;
  const double min_split_radius = radius * kMinSplitRadiusFactor;

  std::uniform_real_distribution<double> unit(0.0, 1.0);

  // One split per step at most: the population should drift toward the
  // target, not snap to it.
  std::size_t candidate = blobs_.size();
  double candidate_radius = min_split_radius;

  for (std::size_t i = 0; i < blobs_.size(); ++i) {
    const Blob& blob = blobs_[i];
    if (blob.shape.count != 0 || blob.merge_cooldown > 0.0 ||
        blob.radius <= candidate_radius) continue;

    const bool oversized = blob.radius > max_radius;
    const bool understaffed = blobs_.size() < target;
    // GPU load makes blobs tear rather than merely move faster: high local
    // shear is what gives the GPU channel a look distinguishable from CPU.
    const Vec2 swirl = curl_noise(blob.pos.x * kNoiseScale, blob.pos.y * kNoiseScale,
                                  time_ * kNoiseTimeScale);
    const bool sheared = swirl.length() * (kTurbulenceBase + kTurbulenceGain * gpu) >
                         kShearSplitThreshold / std::max(gpu, 0.05);

    if (!oversized && !understaffed && !sheared) continue;
    if (blobs_.size() >= kHardMaxBlobs) continue;

    candidate = i;
    candidate_radius = blob.radius;
  }

  if (candidate >= blobs_.size()) return;

  Blob& parent = blobs_[candidate];
  // Area is conserved: two blobs of r/sqrt(2) have the same total area.
  const double child_radius = parent.radius / std::sqrt(2.0);
  const double angle = unit(rng_) * 2.0 * kPi;
  const Vec2 axis{std::cos(angle), std::sin(angle)};

  // Coincident half-weight copies reproduce the parent's field exactly.
  // While the children move apart, each kernel shrinks toward its physical
  // radius and gains its own full weight. Smooth motion alone wasn't enough:
  // immediately substituting two small full-weight kernels caused a flash.
  const ShapeLobe from_parent{Vec2{}, parent.radius, 0.5, 1.0};
  const Vec2 kick = axis * (child_radius * kSplitSeparationSpeed);

  Blob child = parent;
  child.radius = child_radius;
  child.vel = parent.vel - kick;
  child.merge_cooldown = kSplitCooldown;
  morph_from(child, from_parent);

  parent.radius = child_radius;
  parent.vel = parent.vel + kick;
  parent.merge_cooldown = kSplitCooldown;
  morph_from(parent, from_parent);

  blobs_.push_back(child);
}

void LavaSim::enforce_bounds() {
  for (Blob& blob : blobs_) {
    if (!std::isfinite(blob.pos.x) || !std::isfinite(blob.pos.y)) {
      blob.pos = Vec2{width_ * 0.5, height_ * 0.5};
      blob.vel = Vec2{};
    }
    if (!std::isfinite(blob.vel.x) || !std::isfinite(blob.vel.y)) blob.vel = Vec2{};

    // Reflect, then hard clamp. The clamp is what actually guarantees the
    // invariant: reflection alone leaves a blob outside for a frame when the
    // step overshoots far enough.
    if (blob.pos.x < 0.0 && blob.vel.x < 0.0) blob.vel.x = -blob.vel.x * 0.5;
    if (blob.pos.x > width_ && blob.vel.x > 0.0) blob.vel.x = -blob.vel.x * 0.5;
    if (blob.pos.y < 0.0 && blob.vel.y < 0.0) blob.vel.y = -blob.vel.y * 0.5;
    if (blob.pos.y > height_ && blob.vel.y > 0.0) blob.vel.y = -blob.vel.y * 0.5;

    blob.pos.x = std::clamp(blob.pos.x, 0.0, width_);
    blob.pos.y = std::clamp(blob.pos.y, 0.0, height_);
  }
}

void LavaSim::sample_into(Field& field) const {
  const int field_width = field.width();
  const int field_height = field.height();
  if (field_width <= 0 || field_height <= 0) return;

  field.fill(0.0);

  const double to_field_x = static_cast<double>(field_width) / width_;
  const double to_field_y = static_cast<double>(field_height) / height_;

  const auto sample_lobe = [&](Vec2 pos, double radius, double weight) {
    const double influence = radius * kInfluenceFactor;
    if (influence <= 0.0 || weight <= 0.0) return;

    const double centre_x = pos.x * to_field_x;
    const double centre_y = pos.y * to_field_y;
    const double extent_x = influence * to_field_x;
    const double extent_y = influence * to_field_y;

    // Bounding box cull: without it the cost is pixels x blobs every frame.
    const int x_begin = std::max(0, static_cast<int>(std::floor(centre_x - extent_x)));
    const int x_end = std::min(field_width - 1, static_cast<int>(std::ceil(centre_x + extent_x)));
    const int y_begin = std::max(0, static_cast<int>(std::floor(centre_y - extent_y)));
    const int y_end = std::min(field_height - 1, static_cast<int>(std::ceil(centre_y + extent_y)));

    for (int y = y_begin; y <= y_end; ++y) {
      const double dy = (static_cast<double>(y) / to_field_y) - pos.y;
      for (int x = x_begin; x <= x_end; ++x) {
        const double dx = (static_cast<double>(x) / to_field_x) - pos.x;

        const double distance_sq = dx * dx + dy * dy;
        const double influence_sq = influence * influence;
        if (distance_sq >= influence_sq) continue;

        // Smooth polynomial falloff with finite support: C2 at the boundary,
        // so blob edges have no visible seam, and cullable.
        const double u = 1.0 - distance_sq / influence_sq;
        field.at(x, y) += weight * u * u * u;
      }
    }
  };

  for (const Blob& blob : blobs_) {
    if (blob.shape.count == 0) {
      sample_lobe(blob.pos, blob.radius, 1.0);
      continue;
    }
    const double t = clamp01(blob.shape.elapsed / kShapeTransitionSeconds);
    const double blend = t * t * (3.0 - 2.0 * t);  // zero slope at both ends
    for (std::size_t i = 0; i < blob.shape.count; ++i) {
      const ShapeLobe& lobe = blob.shape.lobes[i];
      const Vec2 pos = blob.pos + lobe.offset * (1.0 - blend);
      // Interpolate radius squared and weighted area together. Separately
      // lerping radius and brightness makes merges/splits visibly swell in
      // the middle even though their physical mass is conserved.
      const double old_area = lobe.radius * lobe.radius;
      const double new_area = blob.radius * blob.radius;
      const double area = old_area * (1.0 - blend) + new_area * blend;
      const double weighted_area = lobe.weight * old_area * (1.0 - blend) +
                                   lobe.target_weight * new_area * blend;
      sample_lobe(pos, std::sqrt(area), weighted_area / area);
    }
  }

  for (int y = 0; y < field_height; ++y) {
    for (int x = 0; x < field_width; ++x) {
      field.at(x, y) = clamp01(field.at(x, y) * kFieldGain);
    }
  }
}
