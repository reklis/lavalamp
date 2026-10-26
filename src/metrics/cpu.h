#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

struct CpuCounters {
  std::uint64_t total = 0;
  std::uint64_t idle = 0;  // idle + iowait
};

// Parsers take the file's *contents*, never a path, so they are testable
// against captured fixtures -- including the awkward real-world ones.
std::optional<CpuCounters> parse_proc_stat(std::string_view contents);

// utime + stime, fields 14 and 15 of /proc/self/stat.
std::optional<std::uint64_t> parse_self_jiffies(std::string_view contents);

class CpuSource {
 public:
  // Returns busy fraction in [0, 1], or nullopt when no useful delta is
  // available: on the very first call (the counters are cumulative, so a
  // naive first reading would look like ~100% and slam the lamp to maximum
  // heat at launch) and after a counter reset.
  //
  // The process's own CPU time is subtracted. Without that the renderer's
  // load raises the heat, which raises activity, which raises load -- the
  // lamp would eat itself.
  std::optional<double> sample(std::string_view proc_stat, std::string_view self_stat);

 private:
  bool has_previous_ = false;
  CpuCounters previous_{};
  std::uint64_t previous_self_ = 0;
};
