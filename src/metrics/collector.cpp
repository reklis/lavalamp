#include "metrics/collector.h"

#include <algorithm>
#include <chrono>

namespace {

constexpr double kPollSeconds = 1.0;
constexpr double kAudioPollSeconds = 0.05;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

}  // namespace

Collector::Collector() = default;

Collector::~Collector() {
  // A std::thread destroyed while still joinable calls std::terminate, so
  // this is not optional tidiness.
  stop();
}

void Collector::start() {
  if (thread_.joinable()) return;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = false;
  }
  thread_ = std::thread(&Collector::run, this);
}

void Collector::stop() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
  audio_source_.stop();
}

Heat Collector::snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return published_;
}

void Collector::run() try {
  // The first poll only primes the cumulative-counter deltas; CPU and network
  // deliberately report nothing until the second one.
  poll_once(kPollSeconds);

  auto previous = std::chrono::steady_clock::now();
  auto previous_metrics = previous;

  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Waiting on the condition variable rather than sleeping means stop()
      // returns immediately instead of blocking for up to a poll interval.
      wake_.wait_for(lock, std::chrono::duration<double>(kAudioPollSeconds),
                     [this] { return stopping_; });
      if (stopping_) return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - previous).count();
    previous = now;

    poll_audio(dt);
    const double metrics_dt = std::chrono::duration<double>(now - previous_metrics).count();
    if (metrics_dt >= kPollSeconds) {
      poll_once(metrics_dt);
      previous_metrics = now;
    }
  }
} catch (...) {
  // An exception escaping a std::thread calls std::terminate, which would
  // skip the TerminalSession destructor and leave the terminal wrecked. The
  // lamp is better off losing its metrics and running on neutral values: the
  // last published snapshot simply stops updating.
}

void Collector::poll_once(double dt) {
  // All I/O happens with no lock held: nvidia-smi alone takes ~100 ms, and
  // snapshot() is called every frame by the render loop.
  const std::optional<std::string> proc_stat = read_file("/proc/stat");
  const std::optional<std::string> self_stat = read_file("/proc/self/stat");
  const std::optional<std::string> net_dev = read_file("/proc/net/dev");
  const std::optional<std::string> meminfo = read_file("/proc/meminfo");

  const std::optional<double> raw_cpu =
      proc_stat.has_value()
          ? cpu_source_.sample(*proc_stat, self_stat.value_or(std::string{}))
          : std::nullopt;
  const std::optional<double> raw_gpu = gpu_source_.sample();
  const std::optional<double> raw_net =
      net_dev.has_value() ? net_source_.sample(*net_dev, dt) : std::nullopt;
  const std::optional<double> raw_temp = thermal_source_.sample();
  const std::optional<double> raw_memory =
      meminfo.has_value() ? parse_memory_usage(*meminfo) : std::nullopt;

  Heat heat;

  // An unavailable channel leaves its smoother untouched rather than
  // decaying it toward zero: a momentarily unreadable source should not drag
  // the lamp cold and then let it surge back.
  if (raw_cpu.has_value()) {
    heat.cpu = clamp01(cpu_smoother_.update(clamp01(*raw_cpu), dt));
    heat.cpu_ok = true;
  }
  if (raw_gpu.has_value()) {
    heat.gpu = clamp01(gpu_smoother_.update(clamp01(*raw_gpu), dt));
    heat.gpu_ok = true;
  }
  if (raw_net.has_value()) {
    heat.net = clamp01(net_smoother_.update(clamp01(*raw_net), dt));
    heat.net_ok = true;
  }
  if (raw_temp.has_value()) {
    heat.temp = clamp01(temp_smoother_.update(clamp01(*raw_temp), dt));
    heat.temp_ok = true;
  }

  if (raw_memory.has_value()) {
    heat.memory = clamp01(memory_smoother_.update(*raw_memory, dt));
    heat.memory_ok = true;
  }

  const std::lock_guard<std::mutex> lock(mutex_);
  heat.audio = published_.audio;
  heat.audio_ok = published_.audio_ok;
  published_ = heat;
}

void Collector::poll_audio(double dt) {
  const auto raw_audio = audio_source_.sample(dt);
  const double level = raw_audio.has_value()
      ? clamp01(audio_smoother_.update(*raw_audio, dt)) : 0.0;
  const std::lock_guard<std::mutex> lock(mutex_);
  published_.audio = level;
  published_.audio_ok = raw_audio.has_value();
}
