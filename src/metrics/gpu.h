#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Both parsers return utilization in [0, 1], or nullopt when the input is not
// a utilization reading.
//
// Rejecting non-numeric content is the entire job here, not a nicety:
// nvidia-smi reports every failure as prose on stdout ("No devices were
// found", "Failed to initialize NVML: Driver/library version mismatch"), so a
// parser that reaches for the first digit it can find will happily turn a
// driver mismatch into a utilization number.
std::optional<double> parse_nvidia_smi(std::string_view output);
std::optional<double> parse_gpu_busy_percent(std::string_view contents);

// Max of whatever is available; nullopt when nothing is. The target machine
// is a hybrid laptop, so reading only one GPU would make the lamp go dead
// whenever work is scheduled on the other one.
std::optional<double> combine_gpu(const std::vector<std::optional<double>>& readings);

std::vector<std::string> discover_amd_busy_paths();

class GpuSource {
 public:
  // Probes once for what this machine actually exposes, so a box with no
  // NVIDIA GPU does not pay a failed popen every second.
  GpuSource();

  // Runs nvidia-smi via popen (~100 ms) and reads the AMD sysfs nodes. Must
  // only be called from the metrics thread -- never from the render loop.
  std::optional<double> sample();

 private:
  bool has_nvidia_ = false;
  std::vector<std::string> amd_paths_;
};
