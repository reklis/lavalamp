#pragma once

#include <optional>
#include <string>

// Reads a whole file, or nullopt if it cannot be opened or read.
//
// Every metric source needs this and all of them must degrade gracefully
// when a path is absent, so it lives in one place rather than being
// reimplemented per source.
std::optional<std::string> read_file(const std::string& path);
