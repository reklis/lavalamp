#pragma once

#include <optional>
#include <string_view>

// Used RAM, excluding reclaimable caches: 1 - MemAvailable / MemTotal.
// Missing or malformed counters are unavailable, not zero usage.
std::optional<double> parse_memory_usage(std::string_view meminfo);
