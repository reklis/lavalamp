#include <chrono>
#include <exception>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <unistd.h>

#include "field.h"
#include "metrics/collector.h"
#include "palette.h"
#include "renderer.h"
#include "sim.h"
#include "term.h"

namespace {

constexpr int kDefaultFps = 30;

// Rebuilding the palette means ~32 nearest-colour searches over 240 entries,
// so it is cached and only recomputed when the temperature has actually moved.
constexpr double kRampRebuildThreshold = 0.02;

// What each channel contributes when the machine does not expose it. Not zero
// for temperature: a box with no readable thermal zone should sit at a
// pleasant mid-palette, not at the coldest extreme forever.
constexpr double kNeutralTemp = 0.35;

struct Options {
  int fps = kDefaultFps;
  bool alt_screen = true;
  TermSize forced_size{0, 0};  // zero means "ask the terminal"
};

// Debug-only flags, for development and tuning. Not user-facing
// configuration -- the spec explicitly has none.
Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--no-alt-screen") {
      options.alt_screen = false;
    } else if (argument == "--fps" && i + 1 < argc) {
      const int value = std::atoi(argv[++i]);
      if (value > 0 && value <= 240) options.fps = value;
    } else if (argument == "--size" && i + 1 < argc) {
      int cols = 0;
      int rows = 0;
      if (std::sscanf(argv[++i], "%dx%d", &cols, &rows) == 2) {
        options.forced_size = TermSize{cols, rows};
      }
    }
  }
  return options;
}

}  // namespace

namespace {

int run_lamp(const Options& options) {

  TermSize size = options.forced_size.cols > 0
                      ? options.forced_size
                      : query_term_size_or(STDOUT_FILENO, TermSize{80, 24});
  if (!is_size_usable(size)) {
    // Refused on the normal screen, before the alternate screen is entered,
    // so the message survives for the user to read.
    std::fprintf(stderr, "lavalamp: terminal too small (%dx%d; needs at least 20x10)\n",
                 size.cols, size.rows);
    return 1;
  }

  install_signal_handlers();

  bool shrank_too_small = false;

  {
    TerminalSession session(STDOUT_FILENO, options.alt_screen);
    FdSink sink(STDOUT_FILENO);

    Collector collector;
    collector.start();

    Field field(size.cols, size.rows * 2);
    Renderer renderer(size.cols, size.rows);
    LavaSim sim(size.cols, size.rows * 2, make_seed());

    Ramp ramp = build_ramp(kNeutralTemp, kDefaultRampEntries);
    double ramp_temp = kNeutralTemp;

    const auto frame_budget = std::chrono::duration<double>(1.0 / options.fps);
    auto previous = std::chrono::steady_clock::now();

    while (g_should_quit == 0) {
      const auto frame_start = std::chrono::steady_clock::now();

      // Resize is handled before anything touches the buffers: a stale size
      // here would mean indexing past the end of the field.
      if (g_resized != 0) {
        g_resized = 0;
        if (const auto resized = query_term_size(STDOUT_FILENO)) {
          if (!is_size_usable(*resized)) {
            shrank_too_small = true;
            break;
          }
          size = *resized;
          field.resize(size.cols, size.rows * 2);
          renderer.resize(size.cols, size.rows);
          sim.resize(size.cols, size.rows * 2);
          renderer.invalidate();
        }
      }

      if (poll_quit_key(STDIN_FILENO)) break;

      const Heat heat = collector.snapshot();

      const double dt = std::chrono::duration<double>(frame_start - previous).count();
      previous = frame_start;

      sim.set_heat(heat);
      sim.step(dt);
      sim.sample_into(field);

      const double temp = heat.temp_ok ? heat.temp : kNeutralTemp;
      if (std::abs(temp - ramp_temp) > kRampRebuildThreshold) {
        ramp = build_ramp(temp, kDefaultRampEntries);
        ramp_temp = temp;
      }

      renderer.render(field, ramp, sink);

      const auto elapsed = std::chrono::steady_clock::now() - frame_start;
      if (elapsed < frame_budget) std::this_thread::sleep_for(frame_budget - elapsed);
    }

    collector.stop();
  }  // TerminalSession restores the terminal here, on every exit path.

  if (shrank_too_small) {
    std::fprintf(stderr, "lavalamp: terminal shrank below 20x10\n");
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // Defence in depth for the same reason make_seed exists: anything that
  // escapes run_lamp would reach std::terminate without unwinding, skipping
  // the TerminalSession destructor and wrecking the user's terminal. Catching
  // here means the stack unwinds first and the terminal is restored.
  try {
    return run_lamp(parse_options(argc, argv));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "lavalamp: %s\n", error.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "lavalamp: unknown error\n");
    return 1;
  }
}
