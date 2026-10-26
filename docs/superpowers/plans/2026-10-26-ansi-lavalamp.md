# ANSI Lava Lamp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A full-screen Linux terminal screensaver in C++ that renders a lava lamp in classic BBS/ANSI art style, driven by live CPU, GPU, network, and thermal activity.

**Architecture:** A metrics thread polls `/proc` and sysfs at 1 Hz and publishes an immutable `Heat` snapshot; the main thread runs a metaball blob simulation with thermal buoyancy at 30+ fps, samples it into a scalar field at double vertical resolution, and converts each cell to a CP437 glyph plus a 256-color fg/bg pair. Pure functions (parsing, color, glyph selection, field sampling) are unit-tested; I/O sits at the edges behind injectable string inputs and an abstract output sink.

**Tech Stack:** C++20, CMake, no third-party dependencies. In-repo minimal test harness. Direct ANSI escape emission (no ncurses).

**Spec:** `docs/superpowers/specs/2026-10-26-ansi-lavalamp-design.md`

## Global Constraints

- **Language/standard:** C++20. **Build:** CMake.
- **Dependencies:** none beyond libc, libstdc++, and pthreads. **No ncurses.** `dlopen` permitted only for the optional NVML path.
- **Platform:** Linux only.
- **Output:** UTF-8, 256-color xterm mode (`\e[38;5;Nm` / `\e[48;5;Nm`). No truecolor.
- **Field resolution:** `cols × (rows × 2)` — two stacked square pixels per cell.
- **Glyph set:** shading `' '`, `░`, `▒`, `▓`, `█`; half-block `▀`. No other glyphs.
- **Minimum terminal size:** 20×10. Below that, print a plain message on the normal screen and exit non-zero.
- **Rates:** metrics poll 1 Hz; render target 30 fps minimum.
- **Smoothing time constants:** CPU ~1 s; temperature ~10 s; GPU moderate (~2 s); network fast attack (~0.2 s) / slow decay (~3 s).
- **Temperature normalization range:** 40 °C → 0.0, 90 °C → 1.0, clamped.
- **Counted network interfaces:** prefixes `en`, `eth`, `wl`, `ww`, `tun`, `ppp`. Everything else excluded — notably `lo`, `docker*`, `br-*`, `veth*`, `virbr*`, `tailscale*`.
- **Never let the renderer feed its own CPU reading:** own jiffies from `/proc/self/stat` are subtracted before normalization.
- **Aesthetic constraints are requirements:** banding and dither texture are protected features. No smoothing them away.

## Review Focus

Failure modes the spec implies but which no task's happy-path tests would exercise. Each has its test placed in the task that owns the code.

1. **First metrics sample has no prior delta.** `/proc/stat` counters are cumulative; a naive first read yields a garbage near-100% reading that slams the lamp to maximum heat at launch. First sample must report unavailable. → Task 8, Task 10.
2. **Counter reset or interface disappearance.** A `veth` vanishing, Docker restarting, or a counter wrapping produces a negative delta. Must clamp to zero, never spike or go negative. → Task 8, Task 10.
3. **Terminal resized smaller mid-run.** A stale cached size means out-of-bounds writes into the field and frame buffers. Resize must land before the next sample, and the renderer must force a full repaint. → Task 6, Task 15.
4. **`nvidia-smi` present but failing.** Driver mismatch, permission denial, or a GPU in a bad state returns `[N/A]`, an error string, or empty output. Must report unavailable rather than parsing garbage into a utilization number. → Task 9.
5. **Nonsensical thermal readings.** Zones report `0`, absent files, or values in degrees rather than millidegrees. Must clamp into range so the palette doesn't sit white-hot at boot. → Task 11.

---

### Task 1: Project skeleton, build, and test harness

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `src/main.cpp`, `tests/test_harness.h`, `tests/test_harness.cpp`, `tests/main.cpp`, `tests/test_smoke.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `TEST(name) { ... }` macro registering a test; `CHECK(cond)`, `CHECK_EQ(a, b)`, `CHECK_NEAR(a, b, eps)` macros; `int run_all_tests(const char* filter)`. CMake targets `lavalamp_core` (static lib of `src/*.cpp` except `main.cpp`), `lavalamp` (executable), `lavalamp_tests` (executable, accepts an optional test-name filter argument).

- [ ] **Step 1: `git init`** — the directory is not yet a repository, and every task below commits.

- [ ] **Step 2: Write the smoke test** in `tests/test_smoke.cpp`

```cpp
TEST(harness_runs) { CHECK_EQ(2 + 2, 4); }
TEST(harness_near) { CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9); }
```

- [ ] **Step 3: Implement the harness and build files**

`test_harness.h` declares the macros above plus `void register_test(const char*, void(*)())`. `TEST(name)` declares a static function, registers it via a static initializer, then opens the function body. Failures record file, line, and the stringified expression, and `run_all_tests` prints each failure and returns non-zero if any failed. `tests/main.cpp` forwards `argv[1]` as the filter.

`CMakeLists.txt`: C++20, the three targets above, `enable_testing()`, and `add_test(NAME unit COMMAND lavalamp_tests)`. `src/main.cpp` is a stub `int main() { return 0; }` for now.

Use **explicit source lists, not globs** — every later task adds its `.cpp` to `lavalamp_core` and its test to `lavalamp_tests`, and a glob silently misses new files until the next configure. At this task `lavalamp_core` has no sources yet, so either declare it `INTERFACE` for now and switch to `STATIC` in Task 2, or give it a one-line `src/version.cpp`; an empty `STATIC` library is a configure error.

- [ ] **Step 4: Build and run**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: configure and build succeed; test `unit` passes with 2 tests.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "build: cmake skeleton and minimal test harness"
```

---

### Task 2: Color space and xterm-256 quantization

**Files:**
- Create: `src/color.h`, `src/color.cpp`, `tests/test_color.cpp`

**Interfaces:**
- Consumes: Task 1 harness.
- Produces:
  - `struct Rgb { std::uint8_t r, g, b; };` with `operator==`
  - `struct Oklab { double L, a, b; };`
  - `Oklab rgb_to_oklab(Rgb)`
  - `Rgb xterm256_to_rgb(std::uint8_t index)`
  - `std::uint8_t quantize_to_xterm256(Rgb)`
  - `Rgb lerp_rgb(Rgb a, Rgb b, double t)` — interpolates **in Oklab**, returns sRGB

- [ ] **Step 1: Write the failing tests** in `tests/test_color.cpp`

```cpp
TEST(xterm_cube_corners) {
  CHECK_EQ(xterm256_to_rgb(16), Rgb{0, 0, 0});        // cube origin
  CHECK_EQ(xterm256_to_rgb(231), Rgb{255, 255, 255}); // cube max
}
TEST(xterm_grayscale_ramp) {
  CHECK_EQ(xterm256_to_rgb(232), Rgb{8, 8, 8});
  CHECK_EQ(xterm256_to_rgb(255), Rgb{238, 238, 238});
}
TEST(quantize_is_inverse_on_palette_colors) {
  // every index in the 6x6x6 cube and gray ramp must round-trip
  for (int i = 16; i <= 255; ++i)
    CHECK_EQ(quantize_to_xterm256(xterm256_to_rgb(i)), i);
}
TEST(quantize_never_returns_system_colors) {
  // indices 0-15 are terminal-theme dependent and must never be chosen
  CHECK(quantize_to_xterm256(Rgb{0, 0, 0}) >= 16);
  CHECK(quantize_to_xterm256(Rgb{255, 0, 0}) >= 16);
}
TEST(lerp_endpoints_exact) {
  CHECK_EQ(lerp_rgb(Rgb{10, 20, 30}, Rgb{200, 100, 50}, 0.0), Rgb{10, 20, 30});
  CHECK_EQ(lerp_rgb(Rgb{10, 20, 30}, Rgb{200, 100, 50}, 1.0), Rgb{200, 100, 50});
}
TEST(lerp_midpoint_is_between) {
  Rgb m = lerp_rgb(Rgb{0, 0, 0}, Rgb{255, 255, 255}, 0.5);
  CHECK(m.r > 100 && m.r < 200);  // perceptual midpoint, not 127
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests color`
Expected: FAIL — `color.h` does not exist / link errors.

- [ ] **Step 3: Implement `src/color.cpp`**

The xterm-256 cube uses levels `{0, 95, 135, 175, 215, 255}` with `index = 16 + 36r + 6g + b`; the grayscale ramp is `index = 232 + n` at `8 + 10n`. Quantization searches indices 16–255 for minimum Oklab distance — a 240-entry linear scan, computed once per palette rebuild, so no acceleration structure is warranted. Use the standard Oklab forward transform (sRGB → linear → LMS → cube root → Oklab) and its inverse for `lerp_rgb`.

Interpolating in Oklab rather than sRGB is what keeps the ramp's perceived brightness even; sRGB interpolation darkens and desaturates through the middle, which would show as a muddy band across every blob.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests color`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add src/color.* tests/test_color.cpp && git commit -m "feat: oklab color space and xterm-256 quantization"
```

---

### Task 3: Temperature-blended palette ramps

**Files:**
- Create: `src/palette.h`, `src/palette.cpp`, `tests/test_palette.cpp`

**Interfaces:**
- Consumes: `Rgb`, `lerp_rgb`, `quantize_to_xterm256` from Task 2.
- Produces:
  - `struct Ramp { std::vector<std::uint8_t> indices; std::size_t size() const; std::uint8_t operator[](std::size_t) const; };`
  - `Ramp build_ramp(double temperature01, std::size_t entries)`
  - `constexpr std::size_t kDefaultRampEntries = 32;`

- [ ] **Step 1: Write the failing tests** in `tests/test_palette.cpp`

```cpp
TEST(ramp_has_requested_length) { CHECK_EQ(build_ramp(0.0, 32).size(), 32u); }
TEST(ramp_entries_are_in_safe_range) {
  for (double t : {0.0, 0.5, 1.0})
    for (std::uint8_t i : build_ramp(t, 32).indices) CHECK(i >= 16);
}
TEST(ramp_brightness_increases_monotonically) {
  // luminance must rise from dark fluid to bright blob core, or blobs
  // will not read as emissive
  Ramp r = build_ramp(0.5, 32);
  double prev = -1.0;
  for (std::uint8_t i : r.indices) {
    double L = rgb_to_oklab(xterm256_to_rgb(i)).L;
    CHECK(L >= prev - 0.02);  // allow quantization jitter, forbid reversal
    prev = L;
  }
}
TEST(hot_ramp_top_is_brighter_than_cool_ramp_top) {
  double cool = rgb_to_oklab(xterm256_to_rgb(build_ramp(0.0, 32).indices.back())).L;
  double hot  = rgb_to_oklab(xterm256_to_rgb(build_ramp(1.0, 32).indices.back())).L;
  CHECK(hot > cool);
}
TEST(temperature_is_clamped) {
  CHECK_EQ(build_ramp(-5.0, 32).indices, build_ramp(0.0, 32).indices);
  CHECK_EQ(build_ramp(99.0, 32).indices, build_ramp(1.0, 32).indices);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests palette`
Expected: FAIL.

- [ ] **Step 3: Implement `src/palette.cpp`**

Two arrays of `Rgb` control points — a cool ramp running near-black → deep purple → magenta → red, and a hot ramp running dark red → orange → yellow → near-white. `build_ramp` interpolates within each control-point array to get `entries` samples, blends the cool and hot sample pairwise by `temperature01`, then quantizes each to an xterm index.

The control point values are yours to choose (spec: "the specific RGB control points for the cool and hot ramps"). Two constraints from the tests: luminance must climb monotonically, and index 0–15 must never appear.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests palette`
Expected: PASS, 5 tests.

- [ ] **Step 5: Commit**

```bash
git add src/palette.* tests/test_palette.cpp && git commit -m "feat: temperature-blended lava palette ramps"
```

---

### Task 4: Glyph and color selection per cell

**Files:**
- Create: `src/glyph.h`, `src/glyph.cpp`, `tests/test_glyph.cpp`

**Interfaces:**
- Consumes: `Ramp` from Task 3.
- Produces:
  - `struct Cell { const char* glyph; std::uint8_t fg; std::uint8_t bg; };` with `operator==`
  - `Cell select_cell(double top, double bottom, const Ramp& ramp)`
  - `constexpr const char* kShades[5] = {" ", "░", "▒", "▓", "█"};`
  - `constexpr const char* kUpperHalf = "▀";`

This is the task that decides whether the project hits or misses its primary goal. The band/dither split described below is the mechanism; implement it exactly.

- [ ] **Step 1: Write the failing tests** in `tests/test_glyph.cpp`

```cpp
TEST(differing_bands_use_half_block) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.0, 1.0, r);
  CHECK_EQ(std::string(c.glyph), "▀");
  CHECK_EQ(c.fg, r[0]);          // fg is the TOP sample
  CHECK_EQ(c.bg, r[r.size()-1]); // bg is the BOTTOM sample
}
TEST(same_band_dithers_between_adjacent_entries) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.5, 0.5, r);
  std::string g(c.glyph);
  CHECK(g != "▀");
  // dither pair must be two ADJACENT ramp entries, never a wider jump
  std::size_t band = 0;
  while (band + 1 < r.size() && r[band] != c.bg) ++band;
  CHECK_EQ(c.fg, r[std::min(band + 1, r.size() - 1)]);
}
TEST(residual_selects_shade_density) {
  Ramp r = build_ramp(0.5, 32);
  // within one band, rising residual must pick denser glyphs in order
  double step = 1.0 / (r.size() - 1);
  std::string low  = select_cell(step * 4 + step * 0.05, step * 4 + step * 0.05, r).glyph;
  std::string mid  = select_cell(step * 4 + step * 0.50, step * 4 + step * 0.50, r).glyph;
  std::string high = select_cell(step * 4 + step * 0.95, step * 4 + step * 0.95, r).glyph;
  CHECK_EQ(low, " ");
  CHECK_EQ(mid, "▒");
  CHECK_EQ(high, "█");
}
TEST(field_values_are_clamped) {
  Ramp r = build_ramp(0.5, 32);
  CHECK_EQ(select_cell(-3.0, -3.0, r), select_cell(0.0, 0.0, r));
  CHECK_EQ(select_cell(7.0, 7.0, r), select_cell(1.0, 1.0, r));
}
TEST(top_band_does_not_overflow_ramp) {
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(1.0, 1.0, r);  // residual lands in the last band
  CHECK(c.fg == r[r.size() - 1] && c.bg == r[r.size() - 1]);
}
TEST(empty_space_is_dark_fluid_not_terminal_black) {
  // spec: the terminal is FILLED with goo. Even zero field must paint a
  // real palette color, never fall through to the terminal default.
  Ramp r = build_ramp(0.5, 32);
  Cell c = select_cell(0.0, 0.0, r);
  CHECK(c.bg >= 16); CHECK(c.fg >= 16);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests glyph`
Expected: FAIL.

- [ ] **Step 3: Implement `select_cell` in `src/glyph.cpp`**

Clamp both samples to `[0, 1]`, scale by `ramp.size() - 1`, split into integer band and fractional residual.

- Bands differ → `▀`, `fg = ramp[band_top]`, `bg = ramp[band_bottom]`. Crisp silhouette at double vertical resolution.
- Bands match → interior cell, spend it on texture. Take the mean residual, pick `kShades[]` by thresholds `0.2 / 0.4 / 0.6 / 0.8`, set `bg = ramp[band]` and `fg = ramp[min(band + 1, size - 1)]`.

The second branch is ordered dithering between neighbouring palette entries. It is simultaneously the project's color-depth extension and its entire BBS texture — do not replace it with a nearest-color lookup, which would produce flat banded blobs with no grain.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests glyph`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add src/glyph.* tests/test_glyph.cpp && git commit -m "feat: half-block and dither glyph selection"
```

---

### Task 5: Field buffer and diffing renderer

**Files:**
- Create: `src/field.h`, `src/field.cpp`, `src/renderer.h`, `src/renderer.cpp`, `tests/test_renderer.cpp`

**Interfaces:**
- Consumes: `Cell`, `select_cell` from Task 4; `Ramp` from Task 3.
- Produces:
  - `class Field { Field(int w, int h); double& at(int x, int y); double at(int x, int y) const; int width() const; int height() const; void resize(int w, int h); void fill(double v); };`
  - `struct Sink { virtual void write(std::string_view) = 0; virtual ~Sink() = default; };`
  - `struct StringSink : Sink { std::string data; void write(std::string_view) override; };`
  - `class Renderer { Renderer(int cols, int rows); void resize(int cols, int rows); void invalidate(); void render(const Field&, const Ramp&, Sink&); };`

- [ ] **Step 1: Write the failing tests** in `tests/test_renderer.cpp`

```cpp
TEST(field_resize_preserves_dimensions) {
  Field f(10, 20); f.resize(5, 8);
  CHECK_EQ(f.width(), 5); CHECK_EQ(f.height(), 8);
}
TEST(first_render_emits_every_cell) {
  Field f(2, 2); f.fill(0.0);           // 2 cols x 2 rows-of-pixels = 1 text row
  Renderer r(2, 1); StringSink s;
  r.render(f, build_ramp(0.5, 32), s);
  CHECK(s.data.find("\x1b[38;5;") != std::string::npos);
  CHECK(s.data.find("\x1b[48;5;") != std::string::npos);
}
TEST(unchanged_second_render_emits_no_cells) {
  Field f(2, 2); f.fill(0.0);
  Renderer r(2, 1); Ramp p = build_ramp(0.5, 32);
  StringSink first, second;
  r.render(f, p, first);
  r.render(f, p, second);
  CHECK(second.data.find("\x1b[38;5;") == std::string::npos);
}
TEST(identical_adjacent_cells_emit_sgr_once) {
  Field f(8, 2); f.fill(0.0);
  Renderer r(8, 1); StringSink s;
  r.render(f, build_ramp(0.5, 32), s);
  // 8 identical cells: exactly one fg and one bg sequence for the run
  CHECK_EQ(count_occurrences(s.data, "\x1b[38;5;"), 1u);
}
TEST(invalidate_forces_full_repaint) {
  Field f(2, 2); f.fill(0.0);
  Renderer r(2, 1); Ramp p = build_ramp(0.5, 32);
  StringSink first, second;
  r.render(f, p, first);
  r.invalidate();
  r.render(f, p, second);
  CHECK(second.data.find("\x1b[38;5;") != std::string::npos);
}
TEST(ramp_change_repaints_even_when_field_is_static) {
  Field f(2, 2); f.fill(0.3);
  Renderer r(2, 1); StringSink first, second;
  r.render(f, build_ramp(0.0, 32), first);
  r.render(f, build_ramp(1.0, 32), second);
  CHECK(second.data.find("\x1b[48;5;") != std::string::npos);
}
```

Add a `count_occurrences(std::string_view haystack, std::string_view needle)` helper to `tests/test_harness.h`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests renderer`
Expected: FAIL.

- [ ] **Step 3: Implement `Field` and `Renderer`**

`Field` is a flat `std::vector<double>` with row-major indexing. `Renderer` keeps a previous-frame `std::vector<Cell>`; `render` walks text rows, calls `select_cell(field.at(x, 2*row), field.at(x, 2*row+1), ramp)`, compares against the previous frame, and appends to one `std::string` that is written to the sink in a single call.

Three emission rules, all pinned by tests above: move the cursor only when the next cell to emit is not immediately after the last one; emit an SGR sequence only when `fg` or `bg` differs from what is currently set; and treat `invalidate()` as clearing the previous-frame cache. The last test exists because a palette change alters output while the field is byte-identical — cache cells, not field values.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests renderer`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add src/field.* src/renderer.* tests/ && git commit -m "feat: scalar field and diffing ANSI renderer"
```

---

### Task 6: Terminal lifecycle and signal handling

**Files:**
- Create: `src/term.h`, `src/term.cpp`, `tests/test_term.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct TermSize { int cols, rows; };`
  - `std::optional<TermSize> query_term_size(int fd)`
  - `bool is_size_usable(TermSize)` — false below 20×10
  - `class TerminalSession { TerminalSession(int fd, bool use_alt_screen); ~TerminalSession(); TerminalSession(const TerminalSession&) = delete; };`
  - `void install_signal_handlers()`
  - `extern volatile std::sig_atomic_t g_should_quit;`
  - `extern volatile std::sig_atomic_t g_resized;`
  - `bool poll_quit_key(int fd)` — non-blocking; true on `q`, `Q`, or `Esc`

- [ ] **Step 1: Write the failing tests** in `tests/test_term.cpp`

```cpp
TEST(minimum_size_is_enforced) {
  CHECK(is_size_usable(TermSize{20, 10}));
  CHECK(is_size_usable(TermSize{200, 60}));
  CHECK(!is_size_usable(TermSize{19, 10}));
  CHECK(!is_size_usable(TermSize{20, 9}));
  CHECK(!is_size_usable(TermSize{0, 0}));
}
TEST(signal_flags_start_clear) {
  CHECK_EQ(g_should_quit, 0);
  CHECK_EQ(g_resized, 0);
}
TEST(sigwinch_sets_resize_flag) {
  install_signal_handlers();
  g_resized = 0;
  raise(SIGWINCH);
  CHECK_EQ(g_resized, 1);   // Review Focus 3: resize must be observable
}
TEST(sigint_sets_quit_flag) {
  install_signal_handlers();
  g_should_quit = 0;
  raise(SIGINT);
  CHECK_EQ(g_should_quit, 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests term`
Expected: FAIL.

- [ ] **Step 3: Implement `src/term.cpp`**

`query_term_size` uses `ioctl(fd, TIOCGWINSZ, …)` and returns `std::nullopt` when it fails or reports zero.

`TerminalSession` is strict RAII: the constructor saves `termios` via `tcgetattr`, disables `ECHO` and `ICANON`, then writes `\e[?1049h` (alternate screen) and `\e[?25l` (hide cursor). The destructor writes `\e[?25h` and `\e[?1049l` and restores the saved `termios`. Install handlers for `SIGINT`, `SIGTERM`, `SIGHUP`, and `SIGWINCH` that only set the two `sig_atomic_t` flags — no I/O, no allocation. The main loop observes the flags and exits through normal stack unwinding so the destructor runs.

Getting this wrong leaves the user's terminal with no cursor and no echo, which is the one failure mode that outlives the process.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests term`
Expected: PASS, 4 tests.

- [ ] **Step 5: Commit**

```bash
git add src/term.* tests/test_term.cpp && git commit -m "feat: terminal session RAII and signal handling"
```

---

### Task 7: Heat snapshot and asymmetric smoothing

**Files:**
- Create: `src/metrics/heat.h`, `src/metrics/heat.cpp`, `tests/test_smoother.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct Heat { double cpu = 0, gpu = 0, net = 0, temp = 0; bool cpu_ok = false, gpu_ok = false, net_ok = false, temp_ok = false; };`
  - `class Smoother { Smoother(double tau_attack, double tau_decay); double update(double target, double dt); double value() const; };`

- [ ] **Step 1: Write the failing tests** in `tests/test_smoother.cpp`

```cpp
TEST(smoother_starts_at_zero) { CHECK_EQ(Smoother(1.0, 1.0).value(), 0.0); }
TEST(smoother_converges_toward_target) {
  Smoother s(1.0, 1.0);
  for (int i = 0; i < 1000; ++i) s.update(1.0, 0.01);
  CHECK_NEAR(s.value(), 1.0, 1e-3);
}
TEST(attack_faster_than_decay_rises_quicker_than_it_falls) {
  Smoother s(0.2, 3.0);
  for (int i = 0; i < 20; ++i) s.update(1.0, 0.01);   // 0.2s rising
  double risen = s.value();
  for (int i = 0; i < 20; ++i) s.update(0.0, 0.01);   // 0.2s falling
  CHECK(s.value() > risen * 0.5);  // decayed far less than it rose
}
TEST(large_dt_does_not_overshoot) {
  Smoother s(1.0, 1.0);
  s.update(1.0, 1e6);
  CHECK(s.value() <= 1.0);  // must not exceed target no matter the step
}
TEST(zero_dt_is_a_noop) {
  Smoother s(1.0, 1.0);
  s.update(1.0, 0.5);
  double before = s.value();
  s.update(1.0, 0.0);
  CHECK_EQ(s.value(), before);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests smoother`
Expected: FAIL.

- [ ] **Step 3: Implement `Smoother`**

Exponential smoothing with the time constant selected per call by whether `target` exceeds the current value: `alpha = 1 - exp(-dt / tau)`, then `value += alpha * (target - value)`. The `exp` form is what makes the last two tests pass — a naive `alpha = dt / tau` overshoots and oscillates on a large `dt`, and the render loop will hand this large steps whenever the process is descheduled.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests smoother`
Expected: PASS, 5 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/heat.* tests/test_smoother.cpp && git commit -m "feat: heat snapshot type and asymmetric smoother"
```

---

### Task 8: CPU metric source

**Files:**
- Create: `src/metrics/cpu.h`, `src/metrics/cpu.cpp`, `tests/test_cpu.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct CpuCounters { std::uint64_t total = 0, idle = 0; };`
  - `std::optional<CpuCounters> parse_proc_stat(std::string_view)`
  - `std::optional<std::uint64_t> parse_self_jiffies(std::string_view)` — `utime + stime`, fields 14 and 15
  - `class CpuSource { std::optional<double> sample(std::string_view proc_stat, std::string_view self_stat); };`

- [ ] **Step 1: Write the failing tests** in `tests/test_cpu.cpp`

Use the real captured line from the target machine as the fixture:

```cpp
static constexpr std::string_view kStat1 =
  "cpu  368470584 1600053 61063225 4797970478 7128777 10130392 7281821 0 132707 0\n"
  "cpu0 19033377 70877 3207921 193493658 350015 381855 1729628 0 8843 0\n";

TEST(parses_aggregate_cpu_line) {
  auto c = parse_proc_stat(kStat1);
  CHECK(c.has_value());
  CHECK_EQ(c->idle, 4797970478ull + 7128777ull);  // idle + iowait
  CHECK(c->total > c->idle);
}
TEST(rejects_garbage) {
  CHECK(!parse_proc_stat("").has_value());
  CHECK(!parse_proc_stat("intr 123 456\n").has_value());
  CHECK(!parse_proc_stat("cpu  1 2\n").has_value());  // too few fields
}
TEST(parses_self_jiffies_from_field_14_and_15) {
  // comm field contains spaces and parens; parser must scan past the last ')'
  std::string_view s = "42 (lava lamp (x)) S 1 42 42 0 -1 4194304 100 0 0 0 7 11 0 0 20 0 1 0 999";
  CHECK_EQ(parse_self_jiffies(s).value(), 18ull);  // utime 7 + stime 11
}
TEST(first_sample_is_unavailable) {           // Review Focus 1
  CpuSource src;
  CHECK(!src.sample(kStat1, "").has_value());
}
TEST(second_sample_reports_busy_fraction) {
  // 100 jiffies elapse, 25 of them idle -> 0.75 busy
  std::string_view s2 = "cpu  368470659 1600053 61063225 4797970503 7128777 10130392 7281821 0 132707 0\n";
  CpuSource src;
  src.sample(kStat1, "");
  auto v = src.sample(s2, "");
  CHECK(v.has_value());
  CHECK_NEAR(*v, 0.75, 0.01);
}
TEST(own_cpu_time_is_subtracted) {
  // identical /proc/stat delta, but self burned most of it -> lower reading
  std::string_view s2 = "cpu  368470684 1600053 61063225 4797970503 7128777 10130392 7281821 0 132707 0\n";
  std::string_view self1 = "1 (x) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 20 0 1 0 0";
  std::string_view self2 = "1 (x) S 0 0 0 0 0 0 0 0 0 0 40 0 0 0 20 0 1 0 0";
  CpuSource a, b;
  a.sample(kStat1, self1); b.sample(kStat1, self1);
  double without = *a.sample(s2, self1);
  double with    = *b.sample(s2, self2);
  CHECK(with < without);
}
TEST(counter_reset_clamps_to_zero) {          // Review Focus 2
  std::string_view reset = "cpu  10 0 0 10 0 0 0 0 0 0\n";
  CpuSource src;
  src.sample(kStat1, "");
  auto v = src.sample(reset, "");
  CHECK(!v.has_value() || (*v >= 0.0 && *v <= 1.0));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests cpu`
Expected: FAIL.

- [ ] **Step 3: Implement `src/metrics/cpu.cpp`**

`parse_proc_stat` reads the first line, requires the `cpu ` prefix and at least 8 numeric fields, sums all fields into `total` and `user+nice+…`-excluded `idle + iowait` into `idle`.

`parse_self_jiffies` must locate the **last** `)` in the line before splitting, because the `comm` field can itself contain spaces and parentheses — the test fixture is built to break a naive `strtok` by whitespace.

`CpuSource::sample` stores the previous counters and previous self-jiffies, returns `std::nullopt` when there is no previous sample, clamps a negative or zero total delta to unavailable, and computes `1 - idle_delta / total_delta` with the self-jiffy delta removed from the busy portion. Result clamped to `[0, 1]`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests cpu`
Expected: PASS, 7 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/cpu.* tests/test_cpu.cpp && git commit -m "feat: cpu metric source with self-load subtraction"
```

---

### Task 9: GPU metric source

**Files:**
- Create: `src/metrics/gpu.h`, `src/metrics/gpu.cpp`, `tests/test_gpu.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `std::optional<double> parse_nvidia_smi(std::string_view)` — returns 0..1
  - `std::optional<double> parse_gpu_busy_percent(std::string_view)` — returns 0..1
  - `std::optional<double> combine_gpu(const std::vector<std::optional<double>>&)` — max of available, `nullopt` if none
  - `std::vector<std::string> discover_amd_busy_paths()` — globs `/sys/class/drm/card*/device/gpu_busy_percent`
  - `class GpuSource { GpuSource(); std::optional<double> sample(); };`

- [ ] **Step 1: Write the failing tests** in `tests/test_gpu.cpp`

Fixtures are the real output shapes from the target machine:

```cpp
TEST(parses_nvidia_smi_csv) {
  CHECK_NEAR(*parse_nvidia_smi("utilization.gpu [%]\n34 %\n"), 0.34, 1e-9);
  CHECK_NEAR(*parse_nvidia_smi("34 %\n"), 0.34, 1e-9);
  CHECK_NEAR(*parse_nvidia_smi("0 %\n"), 0.0, 1e-9);
}
TEST(nvidia_smi_failures_are_unavailable) {   // Review Focus 4
  CHECK(!parse_nvidia_smi("").has_value());
  CHECK(!parse_nvidia_smi("[N/A]\n").has_value());
  CHECK(!parse_nvidia_smi("No devices were found\n").has_value());
  CHECK(!parse_nvidia_smi("Failed to initialize NVML: Driver/library version mismatch\n").has_value());
  CHECK(!parse_nvidia_smi("utilization.gpu [%]\n").has_value());
}
TEST(parses_amd_busy_percent) {
  CHECK_NEAR(*parse_gpu_busy_percent("16\n"), 0.16, 1e-9);
  CHECK_NEAR(*parse_gpu_busy_percent("100"), 1.0, 1e-9);
  CHECK(!parse_gpu_busy_percent("").has_value());
  CHECK(!parse_gpu_busy_percent("N/A\n").has_value());
}
TEST(out_of_range_percentages_are_clamped) {
  CHECK_NEAR(*parse_gpu_busy_percent("250\n"), 1.0, 1e-9);
}
TEST(combine_takes_max_of_available) {
  CHECK_NEAR(*combine_gpu({0.34, 0.16}), 0.34, 1e-9);
  CHECK_NEAR(*combine_gpu({std::nullopt, 0.16}), 0.16, 1e-9);
  CHECK_NEAR(*combine_gpu({0.34, std::nullopt}), 0.34, 1e-9);
}
TEST(combine_with_nothing_available_is_unavailable) {
  CHECK(!combine_gpu({}).has_value());
  CHECK(!combine_gpu({std::nullopt, std::nullopt}).has_value());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests gpu`
Expected: FAIL.

- [ ] **Step 3: Implement `src/metrics/gpu.cpp`**

Both parsers scan for the first line that is a bare integer (optionally followed by `%`), reject anything else, and clamp to `[0, 100]` before dividing. Rejecting non-numeric content is the whole point: `nvidia-smi` communicates every failure as prose on stdout, and a parser that reaches for the first digit it finds will turn "Driver/library version mismatch" into a utilization number.

`GpuSource` probes once at construction — does `nvidia-smi` exist on `PATH`, and which AMD sysfs paths exist — and caches the answer so a machine with no NVIDIA GPU does not pay a failed `popen` every second. `sample()` runs `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader` via `popen` and reads the AMD paths, then returns `combine_gpu`. This runs **only on the metrics thread**; `popen` costs ~100 ms.

The spec permits an NVML-via-`dlopen` fast path as an optional upgrade. Skip it; `popen` at 1 Hz on a background thread is sufficient, and it is the lower-risk dependency story.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests gpu`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/gpu.* tests/test_gpu.cpp && git commit -m "feat: hybrid nvidia/amd gpu metric source"
```

---

### Task 10: Network metric source with adaptive normalization

**Files:**
- Create: `src/metrics/net.h`, `src/metrics/net.cpp`, `tests/test_net.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `bool is_counted_interface(std::string_view name)`
  - `std::optional<std::uint64_t> parse_proc_net_dev(std::string_view)` — summed rx+tx bytes over counted interfaces
  - `class NetSource { std::optional<double> sample(std::string_view proc_net_dev, double dt); };`

- [ ] **Step 1: Write the failing tests** in `tests/test_net.cpp`

The fixture is the verbatim 15-interface `/proc/net/dev` captured from the target machine (two header lines, then `lo`, `wlp99s0`, `tailscale0`, `br-fa87cc1d52f9`, `docker0`, `br-fa5db6fe7c62`, `veth7b64bcd`, `veth921d1b2`, `br-349bfb9a64e1`, `br-92b3d8789e55`, `veth846da1d`, `vethb86b332`, `veth21cc202`, `vetha6ede13`, `tun0`).

```cpp
TEST(interface_whitelist) {
  CHECK(is_counted_interface("wlp99s0"));
  CHECK(is_counted_interface("eth0"));
  CHECK(is_counted_interface("enp5s0"));
  CHECK(is_counted_interface("tun0"));
  CHECK(!is_counted_interface("lo"));
  CHECK(!is_counted_interface("docker0"));
  CHECK(!is_counted_interface("br-fa87cc1d52f9"));
  CHECK(!is_counted_interface("veth7b64bcd"));
  CHECK(!is_counted_interface("tailscale0"));
}
TEST(sums_only_counted_interfaces) {
  // wlp99s0 (711873170029 + 86430704077) + tun0 (670345877 + 1231092305)
  // lo alone is 91572712926 each way and must NOT appear
  CHECK_EQ(parse_proc_net_dev(kNetDev).value(), 800205312288ull);
}
TEST(skips_two_header_lines_and_malformed_rows) {
  CHECK(!parse_proc_net_dev("").has_value());
  CHECK(!parse_proc_net_dev("Inter-|   Receive\n face |bytes\n").has_value());
  CHECK(!parse_proc_net_dev("garbage without colons\n").has_value());
}
TEST(first_sample_is_unavailable) {           // Review Focus 1
  NetSource src;
  CHECK(!src.sample(kNetDev, 1.0).has_value());
}
TEST(idle_traffic_normalizes_near_zero) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  auto v = src.sample(kNetDev, 1.0);  // zero bytes moved
  CHECK(v.has_value()); CHECK_NEAR(*v, 0.0, 1e-6);
}
TEST(burst_then_idle_falls_back_toward_zero) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  double burst = *src.sample(kNetDevPlus100MB, 1.0);
  double after = *src.sample(kNetDevPlus100MB, 1.0);
  CHECK(burst > 0.5);        // a 100 MB/s burst must read high
  CHECK(after < burst);      // and then relax
}
TEST(interface_disappearing_clamps_to_zero) {  // Review Focus 2
  NetSource src;
  src.sample(kNetDev, 1.0);
  auto v = src.sample(kNetDevNoTun, 1.0);  // tun0 gone, total went DOWN
  CHECK(v.has_value()); CHECK(*v >= 0.0);
}
TEST(zero_or_negative_dt_is_unavailable) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  CHECK(!src.sample(kNetDevPlus100MB, 0.0).has_value());
}
```

Build `kNetDevPlus100MB` and `kNetDevNoTun` from `kNetDev` as string literals in the test file.

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests net`
Expected: FAIL.

- [ ] **Step 3: Implement `src/metrics/net.cpp`**

`is_counted_interface` matches the prefix list from Global Constraints. Note that a bare prefix test puts `enp5s0` and `eth0` in but must keep `tailscale0` out even though it is a tun-type device — match on the listed prefixes only, and reject anything containing `-` or starting with `veth`/`docker`/`br`/`virbr`/`lo`/`tailscale`.

`parse_proc_net_dev` skips the two header lines, splits each row at `:`, trims the interface name, and sums field 1 (rx bytes) and field 9 (tx bytes) for counted interfaces. Returns `nullopt` if no counted interface was found at all.

`NetSource::sample` computes `bytes_per_sec = max(0, delta) / dt` — the clamp handles both counter resets and interfaces disappearing — then normalizes **logarithmically against an adapting ceiling**: track an EWMA of observed peak rate, and map `log10(1 + rate) / log10(1 + ceiling)` into `[0, 1]`. The ceiling decays slowly so an old 1 Gb transfer does not permanently flatten the scale.

Per the spec, this means the lamp self-calibrates to the machine's recent habits rather than to absolute bandwidth. That is intended.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests net`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/net.* tests/test_net.cpp && git commit -m "feat: network metric source with adaptive log normalization"
```

---

### Task 11: Thermal metric source

**Files:**
- Create: `src/metrics/thermal.h`, `src/metrics/thermal.cpp`, `tests/test_thermal.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `std::optional<double> parse_thermal_temp(std::string_view)` — millidegrees to °C
  - `double normalize_temp(double celsius)` — 40 → 0.0, 90 → 1.0, clamped
  - `std::vector<std::string> discover_thermal_paths()` — zones whose `type` is `acpitz`, `x86_pkg_temp`, or `k10temp`, else all zones
  - `class ThermalSource { ThermalSource(); std::optional<double> sample(); };`

- [ ] **Step 1: Write the failing tests** in `tests/test_thermal.cpp`

```cpp
TEST(parses_millidegrees) {
  CHECK_NEAR(*parse_thermal_temp("71000\n"), 71.0, 1e-9);  // the real reading
  CHECK_NEAR(*parse_thermal_temp("45500"), 45.5, 1e-9);
}
TEST(rejects_nonsense) {                      // Review Focus 5
  CHECK(!parse_thermal_temp("").has_value());
  CHECK(!parse_thermal_temp("N/A\n").has_value());
  CHECK(!parse_thermal_temp("0\n").has_value());        // 0 degrees is not real
  CHECK(!parse_thermal_temp("-40000\n").has_value());
  CHECK(!parse_thermal_temp("500000\n").has_value());   // 500 C is not real
}
TEST(normalizes_to_the_specified_range) {
  CHECK_NEAR(normalize_temp(40.0), 0.0, 1e-9);
  CHECK_NEAR(normalize_temp(90.0), 1.0, 1e-9);
  CHECK_NEAR(normalize_temp(65.0), 0.5, 1e-9);
}
TEST(normalization_clamps_outside_the_range) {
  CHECK_EQ(normalize_temp(5.0), 0.0);
  CHECK_EQ(normalize_temp(150.0), 1.0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests thermal`
Expected: FAIL.

- [ ] **Step 3: Implement `src/metrics/thermal.cpp`**

`parse_thermal_temp` parses an integer and accepts only the plausible window 1 °C–150 °C, returning `nullopt` otherwise. The zero rejection matters in practice: unpopulated zones report `0`, and without the check the palette would sit at its cool extreme while the machine cooks — or, with a different normalization, go white-hot at boot.

`ThermalSource` reads the discovered zones each sample and takes the maximum valid reading, then `normalize_temp`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests thermal`
Expected: PASS, 4 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/thermal.* tests/test_thermal.cpp && git commit -m "feat: thermal metric source"
```

---

### Task 12: Metrics collector thread

**Files:**
- Create: `src/metrics/collector.h`, `src/metrics/collector.cpp`, `tests/test_collector.cpp`
- Modify: `CMakeLists.txt` — link `Threads::Threads`

**Interfaces:**
- Consumes: `Heat`, `Smoother` (Task 7); `CpuSource` (8); `GpuSource` (9); `NetSource` (10); `ThermalSource` (11).
- Produces:
  - `std::optional<std::string> read_file(const std::string& path)`
  - `class Collector { Collector(); ~Collector(); void start(); void stop(); Heat snapshot() const; };`

- [ ] **Step 1: Write the failing tests** in `tests/test_collector.cpp`

```cpp
TEST(snapshot_before_start_is_all_unavailable) {
  Collector c;
  Heat h = c.snapshot();
  CHECK(!h.cpu_ok && !h.gpu_ok && !h.net_ok && !h.temp_ok);
  CHECK_EQ(h.cpu, 0.0);
}
TEST(snapshot_values_are_always_in_range) {
  Collector c; c.start();
  for (int i = 0; i < 50; ++i) {
    Heat h = c.snapshot();   // hammered concurrently with the poll thread
    CHECK(h.cpu >= 0.0 && h.cpu <= 1.0);
    CHECK(h.gpu >= 0.0 && h.gpu <= 1.0);
    CHECK(h.net >= 0.0 && h.net <= 1.0);
    CHECK(h.temp >= 0.0 && h.temp <= 1.0);
  }
  c.stop();
}
TEST(stop_is_idempotent_and_start_stop_does_not_hang) {
  Collector c; c.start(); c.stop(); c.stop();
}
TEST(destructor_stops_a_running_thread) {
  { Collector c; c.start(); }   // must not terminate or leak the thread
}
TEST(read_file_on_missing_path_is_unavailable) {
  CHECK(!read_file("/proc/definitely-not-a-real-file").has_value());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests collector`
Expected: FAIL.

- [ ] **Step 3: Implement `src/metrics/collector.cpp`**

`start()` spawns one `std::thread` that loops until a stop flag: read `/proc/stat`, `/proc/self/stat`, `/proc/net/dev`, poll GPU and thermal sources, push each raw value through its `Smoother` with the time constants from Global Constraints, assemble a `Heat`, and publish it under a mutex. Then wait up to 1 s on a condition variable so `stop()` is immediate rather than blocking for a poll interval.

`snapshot()` copies the published `Heat` under the same mutex — it is called every frame from the render thread, so it must never do I/O or block on anything but the lock. The destructor calls `stop()`; a `std::thread` destroyed while joinable calls `std::terminate`, which is what test 4 catches.

Values are clamped to `[0, 1]` at publication, so the renderer can trust the range unconditionally.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests collector`
Expected: PASS, 5 tests.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/collector.* tests/test_collector.cpp CMakeLists.txt && git commit -m "feat: background metrics collector thread"
```

---

### Task 13: Noise field

**Files:**
- Create: `src/vec2.h`, `src/noise.h`, `src/noise.cpp`, `tests/test_noise.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct Vec2 { double x = 0, y = 0; };` with `+`, `-`, `*` (scalar), `+=`, and `double length() const`
  - `double value_noise(double x, double y, double z)` — returns `[-1, 1]`
  - `Vec2 curl_noise(double x, double y, double t)` — divergence-free, unit-ish magnitude

- [ ] **Step 1: Write the failing tests** in `tests/test_noise.cpp`

```cpp
TEST(noise_is_in_range) {
  for (int i = 0; i < 2000; ++i) {
    double v = value_noise(i * 0.37, i * 0.11, i * 0.013);
    CHECK(v >= -1.0 && v <= 1.0);
  }
}
TEST(noise_is_deterministic) {
  CHECK_EQ(value_noise(1.5, 2.5, 3.5), value_noise(1.5, 2.5, 3.5));
}
TEST(noise_is_continuous) {
  // adjacent samples must not jump; a hash-only implementation fails this
  double a = value_noise(4.0, 4.0, 0.0);
  double b = value_noise(4.001, 4.0, 0.0);
  CHECK_NEAR(a, b, 0.05);
}
TEST(noise_actually_varies) {
  CHECK(std::abs(value_noise(0.5, 0.5, 0.0) - value_noise(9.5, 17.5, 0.0)) > 1e-6);
}
TEST(curl_noise_is_bounded) {
  for (int i = 0; i < 500; ++i) CHECK(curl_noise(i * 0.3, i * 0.7, 0.0).length() < 10.0);
}
TEST(curl_noise_is_approximately_divergence_free) {
  // the whole reason to use curl noise: it swirls without compressing
  double h = 1e-3, x = 3.3, y = 2.1;
  double dudx = (curl_noise(x + h, y, 0).x - curl_noise(x - h, y, 0).x) / (2 * h);
  double dvdy = (curl_noise(x, y + h, 0).y - curl_noise(x, y - h, 0).y) / (2 * h);
  CHECK_NEAR(dudx + dvdy, 0.0, 1e-3);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests noise`
Expected: FAIL.

- [ ] **Step 3: Implement `src/noise.cpp`**

`value_noise` is trilinear interpolation of an integer-lattice hash with a smoothstep fade — the continuity test rejects a raw hash. Use a fixed integer hash (no `rand`, no global state) so the function stays pure and the determinism test holds.

`curl_noise` takes the 2D curl of a scalar potential `ψ = value_noise(x, y, t)`: `(∂ψ/∂y, −∂ψ/∂x)` by central differences. Divergence-free is the property that makes the goo swirl and fold instead of piling up in sinks, which is what separates organic motion from objects drifting on rails.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests noise`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add src/vec2.h src/noise.* tests/test_noise.cpp && git commit -m "feat: value and curl noise fields"
```

---

### Task 14: Blob simulation

**Files:**
- Create: `src/sim.h`, `src/sim.cpp`, `tests/test_sim.cpp`

**Interfaces:**
- Consumes: `Vec2`, `curl_noise` (Task 13); `Heat` (Task 7); `Field` (Task 5).
- Produces:
  - `struct Blob { Vec2 pos, vel; double radius = 1.0, temp = 0.0; };`
  - `class LavaSim { LavaSim(double width, double height, std::uint32_t seed); void resize(double width, double height); void set_heat(const Heat&); void step(double dt); void sample_into(Field&) const; const std::vector<Blob>& blobs() const; };`

- [ ] **Step 1: Write the failing tests** in `tests/test_sim.cpp`

```cpp
TEST(starts_seeded_with_blobs) {
  LavaSim s(80, 50, 1234);
  CHECK(s.blobs().size() >= 4);   // frame one must already look like a lamp
}
TEST(seed_is_deterministic) {
  LavaSim a(80, 50, 7), b(80, 50, 7);
  a.step(0.1); b.step(0.1);
  CHECK_EQ(a.blobs().size(), b.blobs().size());
  CHECK_NEAR(a.blobs()[0].pos.x, b.blobs()[0].pos.x, 1e-12);
}
TEST(blobs_stay_inside_the_lamp) {
  LavaSim s(80, 50, 3);
  Heat h; h.cpu = 1.0; h.cpu_ok = true; h.gpu = 1.0; h.gpu_ok = true;
  s.set_heat(h);
  for (int i = 0; i < 5000; ++i) s.step(1.0 / 60);
  for (const Blob& b : s.blobs()) {
    CHECK(b.pos.x >= 0 && b.pos.x <= 80);
    CHECK(b.pos.y >= 0 && b.pos.y <= 50);
    CHECK(std::isfinite(b.vel.x) && std::isfinite(b.vel.y));
  }
}
TEST(hot_blob_at_the_floor_rises) {
  LavaSim s(80, 50, 11);
  // convection sign check: buoyancy must push heat upward
  double before = mean_height_of_hottest_blob(s);
  for (int i = 0; i < 600; ++i) s.step(1.0 / 60);
  CHECK(mean_height_of_hottest_blob(s) != before);
}
TEST(high_cpu_produces_more_motion_than_idle) {
  // goal 2: busy must be visibly different from idle
  Heat idle; idle.cpu = 0.0; idle.cpu_ok = true;
  Heat busy; busy.cpu = 1.0; busy.cpu_ok = true;
  CHECK(total_speed_after(LavaSim(80, 50, 5), busy, 300) >
        total_speed_after(LavaSim(80, 50, 5), idle, 300) * 1.5);
}
TEST(merging_conserves_volume) {
  // two overlapping equal blobs -> one blob of combined area
  LavaSim s(80, 50, 1);
  double area_before = total_blob_area(s);
  for (int i = 0; i < 2000; ++i) s.step(1.0 / 60);
  CHECK_NEAR(total_blob_area(s), area_before, area_before * 0.35);
}
TEST(dt_is_clamped_against_hitches) {
  LavaSim s(80, 50, 2);
  s.step(1000.0);   // a descheduled process hands us an enormous dt
  for (const Blob& b : s.blobs()) CHECK(std::isfinite(b.pos.x) && b.pos.x >= 0);
}
TEST(sample_into_fills_the_field_in_range) {
  LavaSim s(80, 50, 9);
  Field f(80, 50);
  s.sample_into(f);
  for (int y = 0; y < 50; ++y)
    for (int x = 0; x < 80; ++x) CHECK(f.at(x, y) >= 0.0 && f.at(x, y) <= 1.0);
}
TEST(sample_into_is_brighter_inside_a_blob_than_far_from_one) {
  // the field must actually describe blobs, not a uniform wash
  LavaSim s(80, 50, 4);
  Field f(80, 50);
  s.sample_into(f);
  CHECK(max_field_value(f) > min_field_value(f) + 0.2);
}
TEST(network_bursts_inject_blobs_at_the_floor) {
  LavaSim s(80, 50, 8);
  std::size_t before = s.blobs().size();
  Heat h; h.net = 1.0; h.net_ok = true;
  s.set_heat(h);
  for (int i = 0; i < 120; ++i) s.step(1.0 / 60);
  CHECK(s.blobs().size() > before);  // impulses spawned droplets
}
TEST(no_network_traffic_injects_nothing) {
  LavaSim a(80, 50, 8);
  Heat quiet; quiet.net = 0.0; quiet.net_ok = true;
  a.set_heat(quiet);
  std::size_t before = a.blobs().size();
  for (int i = 0; i < 120; ++i) a.step(1.0 / 60);
  CHECK(a.blobs().size() <= before + 1);  // only convection, no jets
}
TEST(resize_keeps_blobs_in_bounds) {          // Review Focus 3
  LavaSim s(200, 100, 6);
  for (int i = 0; i < 300; ++i) s.step(1.0 / 60);
  s.resize(30, 12);
  s.step(1.0 / 60);
  for (const Blob& b : s.blobs()) CHECK(b.pos.x <= 30 && b.pos.y <= 12);
}
```

Put the small helpers (`total_blob_area`, `total_speed_after`, `max_field_value`, `min_field_value`, `mean_height_of_hottest_blob`) at the top of the test file.

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/lavalamp_tests sim`
Expected: FAIL.

- [ ] **Step 3: Implement `src/sim.cpp`**

Constructor seeds blobs at randomized positions with randomized temperatures (spec: frame one must look like a lamp mid-cycle, not a cold uniform state). Use a seeded `std::mt19937` member, never global `rand`, so the determinism test holds.

`step(dt)` clamps `dt` to a maximum (~1/15 s) and then, per blob:

1. Heat by proximity to the floor; cool by proximity to the ceiling.
2. Apply buoyancy upward proportional to `temp` above ambient — this is the sign that makes the convection cycle emergent. Nothing scripts the turnaround.
3. Add `curl_noise(pos.x * scale, pos.y * scale, time) * turbulence`, where turbulence comes from `Heat::gpu`.
4. Apply drag; integrate; apply soft wall containment.

Then resolve merges (overlapping pairs combine with `r = sqrt(r₁² + r₂²)`, mass-weighted velocity and temperature) and splits (oversized or high-shear blobs divide, conserving area).

`Heat` maps per the spec: `cpu` → buoyancy strength and target blob count; `gpu` → turbulence amplitude; `net` → impulse events spawning fast small blobs at the floor; `temp` is **not** used here — it drives the palette in Task 15. An unavailable channel contributes its neutral value, so the lamp stays alive with any subset present.

`sample_into` evaluates the metaball sum per pixel with per-blob bounding-box culling, mapping lamp coordinates to field coordinates, and normalizes into `[0, 1]`. Culling is what keeps this affordable: without it the cost is pixels × blobs every frame, and the "must not burn a core" anti-goal is a correctness requirement here, not a nicety.

**Numeric constants are yours** (spec: blob counts, radii, buoyancy and drag coefficients, heating/cooling rates, merge and split thresholds, noise scale). The tests pin the behavior, not the values.

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/lavalamp_tests sim`
Expected: PASS, 12 tests.

- [ ] **Step 5: Commit**

```bash
git add src/sim.* tests/test_sim.cpp && git commit -m "feat: thermal buoyancy blob simulation"
```

---

### Task 15: Main loop, wiring, and tuning pass

**Files:**
- Modify: `src/main.cpp`
- Create: `README.md`

**Interfaces:**
- Consumes: everything from Tasks 2–14.
- Produces: the `lavalamp` binary.

- [ ] **Step 1: Write the failing test** in `tests/test_term.cpp`

```cpp
TEST(tiny_terminal_is_rejected_before_any_rendering) {
  // Review Focus 3, lower bound: must refuse rather than corrupt the screen
  CHECK(!is_size_usable(query_term_size_or(TermSize{10, 4})));
}
```

Add `TermSize query_term_size_or(TermSize fallback)` to `src/term.h` so the startup size path is testable without a tty.

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/lavalamp_tests term`
Expected: FAIL — `query_term_size_or` not declared.

- [ ] **Step 3: Implement `src/main.cpp`**

Startup: parse the two debug-only flags (`--fps N`, `--no-alt-screen`), query terminal size, bail with a plain message on the normal screen if `!is_size_usable`, construct `TerminalSession`, `install_signal_handlers()`, start the `Collector`, and build `LavaSim` sized `cols × (rows * 2)`.

Frame loop, until `g_should_quit` or `poll_quit_key`:

1. If `g_resized`, clear the flag, re-query size, bail or `Field::resize` + `Renderer::resize` + `Renderer::invalidate()` + `LavaSim::resize`. **Do this before anything touches the buffers** — a stale size here is an out-of-bounds write.
2. `Heat h = collector.snapshot()`.
3. `sim.set_heat(h)`, `sim.step(dt)`, `sim.sample_into(field)`.
4. Rebuild the `Ramp` from `h.temp` only when it has moved more than ~0.02 since the last rebuild; otherwise reuse the cached ramp.
5. `renderer.render(field, ramp, stdout_sink)`.
6. Sleep to pace to the target framerate, measuring real elapsed time for the next `dt`.

The `TerminalSession` destructor restores the terminal on every exit path, including signals, because the loop exits by flag rather than by `_exit`.

`README.md`: what it is, how to build, how to run, how to quit, and the Linux-only requirement.

- [ ] **Step 4: Verify**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all tests pass.

Run: `./build/lavalamp` in a real terminal.
Expected: full-screen animated goo; `q` exits with the terminal intact — cursor visible, echo working, scrollback unharmed.

- [ ] **Step 5: Tuning pass — the real work of this task**

Run it, watch it, and adjust the constants from Tasks 3 and 14 until it meets the spec's goals and avoids its anti-goals. Check each explicitly:

- Does it read as **BBS ANSI art** — visible banding, chunky dither grain — rather than smooth gradient mush? (anti-goal 1)
- Is **idle visibly different from loaded**? Compare at rest against `yes > /dev/null` on several cores, a GPU load, and a large download. (anti-goal 2)
- Are **merge and split clearly visible** at idle? That's the lava lamp signature.
- Measure its **own CPU usage** with `top`. If it's burning a core, fix that before anything else — it's an anti-goal and it poisons its own input. (anti-goal 3)
- Confirm it still runs and looks good with the GPU source forced unavailable. (anti-goal 4)
- Resize the terminal while it runs, including smaller; confirm no corruption.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: main render loop and tuning pass"
```
