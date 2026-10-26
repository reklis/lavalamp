#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// Only physical-ish interfaces and VPN tunnels count. Everything else is
// excluded by name: loopback, Docker's bridge and veth pairs, libvirt
// bridges, and tailscale. This is not tidiness -- on the target machine `lo`
// carries 91 GB and the Docker bridges another 200 GB, so including them
// would mean the lamp mostly reports container chatter.
bool is_counted_interface(std::string_view name);

// Summed rx + tx bytes across counted interfaces. nullopt when no counted
// interface appears at all.
std::optional<std::uint64_t> parse_proc_net_dev(std::string_view contents);

class NetSource {
 public:
  // Returns a normalized burst level in [0, 1], or nullopt on the first call
  // and when dt is not positive.
  //
  // Normalization is logarithmic against a slowly adapting observed peak,
  // because absolute byte rates span orders of magnitude and no fixed ceiling
  // works for both idle browsing and a gigabit transfer. The deliberate
  // consequence: the lamp self-calibrates to this machine's recent habits
  // rather than to absolute bandwidth. Visible response matters more here
  // than a meaningful absolute scale.
  std::optional<double> sample(std::string_view proc_net_dev, double dt);

 private:
  bool has_previous_ = false;
  std::uint64_t previous_bytes_ = 0;
  double ceiling_ = 0.0;
};
