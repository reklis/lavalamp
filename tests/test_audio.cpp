#include "metrics/audio.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "test_harness.h"

namespace {
std::string pcm(std::initializer_list<std::int16_t> samples) {
  return std::string(reinterpret_cast<const char*>(samples.begin()), samples.size() * 2);
}

// An isolated PATH means these tests never connect to the real audio server.
// The fake helper blocks opening a FIFO after emitting PCM: no CPU spin,
// sleeps, or grandchildren to leak when AudioSource terminates it.
class FakeAudio {
 public:
  FakeAudio() {
    if (const char* path = std::getenv("PATH")) old_path_ = path;
    char pattern[] = "/tmp/lavalamp-audio-test-XXXXXX";
    if (char* path = ::mkdtemp(pattern)) directory = path;
    if (!directory.empty()) ::setenv("PATH", directory.c_str(), 1);
  }
  ~FakeAudio() {
    if (old_path_) ::setenv("PATH", old_path_->c_str(), 1);
    else ::unsetenv("PATH");
    if (!directory.empty()) {
      ::unlink((directory + "/pw-record").c_str());
      ::unlink((directory + "/parec").c_str());
      ::unlink((directory + "/block").c_str());
      ::rmdir(directory.c_str());
    }
  }
  bool helper(const char* name, const char* guard) {
    const std::string path = directory + "/" + name;
    std::ofstream script(path);
    script << "#!/bin/sh\ncase \"$*\" in\n*" << guard << "*) ;;\n*) exit 1 ;;\nesac\n"
              "printf '\\000\\100\\000\\100'\n"
              "read x < '" << directory << "/block'\n";
    script.close();
    return script.good() && ::chmod(path.c_str(), 0700) == 0;
  }
  std::string directory;
 private:
  std::optional<std::string> old_path_;
};
}  // namespace

TEST(audio_pcm_distinguishes_unavailable_silence_and_full_scale) {
  CHECK(!pcm_audio_level("").has_value());
  CHECK(!pcm_audio_level(std::string(1, '\0')).has_value());
  CHECK_EQ(pcm_audio_level(pcm({0, 0, 0, 0})), 0.0);
  CHECK_EQ(pcm_audio_level(pcm({-32768, 32767})), 1.0);
  CHECK_EQ(pcm_audio_level(pcm({1, -1})), 0.0);  // below noise floor
}

TEST(audio_pcm_rms_is_polarity_independent_and_log_scaled) {
  const auto positive = pcm_audio_level(pcm({3277, 3277}));
  const auto alternating = pcm_audio_level(pcm({-3277, 3277}));
  CHECK(positive.has_value() && alternating.has_value());
  if (positive && alternating) {
    CHECK_NEAR(*positive, 40.0 / 54.0, 0.001);  // -20 dBFS
    CHECK_NEAR(*positive, *alternating, 1e-12);
  }
  CHECK(*pcm_audio_level(pcm({100, -100})) < *positive);
}

TEST(audio_missing_helpers_is_unavailable_and_stop_is_idempotent) {
  FakeAudio fake;
  CHECK(!fake.directory.empty());
  if (fake.directory.empty()) return;
  AudioSource source;
  CHECK(!source.sample(0.05).has_value());
  source.stop();
  source.stop();
}

TEST(audio_monitor_helpers_are_nonblocking_expire_stale_levels_and_stop) {
  for (const char* name : {"pw-record", "parec"}) {
    FakeAudio fake;
    CHECK(!fake.directory.empty());
    if (fake.directory.empty()) return;
    CHECK_EQ(::mkfifo((fake.directory + "/block").c_str(), 0600), 0);
    CHECK(fake.helper(name, std::string(name) == "pw-record"
                              ? "stream.capture.sink=true" : "--device=@DEFAULT_MONITOR@"));
    AudioSource source;
    std::optional<double> level;
    for (int i = 0; i < 100 && !level; ++i) {
      level = source.sample(0.01);
      if (!level) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(level.has_value());
    if (level) CHECK(*level > 0.99);
    CHECK(!source.sample(0.5).has_value());  // stalled stream is not loud forever
    const auto start = std::chrono::steady_clock::now();
    source.stop();
    source.stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
  }
}
