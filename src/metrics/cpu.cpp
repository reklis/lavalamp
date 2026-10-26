#include "metrics/cpu.h"

#include <algorithm>
#include <charconv>
#include <vector>

namespace {

std::string_view first_line(std::string_view contents) {
  const std::size_t end = contents.find('\n');
  return end == std::string_view::npos ? contents : contents.substr(0, end);
}

// Splits on runs of spaces and tabs. Returns tokens as views into `line`.
std::vector<std::string_view> split_whitespace(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t position = 0;
  while (position < line.size()) {
    while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) ++position;
    const std::size_t start = position;
    while (position < line.size() && line[position] != ' ' && line[position] != '\t') ++position;
    if (position > start) tokens.push_back(line.substr(start, position - start));
  }
  return tokens;
}

std::optional<std::uint64_t> parse_u64(std::string_view token) {
  std::uint64_t value = 0;
  const auto* begin = token.data();
  const auto* end = token.data() + token.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
  return value;
}

}  // namespace

std::optional<CpuCounters> parse_proc_stat(std::string_view contents) {
  const std::string_view line = first_line(contents);
  // "cpu " with the trailing space: "cpu0" and friends are per-core lines,
  // and the lamp wants the aggregate.
  if (line.size() < 4 || line.substr(0, 4) != "cpu ") return std::nullopt;

  const std::vector<std::string_view> tokens = split_whitespace(line.substr(4));
  if (tokens.size() < 8) return std::nullopt;

  CpuCounters counters;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const std::optional<std::uint64_t> value = parse_u64(tokens[i]);
    if (!value.has_value()) return std::nullopt;
    counters.total += *value;
    if (i == 3 || i == 4) counters.idle += *value;  // idle, iowait
  }

  return counters;
}

std::optional<std::uint64_t> parse_self_jiffies(std::string_view contents) {
  // The comm field is parenthesised and may itself contain spaces and
  // parentheses ("(lava lamp (x))"), so splitting the whole line on
  // whitespace misaligns every field after it. Scan past the LAST ')'.
  const std::size_t comm_end = contents.rfind(')');
  if (comm_end == std::string_view::npos) return std::nullopt;

  const std::vector<std::string_view> tokens = split_whitespace(contents.substr(comm_end + 1));
  // tokens[0] is the state character (field 3); utime and stime are fields
  // 14 and 15, so they sit at offsets 11 and 12 from here.
  if (tokens.size() < 13) return std::nullopt;

  const std::optional<std::uint64_t> utime = parse_u64(tokens[11]);
  const std::optional<std::uint64_t> stime = parse_u64(tokens[12]);
  if (!utime.has_value() || !stime.has_value()) return std::nullopt;

  return *utime + *stime;
}

std::optional<double> CpuSource::sample(std::string_view proc_stat,
                                        std::string_view self_stat) {
  const std::optional<CpuCounters> current = parse_proc_stat(proc_stat);
  if (!current.has_value()) return std::nullopt;

  // Self jiffies are best-effort: if /proc/self/stat is unreadable we simply
  // do not subtract, rather than losing the CPU channel entirely.
  const std::uint64_t current_self = parse_self_jiffies(self_stat).value_or(0);

  const bool had_previous = has_previous_;
  const CpuCounters previous = previous_;
  const std::uint64_t previous_self = previous_self_;

  has_previous_ = true;
  previous_ = *current;
  previous_self_ = current_self;

  if (!had_previous) return std::nullopt;

  // A counter going backwards means a reset (or a container boundary moving
  // under us). There is no meaningful rate to report for that interval.
  if (current->total <= previous.total || current->idle < previous.idle) return std::nullopt;

  const double total_delta = static_cast<double>(current->total - previous.total);
  const double idle_delta = static_cast<double>(current->idle - previous.idle);
  const double self_delta = current_self >= previous_self
                                ? static_cast<double>(current_self - previous_self)
                                : 0.0;

  const double busy_delta = std::max(total_delta - idle_delta - self_delta, 0.0);
  return std::clamp(busy_delta / total_delta, 0.0, 1.0);
}
