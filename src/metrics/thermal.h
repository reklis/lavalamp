#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Millidegrees to degrees Celsius. Accepts only the plausible window
// 1-150 C: unpopulated thermal zones report 0, and that zero would otherwise
// pin the palette to its cool extreme while the machine cooks.
std::optional<double> parse_thermal_temp(std::string_view contents);

// 40 C -> 0.0, 90 C -> 1.0, clamped. The most literal mapping in the project:
// the lamp glows hotter when the laptop actually is hotter.
double normalize_temp(double celsius);

// Prefers package/CPU zones by type, falling back to every zone when none of
// the known types are present.
std::vector<std::string> discover_thermal_paths();

class ThermalSource {
 public:
  ThermalSource();

  // Max valid reading across the discovered zones, normalized.
  std::optional<double> sample();

 private:
  std::vector<std::string> paths_;
};
