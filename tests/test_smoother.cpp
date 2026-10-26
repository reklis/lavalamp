#include "metrics/heat.h"

#include "test_harness.h"

TEST(smoother_starts_at_zero) { CHECK_EQ(Smoother(1.0, 1.0).value(), 0.0); }

TEST(smoother_converges_toward_target) {
  Smoother s(1.0, 1.0);
  for (int i = 0; i < 1000; ++i) s.update(1.0, 0.01);
  CHECK_NEAR(s.value(), 1.0, 1e-3);
}

TEST(attack_faster_than_decay_rises_quicker_than_it_falls) {
  Smoother s(0.2, 3.0);
  for (int i = 0; i < 20; ++i) s.update(1.0, 0.01);  // 0.2s rising
  double risen = s.value();
  for (int i = 0; i < 20; ++i) s.update(0.0, 0.01);  // 0.2s falling
  CHECK(s.value() > risen * 0.5);                    // decayed far less than it rose
}

TEST(large_dt_does_not_overshoot) {
  Smoother s(1.0, 1.0);
  s.update(1.0, 1e6);
  CHECK(s.value() <= 1.0);  // must not exceed target no matter the step
}

TEST(zero_dt_is_a_noop) {
  Smoother s(1.0, 1.0);
  s.update(1.0, 0.5);
  double before = s.value();
  s.update(1.0, 0.0);
  CHECK_EQ(s.value(), before);
}
