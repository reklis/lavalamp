#include "noise.h"

#include <cmath>

#include "test_harness.h"

TEST(noise_is_in_range) {
  for (int i = 0; i < 2000; ++i) {
    double v = value_noise(i * 0.37, i * 0.11, i * 0.013);
    CHECK(v >= -1.0 && v <= 1.0);
  }
}

TEST(noise_is_deterministic) {
  CHECK_EQ(value_noise(1.5, 2.5, 3.5), value_noise(1.5, 2.5, 3.5));
}

TEST(noise_is_continuous) {
  // adjacent samples must not jump; a hash-only implementation fails this
  double a = value_noise(4.0, 4.0, 0.0);
  double b = value_noise(4.001, 4.0, 0.0);
  CHECK_NEAR(a, b, 0.05);
}

TEST(noise_actually_varies) {
  CHECK(std::abs(value_noise(0.5, 0.5, 0.0) - value_noise(9.5, 17.5, 0.0)) > 1e-6);
}

TEST(curl_noise_is_bounded) {
  for (int i = 0; i < 500; ++i) CHECK(curl_noise(i * 0.3, i * 0.7, 0.0).length() < 10.0);
}

TEST(curl_noise_is_approximately_divergence_free) {
  // the whole reason to use curl noise: it swirls without compressing
  double h = 1e-3, x = 3.3, y = 2.1;
  double dudx = (curl_noise(x + h, y, 0).x - curl_noise(x - h, y, 0).x) / (2 * h);
  double dvdy = (curl_noise(x, y + h, 0).y - curl_noise(x, y - h, 0).y) / (2 * h);
  CHECK_NEAR(dudx + dvdy, 0.0, 1e-3);
}
