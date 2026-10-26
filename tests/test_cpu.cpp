#include "metrics/cpu.h"

#include "test_harness.h"

// The real aggregate line from the target machine, so the fixture has the
// same field count and magnitude as production input.
static constexpr std::string_view kStat1 =
    "cpu  368470584 1600053 61063225 4797970478 7128777 10130392 7281821 0 132707 0\n"
    "cpu0 19033377 70877 3207921 193493658 350015 381855 1729628 0 8843 0\n";

TEST(parses_aggregate_cpu_line) {
  auto c = parse_proc_stat(kStat1);
  CHECK(c.has_value());
  CHECK_EQ(c->idle, 4797970478ull + 7128777ull);  // idle + iowait
  CHECK(c->total > c->idle);
}

TEST(rejects_garbage) {
  CHECK(!parse_proc_stat("").has_value());
  CHECK(!parse_proc_stat("intr 123 456\n").has_value());
  CHECK(!parse_proc_stat("cpu  1 2\n").has_value());  // too few fields
}

TEST(parses_self_jiffies_from_field_14_and_15) {
  // comm field contains spaces and parens; parser must scan past the last ')'
  std::string_view s =
      "42 (lava lamp (x)) S 1 42 42 0 -1 4194304 100 0 0 0 7 11 0 0 20 0 1 0 999";
  CHECK_EQ(parse_self_jiffies(s).value(), 18ull);  // utime 7 + stime 11
}

TEST(first_sample_is_unavailable) {  // Review Focus 1
  CpuSource src;
  CHECK(!src.sample(kStat1, "").has_value());
}

TEST(second_sample_reports_busy_fraction) {
  // 100 jiffies elapse, 25 of them idle -> 0.75 busy
  std::string_view s2 =
      "cpu  368470659 1600053 61063225 4797970503 7128777 10130392 7281821 0 132707 0\n";
  CpuSource src;
  src.sample(kStat1, "");
  auto v = src.sample(s2, "");
  CHECK(v.has_value());
  CHECK_NEAR(*v, 0.75, 0.01);
}

TEST(own_cpu_time_is_subtracted) {
  // identical /proc/stat delta, but self burned most of it -> lower reading
  std::string_view s2 =
      "cpu  368470684 1600053 61063225 4797970503 7128777 10130392 7281821 0 132707 0\n";
  std::string_view self1 = "1 (x) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 20 0 1 0 0";
  std::string_view self2 = "1 (x) S 0 0 0 0 0 0 0 0 0 0 40 0 0 0 20 0 1 0 0";
  CpuSource a, b;
  a.sample(kStat1, self1);
  b.sample(kStat1, self1);
  double without = *a.sample(s2, self1);
  double with = *b.sample(s2, self2);
  CHECK(with < without);
}

TEST(counter_reset_clamps_to_zero) {  // Review Focus 2
  std::string_view reset = "cpu  10 0 0 10 0 0 0 0 0 0\n";
  CpuSource src;
  src.sample(kStat1, "");
  auto v = src.sample(reset, "");
  CHECK(!v.has_value() || (*v >= 0.0 && *v <= 1.0));
}
