#include "metrics/audio.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

std::optional<double> pcm_audio_level(std::string_view pcm) {
  if (pcm.empty() || pcm.size() % sizeof(std::int16_t) != 0) return std::nullopt;
  double squares = 0.0;
  const std::size_t count = pcm.size() / sizeof(std::int16_t);
  for (std::size_t i = 0; i < count; ++i) {
    std::int16_t sample;
    std::memcpy(&sample, pcm.data() + i * sizeof(sample), sizeof(sample));
    const double normalized = static_cast<double>(sample) / 32768.0;
    squares += normalized * normalized;
  }
  const double rms = std::sqrt(squares / static_cast<double>(count));
  if (rms <= 0.001) return 0.0;
  return std::clamp((20.0 * std::log10(rms) + 60.0) / 54.0, 0.0, 1.0);
}

AudioSource::~AudioSource() { stop(); }

bool AudioSource::launch() {
  // Explicit sink capture is important: the default recording device is
  // often a microphone. Neither backend may fall back to that default.
  const char* pipewire[] = {"pw-record", "--raw", "--rate=8000", "--channels=1",
                            "--format=s16", "--latency=50ms", "--properties",
                            "stream.capture.sink=true", "-", nullptr};
  const char* pulse[] = {"parec", "--device=@DEFAULT_MONITOR@", "--raw",
                         "--format=s16ne", "--rate=8000", "--channels=1", nullptr};
  for (int attempt = 0; attempt < 2; ++attempt) {
    int pipes[2];
    if (::pipe2(pipes, O_CLOEXEC) != 0) return false;
    const int flags = ::fcntl(pipes[0], F_GETFL);
    if (flags < 0 || ::fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK) < 0) {
      ::close(pipes[0]);
      ::close(pipes[1]);
      return false;
    }
    posix_spawn_file_actions_t actions;
    int error = ::posix_spawn_file_actions_init(&actions);
    if (error == 0) {
      error = ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
      if (!error) error = ::posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
      if (!error) error = ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
      if (!error) error = ::posix_spawn_file_actions_addclose(&actions, pipes[0]);
      if (!error) error = ::posix_spawn_file_actions_addclose(&actions, pipes[1]);
      const char** args = prefer_pulse_ ? pulse : pipewire;
      if (!error) error = ::posix_spawnp(&child_, args[0], &actions, nullptr,
                                        const_cast<char**>(args), environ);
      ::posix_spawn_file_actions_destroy(&actions);
    }
    ::close(pipes[1]);
    if (!error) {
      fd_ = pipes[0];
      return true;
    }
    child_ = -1;
    ::close(pipes[0]);
    prefer_pulse_ = !prefer_pulse_;
  }
  return false;
}

void AudioSource::stop() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  if (child_ > 0) {
    // Only our owned helper is killed. SIGKILL bounds shutdown even if a
    // disconnected audio server leaves it stuck; always reap the child.
    ::kill(child_, SIGKILL);
    while (::waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
  }
  child_ = -1;
  pending_.clear();
  last_level_.reset();
  age_ = 0.0;
}

std::optional<double> AudioSource::sample(double dt) {
  if (fd_ < 0) {
    retry_in_ = std::max(0.0, retry_in_ - std::max(dt, 0.0));
    if (retry_in_ > 0.0) return std::nullopt;
    retry_in_ = 5.0;
    if (!launch()) return std::nullopt;
  }

  std::string pcm = std::move(pending_);
  pending_.clear();
  char buffer[4096];
  // Bounded work even if a broken helper floods stdout. Normally this drains
  // only ~800 bytes per tick (8 kHz mono); enough headroom for slow GPU polls.
  for (int i = 0; i < 32; ++i) {
    const ssize_t count = ::read(fd_, buffer, sizeof buffer);
    if (count > 0) {
      pcm.append(buffer, static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    stop();
    prefer_pulse_ = !prefer_pulse_;
    retry_in_ = 5.0;
    return std::nullopt;
  }
  if (pcm.size() % 2 != 0) {
    pending_.assign(1, pcm.back());
    pcm.pop_back();
  }
  if (const auto level = pcm_audio_level(pcm)) {
    last_level_ = level;
    age_ = 0.0;
  } else {
    // A helper may batch adjacent ticks. Keep the previous reading briefly,
    // but do not treat a stalled server as sustained audio indefinitely.
    age_ += std::max(dt, 0.0);
    if (age_ > 0.25) last_level_.reset();
  }
  return last_level_;
}
