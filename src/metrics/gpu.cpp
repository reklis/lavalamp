#include "metrics/gpu.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                           text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

// Accepts exactly "<digits>" or "<digits> %" (with optional surrounding
// whitespace) and nothing else.
std::optional<double> parse_percent_line(std::string_view line) {
  std::string_view token = trim(line);
  if (token.empty()) return std::nullopt;

  if (token.back() == '%') {
    token.remove_suffix(1);
    token = trim(token);
    if (token.empty()) return std::nullopt;
  }

  unsigned long long value = 0;
  const auto* begin = token.data();
  const auto* end = token.data() + token.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;

  return std::clamp(static_cast<double>(value), 0.0, 100.0) / 100.0;
}

std::optional<double> parse_first_percent_line(std::string_view text) {
  std::size_t position = 0;
  while (position <= text.size()) {
    const std::size_t newline = text.find('\n', position);
    const std::string_view line = text.substr(
        position, newline == std::string_view::npos ? std::string_view::npos : newline - position);

    if (const std::optional<double> value = parse_percent_line(line)) return value;

    if (newline == std::string_view::npos) break;
    position = newline + 1;
  }
  return std::nullopt;
}

std::string read_whole_file(const std::string& path) {
  std::ifstream stream(path);
  if (!stream) return {};
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

bool executable_on_path(const char* name) {
  const char* path = std::getenv("PATH");
  if (path == nullptr) return false;

  std::string_view remaining(path);
  while (!remaining.empty()) {
    const std::size_t separator = remaining.find(':');
    const std::string_view directory = remaining.substr(0, separator);
    if (!directory.empty()) {
      std::string candidate(directory);
      candidate += '/';
      candidate += name;
      if (::access(candidate.c_str(), X_OK) == 0) return true;
    }
    if (separator == std::string_view::npos) break;
    remaining.remove_prefix(separator + 1);
  }
  return false;
}

std::string run_capture(const char* command) {
  // stderr is redirected away: nvidia-smi is chatty on failure and anything
  // it printed would land in the middle of the art.
  FILE* pipe = ::popen(command, "r");
  if (pipe == nullptr) return {};

  std::string output;
  char buffer[256];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) output += buffer;
  ::pclose(pipe);
  return output;
}

bool is_card_directory(std::string_view name) {
  // "card1" yes; "card1-DP-9" (a connector) no.
  if (name.size() < 5 || name.substr(0, 4) != "card") return false;
  return std::all_of(name.begin() + 4, name.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::optional<double> parse_nvidia_smi(std::string_view output) {
  return parse_first_percent_line(output);
}

std::optional<double> parse_gpu_busy_percent(std::string_view contents) {
  return parse_percent_line(contents);
}

std::optional<double> combine_gpu(const std::vector<std::optional<double>>& readings) {
  std::optional<double> best;
  for (const std::optional<double>& reading : readings) {
    if (!reading.has_value()) continue;
    if (!best.has_value() || *reading > *best) best = reading;
  }
  return best;
}

std::vector<std::string> discover_amd_busy_paths() {
  std::vector<std::string> paths;

  DIR* directory = ::opendir("/sys/class/drm");
  if (directory == nullptr) return paths;

  while (const dirent* entry = ::readdir(directory)) {
    if (!is_card_directory(entry->d_name)) continue;
    std::string candidate = std::string("/sys/class/drm/") + entry->d_name +
                            "/device/gpu_busy_percent";
    if (::access(candidate.c_str(), R_OK) == 0) paths.push_back(std::move(candidate));
  }
  ::closedir(directory);

  std::sort(paths.begin(), paths.end());
  return paths;
}

GpuSource::GpuSource()
    : has_nvidia_(executable_on_path("nvidia-smi")), amd_paths_(discover_amd_busy_paths()) {}

std::optional<double> GpuSource::sample() {
  std::vector<std::optional<double>> readings;

  if (has_nvidia_) {
    readings.push_back(parse_nvidia_smi(
        run_capture("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader 2>/dev/null")));
  }

  for (const std::string& path : amd_paths_) {
    readings.push_back(parse_gpu_busy_percent(read_whole_file(path)));
  }

  return combine_gpu(readings);
}
