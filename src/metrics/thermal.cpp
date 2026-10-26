#include "metrics/thermal.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <dirent.h>

#include "metrics/procfs.h"

namespace {

constexpr double kColdCelsius = 40.0;
constexpr double kHotCelsius = 90.0;

// A thermal zone reporting outside this is not reporting a CPU temperature.
constexpr double kMinPlausibleCelsius = 1.0;
constexpr double kMaxPlausibleCelsius = 150.0;

constexpr std::array<std::string_view, 3> kPreferredTypes = {"acpitz", "x86_pkg_temp", "k10temp"};

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                           text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

bool is_zone_directory(std::string_view name) {
  constexpr std::string_view prefix = "thermal_zone";
  if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) return false;
  return std::all_of(name.begin() + prefix.size(), name.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::optional<double> parse_thermal_temp(std::string_view contents) {
  const std::string_view token = trim(contents);
  if (token.empty()) return std::nullopt;

  long long millidegrees = 0;
  const auto* begin = token.data();
  const auto* end = token.data() + token.size();
  const auto result = std::from_chars(begin, end, millidegrees);
  if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;

  const double celsius = static_cast<double>(millidegrees) / 1000.0;
  if (celsius < kMinPlausibleCelsius || celsius > kMaxPlausibleCelsius) return std::nullopt;
  return celsius;
}

double normalize_temp(double celsius) {
  const double scaled = (celsius - kColdCelsius) / (kHotCelsius - kColdCelsius);
  return std::clamp(scaled, 0.0, 1.0);
}

std::vector<std::string> discover_thermal_paths() {
  std::vector<std::string> preferred;
  std::vector<std::string> all;

  DIR* directory = ::opendir("/sys/class/thermal");
  if (directory == nullptr) return preferred;

  while (const dirent* entry = ::readdir(directory)) {
    if (!is_zone_directory(entry->d_name)) continue;

    const std::string base = std::string("/sys/class/thermal/") + entry->d_name;
    const std::string temp_path = base + "/temp";
    if (!read_file(temp_path).has_value()) continue;

    all.push_back(temp_path);

    // Held in a named string: trim() returns a view, and a view into a
    // temporary would dangle the moment this statement ended.
    const std::string type_contents = read_file(base + "/type").value_or(std::string{});
    const std::string_view type = trim(type_contents);
    if (std::find(kPreferredTypes.begin(), kPreferredTypes.end(), type) !=
        kPreferredTypes.end()) {
      preferred.push_back(temp_path);
    }
  }
  ::closedir(directory);

  std::vector<std::string>& chosen = preferred.empty() ? all : preferred;
  std::sort(chosen.begin(), chosen.end());
  return chosen;
}

ThermalSource::ThermalSource() : paths_(discover_thermal_paths()) {}

std::optional<double> ThermalSource::sample() {
  std::optional<double> hottest;

  for (const std::string& path : paths_) {
    const std::optional<double> celsius = parse_thermal_temp(read_file(path).value_or(std::string{}));
    if (!celsius.has_value()) continue;
    if (!hottest.has_value() || *celsius > *hottest) hottest = celsius;
  }

  if (!hottest.has_value()) return std::nullopt;
  return normalize_temp(*hottest);
}
