#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "metrics/cpu.h"
#include "metrics/gpu.h"
#include "metrics/heat.h"
#include "metrics/net.h"
#include "metrics/procfs.h"
#include "metrics/thermal.h"

// Polls the machine on its own thread at 1 Hz and publishes an immutable
// Heat snapshot.
//
// The separate thread is not an optimisation, it is a requirement: nvidia-smi
// costs around 100 ms per call, and doing that inside a 30 fps render loop
// would stutter the animation into garbage. The render loop reads the
// last-known snapshot every frame and never blocks on I/O.
class Collector {
 public:
  Collector();
  ~Collector();

  Collector(const Collector&) = delete;
  Collector& operator=(const Collector&) = delete;

  void start();
  void stop();

  // Safe to call every frame: takes the lock, copies, returns. Never does
  // I/O. Values are clamped to [0, 1] at publication, so callers can trust
  // the range unconditionally.
  Heat snapshot() const;

 private:
  void run();
  void poll_once(double dt);

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::thread thread_;
  Heat published_{};

  CpuSource cpu_source_;
  GpuSource gpu_source_;
  NetSource net_source_;
  ThermalSource thermal_source_;

  // Time constants from the spec: CPU responsive but not jittery, GPU
  // moderate, temperature slow because it physically is, network fast attack
  // and slow decay so a burst registers at once and then fades.
  Smoother cpu_smoother_{1.0, 1.0};
  Smoother gpu_smoother_{2.0, 2.0};
  Smoother net_smoother_{0.2, 3.0};
  Smoother temp_smoother_{10.0, 10.0};
};
