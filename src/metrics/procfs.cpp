#include "metrics/procfs.h"

#include <fstream>
#include <sstream>

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream stream(path);
  if (!stream) return std::nullopt;

  std::ostringstream contents;
  contents << stream.rdbuf();
  if (stream.bad()) return std::nullopt;

  return contents.str();
}
