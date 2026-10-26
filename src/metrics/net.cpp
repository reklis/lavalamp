#include "metrics/net.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <vector>

namespace {

// Floor for the adaptive ceiling: 256 KiB/s. Without a floor, the first
// trickle of traffic on an idle machine would become the reference peak and
// read as a full-scale burst.
constexpr double kCeilingFloor = 256.0 * 1024.0;
// The ceiling relaxes by this factor per second, so an old bulk transfer does
// not permanently flatten the scale.
constexpr double kCeilingDecayPerSecond = 0.97;

constexpr std::array<std::string_view, 6> kCountedPrefixes = {"en", "eth", "wl", "ww", "tun",
                                                              "ppp"};

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

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

bool is_counted_interface(std::string_view name) {
  // Bridges are named with a dash on this machine (br-349bfb9a64e1) and no
  // physical interface is, so reject them outright before the prefix test.
  if (name.find('-') != std::string_view::npos) return false;

  return std::any_of(kCountedPrefixes.begin(), kCountedPrefixes.end(),
                     [name](std::string_view prefix) {
                       return name.size() > prefix.size() &&
                              name.substr(0, prefix.size()) == prefix;
                     });
}

std::optional<std::uint64_t> parse_proc_net_dev(std::string_view contents) {
  std::uint64_t total = 0;
  bool found_any = false;

  // Requiring a colon skips both header lines without hardcoding a line
  // count, and skips malformed rows for the same reason.
  std::size_t position = 0;
  while (position <= contents.size()) {
    const std::size_t newline = contents.find('\n', position);
    const std::string_view line =
        contents.substr(position, newline == std::string_view::npos ? std::string_view::npos
                                                                    : newline - position);

    const std::size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
      const std::string_view name = trim(line.substr(0, colon));
      if (is_counted_interface(name)) {
        const std::vector<std::string_view> fields = split_whitespace(line.substr(colon + 1));
        // Field 0 is rx bytes; field 8 is tx bytes.
        if (fields.size() >= 9) {
          const std::optional<std::uint64_t> rx = parse_u64(fields[0]);
          const std::optional<std::uint64_t> tx = parse_u64(fields[8]);
          if (rx.has_value() && tx.has_value()) {
            total += *rx + *tx;
            found_any = true;
          }
        }
      }
    }

    if (newline == std::string_view::npos) break;
    position = newline + 1;
  }

  if (!found_any) return std::nullopt;
  return total;
}

std::optional<double> NetSource::sample(std::string_view proc_net_dev, double dt) {
  const std::optional<std::uint64_t> current = parse_proc_net_dev(proc_net_dev);
  if (!current.has_value()) return std::nullopt;

  const bool had_previous = has_previous_;
  const std::uint64_t previous = previous_bytes_;

  has_previous_ = true;
  previous_bytes_ = *current;

  if (!had_previous) return std::nullopt;
  if (!(dt > 0.0)) return std::nullopt;

  // A total that went down means a counter reset or an interface that
  // disappeared, not negative traffic.
  const double delta = *current >= previous ? static_cast<double>(*current - previous) : 0.0;
  const double rate = delta / dt;

  ceiling_ = std::max({kCeilingFloor, ceiling_ * std::pow(kCeilingDecayPerSecond, dt), rate});

  const double scaled = std::log10(1.0 + rate) / std::log10(1.0 + ceiling_);
  return std::clamp(scaled, 0.0, 1.0);
}
