#include "metrics/memory.h"

#include "test_harness.h"

TEST(memory_uses_available_not_free_so_caches_are_not_pressure) {
  const auto value = parse_memory_usage(
      "MemTotal: 10000 kB\nMemFree: 100 kB\nCached: 8000 kB\nMemAvailable: 9000 kB\n");
  CHECK(value.has_value());
  if (value) CHECK_NEAR(*value, 0.1, 1e-12);
}

TEST(memory_handles_idle_full_and_reordered_counters) {
  CHECK_EQ(parse_memory_usage("MemTotal: 100 kB\nMemAvailable: 100 kB\n"), 0.0);
  CHECK_EQ(parse_memory_usage("MemAvailable:\t0 kB\nMemTotal: 100 kB\n"), 1.0);
}

TEST(memory_rejects_missing_invalid_and_inconsistent_counters) {
  for (const char* contents : {
           "", "MemTotal: 100 kB\n", "MemAvailable: 10 kB\n",
           "MemTotal: 0 kB\nMemAvailable: 0 kB\n",
           "MemTotal: 100 kB\nMemAvailable: 101 kB\n",
           "MemTotal: -100 kB\nMemAvailable: 0 kB\n",
           "MemTotal: 100 MB\nMemAvailable: 0 kB\n",
           "MemTotal: 100 kB extra\nMemAvailable: 0 kB\n",
           "MemTotal: 100 kB\nMemAvailable: nope kB\n",
           "MemTotal: 100 kB\nMemAvailable: 1.5 kB\n",
           "MemTotal: 18446744073709551616 kB\nMemAvailable: 0 kB\n",
           "MemTotal: 100 kB\nMemTotal: 200 kB\nMemAvailable: 0 kB\n"}) {
    CHECK(!parse_memory_usage(contents).has_value());
  }
}
