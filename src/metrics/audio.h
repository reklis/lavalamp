#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>

// Native-endian signed 16-bit mono PCM -> RMS, mapped from -60..-6 dBFS
// into [0, 1]. Empty/incomplete frames are unavailable; silence is zero.
std::optional<double> pcm_audio_level(std::string_view pcm);

// Optional playback monitor: pw-record (PipeWire) or parec (PulseAudio).
// Never opens a microphone. A nonblocking pipe keeps audio-server stalls
// off the metrics/render loops. Raw samples are reduced immediately, never
// saved. sample() is called at 20 Hz by the collector, not by the renderer.
class AudioSource {
 public:
  AudioSource() = default;
  ~AudioSource();
  AudioSource(const AudioSource&) = delete;
  AudioSource& operator=(const AudioSource&) = delete;

  std::optional<double> sample(double dt);
  void stop();

 private:
  bool launch();
  int fd_ = -1;
  pid_t child_ = -1;
  double retry_in_ = 0.0;
  bool prefer_pulse_ = false;
  std::string pending_;
  std::optional<double> last_level_;
  double age_ = 0.0;
};
