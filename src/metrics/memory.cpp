#include "metrics/memory.h"

#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>

std::optional<double> parse_memory_usage(std::string_view meminfo) {
  std::optional<std::uint64_t> total, available;
  std::istringstream lines{std::string(meminfo)};
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key, number, unit, extra;
    fields >> key;
    if (key != "MemTotal:" && key != "MemAvailable:") continue;
    if (!(fields >> number >> unit) || unit != "kB" || (fields >> extra))
      return std::nullopt;
    std::uint64_t value = 0;
    const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size())
      return std::nullopt;
    auto& counter = key == "MemTotal:" ? total : available;
    if (counter.has_value()) return std::nullopt;
    counter = value;
  }
  if (!total || !available || *total == 0 || *available > *total) return std::nullopt;
  return 1.0 - static_cast<double>(*available) / static_cast<double>(*total);
}
