#include "metrics/gpu.h"

#include "test_harness.h"

TEST(parses_nvidia_smi_csv) {
  CHECK_NEAR(*parse_nvidia_smi("utilization.gpu [%]\n34 %\n"), 0.34, 1e-9);
  CHECK_NEAR(*parse_nvidia_smi("34 %\n"), 0.34, 1e-9);
  CHECK_NEAR(*parse_nvidia_smi("0 %\n"), 0.0, 1e-9);
}

TEST(nvidia_smi_failures_are_unavailable) {  // Review Focus 4
  CHECK(!parse_nvidia_smi("").has_value());
  CHECK(!parse_nvidia_smi("[N/A]\n").has_value());
  CHECK(!parse_nvidia_smi("No devices were found\n").has_value());
  CHECK(!parse_nvidia_smi("Failed to initialize NVML: Driver/library version mismatch\n")
             .has_value());
  CHECK(!parse_nvidia_smi("utilization.gpu [%]\n").has_value());
}

TEST(parses_amd_busy_percent) {
  CHECK_NEAR(*parse_gpu_busy_percent("16\n"), 0.16, 1e-9);
  CHECK_NEAR(*parse_gpu_busy_percent("100"), 1.0, 1e-9);
  CHECK(!parse_gpu_busy_percent("").has_value());
  CHECK(!parse_gpu_busy_percent("N/A\n").has_value());
}

TEST(out_of_range_percentages_are_clamped) {
  CHECK_NEAR(*parse_gpu_busy_percent("250\n"), 1.0, 1e-9);
}

TEST(combine_takes_max_of_available) {
  CHECK_NEAR(*combine_gpu({0.34, 0.16}), 0.34, 1e-9);
  CHECK_NEAR(*combine_gpu({std::nullopt, 0.16}), 0.16, 1e-9);
  CHECK_NEAR(*combine_gpu({0.34, std::nullopt}), 0.34, 1e-9);
}

TEST(combine_with_nothing_available_is_unavailable) {
  CHECK(!combine_gpu({}).has_value());
  CHECK(!combine_gpu({std::nullopt, std::nullopt}).has_value());
}
