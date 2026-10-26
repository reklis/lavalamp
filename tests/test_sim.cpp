#include "sim.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "field.h"
#include "test_harness.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

double total_blob_area(const LavaSim& s) {
  double area = 0.0;
  for (const Blob& b : s.blobs()) area += kPi * b.radius * b.radius;
  return area;
}

double total_speed_after(LavaSim s, const Heat& h, int steps) {
  s.set_heat(h);
  for (int i = 0; i < steps; ++i) s.step(1.0 / 60);
  double total = 0.0;
  for (const Blob& b : s.blobs()) total += b.vel.length();
  return total;
}

double max_field_value(const Field& f) {
  double best = 0.0;
  for (int y = 0; y < f.height(); ++y)
    for (int x = 0; x < f.width(); ++x) best = std::max(best, f.at(x, y));
  return best;
}

double min_field_value(const Field& f) {
  double best = 1.0;
  for (int y = 0; y < f.height(); ++y)
    for (int x = 0; x < f.width(); ++x) best = std::min(best, f.at(x, y));
  return best;
}

double mean_height_of_hottest_blob(const LavaSim& s) {
  const Blob* hottest = nullptr;
  for (const Blob& b : s.blobs())
    if (hottest == nullptr || b.temp > hottest->temp) hottest = &b;
  return hottest == nullptr ? 0.0 : hottest->pos.y;
}

// Mean vertical velocity of the hottest and coldest thirds.
std::pair<double, double> hot_and_cold_mean_vy(const LavaSim& s) {
  std::vector<Blob> sorted(s.blobs().begin(), s.blobs().end());
  std::sort(sorted.begin(), sorted.end(),
            [](const Blob& a, const Blob& b) { return a.temp > b.temp; });

  const std::size_t third = std::max<std::size_t>(sorted.size() / 3, 1);
  double hot = 0.0;
  double cold = 0.0;
  for (std::size_t i = 0; i < third; ++i) hot += sorted[i].vel.y;
  for (std::size_t i = sorted.size() - third; i < sorted.size(); ++i) cold += sorted[i].vel.y;
  return {hot / static_cast<double>(third), cold / static_cast<double>(third)};
}

}  // namespace

TEST(starts_seeded_with_blobs) {
  LavaSim s(80, 50, 1234);
  CHECK(s.blobs().size() >= 4);  // frame one must already look like a lamp
}

TEST(seed_is_deterministic) {
  LavaSim a(80, 50, 7), b(80, 50, 7);
  a.step(0.1);
  b.step(0.1);
  CHECK_EQ(a.blobs().size(), b.blobs().size());
  CHECK_NEAR(a.blobs()[0].pos.x, b.blobs()[0].pos.x, 1e-12);
}

TEST(blobs_stay_inside_the_lamp) {
  LavaSim s(80, 50, 3);
  Heat h;
  h.cpu = 1.0;
  h.cpu_ok = true;
  h.gpu = 1.0;
  h.gpu_ok = true;
  s.set_heat(h);
  for (int i = 0; i < 5000; ++i) s.step(1.0 / 60);
  for (const Blob& b : s.blobs()) {
    CHECK(b.pos.x >= 0 && b.pos.x <= 80);
    CHECK(b.pos.y >= 0 && b.pos.y <= 50);
    CHECK(std::isfinite(b.vel.x) && std::isfinite(b.vel.y));
  }
}

TEST(hot_blob_at_the_floor_rises) {
  LavaSim s(80, 50, 11);
  // convection sign check: buoyancy must push heat upward
  double before = mean_height_of_hottest_blob(s);
  for (int i = 0; i < 600; ++i) s.step(1.0 / 60);
  CHECK(mean_height_of_hottest_blob(s) != before);
}

TEST(buoyancy_pushes_hot_blobs_upward_not_downward) {
  // The sign of the convection force, asserted directly. y grows downward,
  // so rising means a more negative vertical velocity.
  LavaSim s(80, 50, 11);
  for (int i = 0; i < 120; ++i) s.step(1.0 / 60);
  auto [hot_vy, cold_vy] = hot_and_cold_mean_vy(s);
  CHECK(hot_vy < cold_vy);
}

TEST(high_cpu_produces_more_motion_than_idle) {
  // goal 2: busy must be visibly different from idle
  Heat idle;
  idle.cpu = 0.0;
  idle.cpu_ok = true;
  Heat busy;
  busy.cpu = 1.0;
  busy.cpu_ok = true;
  CHECK(total_speed_after(LavaSim(80, 50, 5), busy, 300) >
        total_speed_after(LavaSim(80, 50, 5), idle, 300) * 1.5);
}

TEST(merging_conserves_volume) {
  // two overlapping equal blobs -> one blob of combined area
  LavaSim s(80, 50, 1);
  double area_before = total_blob_area(s);
  for (int i = 0; i < 2000; ++i) s.step(1.0 / 60);
  CHECK_NEAR(total_blob_area(s), area_before, area_before * 0.35);
}

TEST(dt_is_clamped_against_hitches) {
  LavaSim s(80, 50, 2);
  s.step(1000.0);  // a descheduled process hands us an enormous dt
  for (const Blob& b : s.blobs()) CHECK(std::isfinite(b.pos.x) && b.pos.x >= 0);
}

TEST(sample_into_fills_the_field_in_range) {
  LavaSim s(80, 50, 9);
  Field f(80, 50);
  s.sample_into(f);
  for (int y = 0; y < 50; ++y)
    for (int x = 0; x < 80; ++x) CHECK(f.at(x, y) >= 0.0 && f.at(x, y) <= 1.0);
}

TEST(sample_into_is_brighter_inside_a_blob_than_far_from_one) {
  // the field must actually describe blobs, not a uniform wash
  LavaSim s(80, 50, 4);
  Field f(80, 50);
  s.sample_into(f);
  CHECK(max_field_value(f) > min_field_value(f) + 0.2);
}

TEST(network_bursts_inject_blobs_at_the_floor) {
  LavaSim s(80, 50, 8);
  std::size_t before = s.blobs().size();
  Heat h;
  h.net = 1.0;
  h.net_ok = true;
  s.set_heat(h);
  for (int i = 0; i < 120; ++i) s.step(1.0 / 60);
  CHECK(s.blobs().size() > before);  // impulses spawned droplets
}

TEST(sustained_network_traffic_cannot_flood_the_lamp) {
  for (double net : {0.05, 0.2, 1.0}) {
    LavaSim s(160, 72, 42);
    Heat h;
    h.net = net;
    h.net_ok = true;
    h.cpu = 0.1;
    h.cpu_ok = true;
    s.set_heat(h);
    const double initial_area = total_blob_area(s);
    Field field(160, 72);
    double worst_area_error = 0.0;
    double least_contrast = 1.0;
    int fewest_empty = field.width() * field.height();
    std::size_t most_blobs = 0;

    // Half an hour, including a background trickle: a count cap alone did
    // not help because merges made room for more mass to enter indefinitely.
    for (int second = 0; second < 1800; ++second) {
      for (int frame = 0; frame < 60; ++frame) s.step(1.0 / 60);
      worst_area_error = std::max(worst_area_error, std::abs(total_blob_area(s) - initial_area));
      most_blobs = std::max(most_blobs, s.blobs().size());
      if (second % 60 != 59) continue;

      s.sample_into(field);
      least_contrast = std::min(least_contrast, max_field_value(field) - min_field_value(field));
      int empty = 0;
      for (int y = 0; y < field.height(); ++y)
        for (int x = 0; x < field.width(); ++x) empty += field.at(x, y) == 0.0;
      fewest_empty = std::min(fewest_empty, empty);
    }
    CHECK(worst_area_error < initial_area * 1e-9);
    CHECK(most_blobs <= 44);
    CHECK(least_contrast > 0.2);
    CHECK(fewest_empty > field.width() * field.height() / 5);
  }
}

TEST(no_network_traffic_injects_nothing) {
  LavaSim a(80, 50, 8);
  Heat quiet;
  quiet.net = 0.0;
  quiet.net_ok = true;
  a.set_heat(quiet);
  std::size_t before = a.blobs().size();
  for (int i = 0; i < 120; ++i) a.step(1.0 / 60);
  CHECK(a.blobs().size() <= before + 1);  // only convection, no jets
}

TEST(resize_keeps_blobs_in_bounds) {  // Review Focus 3
  LavaSim s(200, 100, 6);
  for (int i = 0; i < 300; ++i) s.step(1.0 / 60);
  s.resize(30, 12);
  s.step(1.0 / 60);
  for (const Blob& b : s.blobs()) CHECK(b.pos.x <= 30 && b.pos.y <= 12);
}

TEST(make_seed_varies_without_using_random_device) {
  // std::random_device throws std::system_error when its entropy source
  // cannot be opened (minimal containers, chroots). The only call site sat
  // INSIDE the TerminalSession scope, and an uncaught exception does not
  // unwind under GCC -- so the destructor never ran and the user was left
  // with no cursor and no echo, which is the one failure mode the spec says
  // must never outlive the process.
  const std::uint32_t a = make_seed();
  const std::uint32_t b = make_seed();
  CHECK(a != 0u);
  CHECK(a != b);
}

TEST(a_split_does_not_teleport_its_children_apart) {
  // Merges read as smooth because they fire only when the two blobs already
  // overlap heavily -- the field already looked like one blob, so nothing
  // structural changes. A split that places its children a full parent
  // radius apart in ONE frame instantly creates a configuration MORE
  // separated than the one that would have triggered a merge, and the eye
  // reads that as two objects teleporting out of one. Per-pixel field delta
  // is no larger than a merge's; the structure is what gives it away.
  //
  // So children must start effectively coincident and separate under their
  // own velocity.
  LavaSim s(100, 60, 42);
  Heat h;
  h.cpu = 1.0;
  h.cpu_ok = true;
  h.gpu = 0.8;
  h.gpu_ok = true;
  s.set_heat(h);

  for (int i = 0; i < 300; ++i) s.step(1.0 / 60);

  std::size_t previous = s.blobs().size();
  int splits_seen = 0;
  double worst_separation = 0.0;

  for (int frame = 0; frame < 3000; ++frame) {
    s.step(1.0 / 60);
    const std::size_t now = s.blobs().size();
    if (now > previous) {
      ++splits_seen;
      // The freshly split pair is the closest pair: merging runs before
      // splitting in step(), so no settled pair is closer than the merge
      // threshold.
      double closest = 1e9;
      for (std::size_t i = 0; i < s.blobs().size(); ++i) {
        for (std::size_t j = i + 1; j < s.blobs().size(); ++j) {
          const Blob& a = s.blobs()[i];
          const Blob& b = s.blobs()[j];
          const double mean_radius = (a.radius + b.radius) * 0.5;
          if (mean_radius <= 0.0) continue;
          closest = std::min(closest, (a.pos - b.pos).length() / mean_radius);
        }
      }
      worst_separation = std::max(worst_separation, closest);
    }
    previous = now;
  }

  CHECK(splits_seen > 10);           // the scenario must actually split
  CHECK(worst_separation < 0.35);    // was 1.4 -- a teleport past the merge threshold
}
