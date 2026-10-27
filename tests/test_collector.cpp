#include "metrics/collector.h"

#include "test_harness.h"

TEST(snapshot_before_start_is_all_unavailable) {
  Collector c;
  Heat h = c.snapshot();
  CHECK(!h.cpu_ok && !h.gpu_ok && !h.net_ok && !h.temp_ok &&
        !h.memory_ok && !h.audio_ok);
  CHECK_EQ(h.cpu, 0.0);
}

TEST(snapshot_values_are_always_in_range) {
  Collector c;
  c.start();
  for (int i = 0; i < 50; ++i) {
    Heat h = c.snapshot();  // hammered concurrently with the poll thread
    CHECK(h.cpu >= 0.0 && h.cpu <= 1.0);
    CHECK(h.gpu >= 0.0 && h.gpu <= 1.0);
    CHECK(h.net >= 0.0 && h.net <= 1.0);
    CHECK(h.temp >= 0.0 && h.temp <= 1.0);
    CHECK(h.memory >= 0.0 && h.memory <= 1.0);
    CHECK(h.audio >= 0.0 && h.audio <= 1.0);
  }
  c.stop();
}

TEST(stop_is_idempotent_and_start_stop_does_not_hang) {
  Collector c;
  c.start();
  c.stop();
  c.stop();
}

TEST(destructor_stops_a_running_thread) {
  Collector c;
  c.start();  // must not terminate or leak the thread when c goes out of scope
}

TEST(read_file_on_missing_path_is_unavailable) {
  CHECK(!read_file("/proc/definitely-not-a-real-file").has_value());
}
