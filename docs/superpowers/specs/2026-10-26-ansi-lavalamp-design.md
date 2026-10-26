# ANSI Lava Lamp — Design

**Date:** 2026-10-26
**Status:** approved design, ready to plan

## Intent

A full-screen terminal screensaver that renders a living lava lamp in the visual
language of classic BBS/ANSI artwork, where the lamp's heat comes from the real
activity of the machine it's running on.

You run it, you stare at it, it looks cool. When the box is idle the goo is slow
and fat. When you kick off a build, the lamp gets violent. The result is an
ambient system monitor wearing a lava lamp costume — but the costume is the
point, not the telemetry.

### Why this, why now

For fun. That is the whole business case, and it has a real design consequence:
**every tradeoff resolves toward "looks sick"**, not toward accuracy,
configurability, or portability. A metric mapping that reads beautifully but
distorts the underlying number is correct. A faithful readout that looks like a
gradient wash is wrong.

## Goals

1. **Reads as BBS ANSI art.** A person who spent time on 1993-era boards should
   recognize the idiom: CP437 block and shading glyphs, visible banding, chunky
   dithered texture, a deliberate palette. Not a smooth modern shader.
2. **Visibly alive and visibly *responsive*.** Watching it during a kernel build
   should look obviously different from watching it at idle — different enough
   that you notice without being told to look.
3. **Full-bleed goo.** The terminal is the inside of the lamp. No lamp
   furniture, no base, no cap, no border, no chrome, no stats overlay.
4. **Runs from a single binary with no arguments** and looks good immediately.
5. **Cheap enough to leave running.** See anti-goals — this one has teeth.

## Non-goals

- **Interactivity.** No controls, no mouse, no keybindings that change the art.
  Decided explicitly by the user: "nothing is interactive, it just looks cool."
- **Cross-platform.** Linux only. Every metric source here is Linux-specific
  plumbing, and supporting macOS/Windows would mean abstracting all of it for no
  gain to the user.
- **Truecolor.** 256-color xterm mode, decided by the user. The quantization is
  doing aesthetic work.
- **Physically accurate fluid dynamics.** Not a Navier-Stokes solver. We want
  the *look* of convecting immiscible blobs.
- **Configuration.** No config file, no theme system, no tunables surfaced to
  the user.

## Anti-goals

These make the project a failure even if it runs correctly:

- **Gradient mush.** If it ends up looking like a smooth plasma or a modern
  fragment shader, it has failed goal 1 no matter how good the physics are.
  Banding and dither texture are features to protect, not artifacts to minimize.
- **A lamp that ignores the machine.** If you can't tell a busy box from an idle
  one by looking, the central idea is dead and it's just a screensaver.
- **A screensaver that burns a core.** Absurd on its face, and worse than
  absurd here: CPU load is an *input*, so a hot render loop feeds on its own
  output and pins the lamp to maximum heat forever. See "Self-feedback" below —
  this is a correctness issue, not just a performance one.
- **Mandatory GPU.** Must degrade gracefully to CPU-only on machines with no
  readable GPU utilization.

## Constraints

- **Language:** C++ (user's choice). Target C++20.
- **Dependencies:** none beyond libc/libstdc++ and pthreads. No ncurses — we
  emit escape sequences directly, because we want precise control over glyph and
  color output and ncurses' optimizer fights that. Optionally `dlopen` for NVML.
  Keeping the dependency set empty makes this trivial to build on the user's
  NixOS box.
- **Build:** CMake.
- **Terminal assumptions:** UTF-8 output, 256-color support, cursor
  addressing, alternate screen buffer. Ubiquitous in any terminal from the last
  decade. We do not probe or degrade for less.

## Target machine (measured, not assumed)

Probed on the user's box on 2026-10-26:

| Source | Finding |
|---|---|
| CPU | `/proc/stat`, 24 logical cores |
| GPU (discrete) | NVIDIA RTX 4070 Laptop, `nvidia-smi` present |
| GPU (integrated) | AMD, `/sys/class/drm/card2/device/gpu_busy_percent` readable |
| Network | `wlp99s0` is the live NIC. Also present: `lo`, `docker0`, `tun0`, `tailscale0`, four `br-*` bridges, seven `veth*` |
| Thermal | `/sys/class/thermal/thermal_zone0`, type `acpitz`, reading 71°C |

Three things fall out of this and must be handled:

1. **It's a hybrid GPU laptop.** Reading only one GPU means the lamp goes dead
   whenever work is on the other one. Read both, take the max.
2. **Loopback and Docker traffic would drown out the real NIC.** `lo` alone has
   91 GB of lifetime traffic. Interfaces are whitelisted by pattern, not summed
   blindly.
3. **`nvidia-smi` is a fork+exec costing ~100 ms.** Unusable inside a 30 fps
   render loop. This forces the threading design below.

## How it works

### Architecture

Four pieces, deliberately decoupled:

```
  metrics thread (1 Hz)          main thread (30+ fps)
  ┌─────────────────┐            ┌──────────────────────┐
  │ /proc/stat      │            │  sim step (blobs)    │
  │ sysfs / nvidia  │  ─ Heat ─> │  field eval          │
  │ /proc/net/dev   │  snapshot  │  glyph + color select│
  │ thermal zones   │            │  diff + emit         │
  └─────────────────┘            └──────────────────────┘
```

The metrics thread polls slowly and publishes an immutable `Heat` snapshot. The
render loop reads the last-known snapshot every frame and never blocks on I/O.
This is non-negotiable: a 100 ms `nvidia-smi` call in the render path would
stutter the animation to garbage.

### The simulation

A **blob system with thermal buoyancy**, not a grid fluid solver. This choice is
central to goal 1: real lava lamps are about *discrete immiscible globs* that
merge and split, and a metaball field reproduces that iconography directly,
while a grid solver tends toward smoke-like diffusion — which lands on the
gradient-mush anti-goal.

Each blob carries position, velocity, radius, and its own temperature. Per step:

- Blobs near the floor absorb heat; blobs near the ceiling shed it.
- Buoyancy acts upward, proportional to the blob's temperature above ambient.
  Hot blobs rise, cool at the top, sink, reheat. The cycle is emergent, not
  scripted — nothing tells a blob to turn around.
- Drag, plus soft containment at the walls.
- A curl-noise velocity field adds organic wander, so motion never looks like
  objects on rails.
- Blobs that overlap sufficiently **merge** (conserving volume); blobs that grow
  too large or experience too much shear **split**. This is the signature lava
  lamp behavior and should be tuned until it's clearly visible at idle.

The scalar field at any point is the usual smooth falloff sum over nearby blobs.
Per-blob bounding boxes cull the accumulation so cost scales with coverage, not
with (pixels × blobs).

Sim runs on a fixed timestep, decoupled from render framerate, with `dt`
clamped so a scheduling hiccup can't explode the integrator.

### Heat → motion mapping

The four inputs, each normalized to 0..1 and smoothed, as agreed with the user:

| Input | Drives | Why this pairing |
|---|---|---|
| **CPU utilization** | Convection rate, blob count | The primary driver. Idle gives few, fat, slow blobs; load gives many, faster, more agitated ones. CPU is the metric people have the strongest intuition for, so it gets the most legible channel: overall liveliness. |
| **Temperature** | Palette | The most literal mapping available. A cool box sits in deep reds and magentas; as it heats, the ramp climbs through orange and yellow toward white-hot. The lamp glows hotter when the laptop actually is hotter. |
| **GPU utilization** | Turbulence (curl-noise amplitude, shear) | Deliberately *not* speed. GPU gets chaos rather than pace, so it's distinguishable from CPU at a glance: a GPU-bound workload makes the goo tear and swirl, a CPU-bound one makes it race. Max of NVIDIA and AMD readings. |
| **Network throughput** | Discrete impulses | Traffic is spiky and bursty by nature, so a continuous parameter wastes it. Bursts instead fire jets of droplets up from the floor — punctuation rather than a dial. |

**Smoothing is per-channel and asymmetric**, because the physical meaning of
each signal differs:

- CPU: moderate time constant (~1 s). Responsive but not jittery.
- Temperature: slow (~10 s). It's physically slow; pretending otherwise looks
  like noise.
- GPU: moderate.
- Network: **fast attack, slow decay**, so a burst registers instantly and then
  fades like a splash settling.

**Network normalization** is adaptive. Absolute byte rates span orders of
magnitude and no fixed ceiling works across idle browsing and a 1 Gb transfer,
so throughput is mapped logarithmically against a slowly-adapting observed peak.
Consequence the builder should accept deliberately: this means the lamp
self-calibrates to the machine's recent habits rather than to absolute
bandwidth. That's correct for goal 2 — visible response matters more than a
meaningful absolute scale.

**Self-feedback.** The lamp's own CPU time is subtracted from the CPU reading
(via `/proc/self/stat`) before normalization. Without this, the renderer's own
load raises the heat, which raises activity, which raises load. The lamp must
not eat itself.

**Graceful degradation.** Every source independently reports availability. A
missing source contributes nothing and the lamp stays beautiful on a machine
with no GPU counter, no thermal zone, or no network — it just has fewer moods.

### Rendering

**Vertical resolution doubling.** A terminal cell is roughly twice as tall as it
is wide. Treating each cell as two stacked square pixels via half-block glyphs
gives square sampling and doubles effective vertical resolution. The field is
therefore sampled at `cols × (rows × 2)`.

**Glyph selection is where the BBS look is won or lost.** Per cell, we have a
top and a bottom field sample. Two cases:

- **The two samples land in different palette bands** → emit `▀` (upper half
  block) with foreground = top color, background = bottom color. This renders
  crisp blob silhouettes and hard color boundaries at double vertical
  resolution. This is the *edge* case.
- **The two samples land in the same band** → there's no silhouette here, so the
  cell is interior, and we spend it on texture instead. Use the residual within
  the band to pick a shading glyph from `' ' ░ ▒ ▓ █`, with foreground set to
  the next palette entry and background to the current one.

That second case is the important one. It is ordered dithering between adjacent
palette entries, and it does two jobs at once: it multiplies effective color
depth well past the palette size, and it fills blob interiors with exactly the
chunky dither texture that reads as hand-made ANSI art. The banding and the
texture are the aesthetic — the builder should resist any instinct to smooth
them away.

**Palette construction.** Ramps are authored as RGB control points and
interpolated in a perceptually-uniform space, then each ramp entry is quantized
to its nearest xterm-256 index. Interpolating *index* numbers instead would
produce garbage, since the xterm palette isn't ordered perceptually. Temperature
blends between a cool ramp and a hot ramp; the quantized ramp is recomputed only
when temperature has moved materially, not per frame.

**Output path.** Each frame is composed into a single buffer and written with
one `write()`. Two optimizations matter: cell-level diffing against the previous
frame, and run-length coalescing of SGR sequences so color escapes are emitted
only when fg/bg actually change. Without the second one, a 200×50 terminal
costs tens of kilobytes per frame and the lamp becomes bandwidth-bound rather
than compute-bound.

### Details that came up on review

Settled by the author so the builder isn't guessing:

- **The screen is never empty.** Field values below the lowest blob band render
  as the darkest palette entry — a dark surrounding fluid — rather than
  terminal-default black. The user asked for the terminal *filled* with goo, so
  there is always medium everywhere; blobs are bright regions within it, not
  objects floating on a void.
- **CPU is read in aggregate**, not per-core. Per-core detail has nowhere
  legible to go in this visual, and aggregate is what matches the viewer's
  intuition of "the box is busy."
- **Seed the sim at startup** with randomized blob positions and temperatures so
  frame one already looks like a lamp mid-cycle. Starting from a cold uniform
  state and waiting for convection to develop makes launch look broken.
- **Minimum viable terminal size.** Below roughly 20×10, bail with a plain
  message on the normal screen rather than rendering a corrupt few cells.

### Testability

The build will be test-driven, which constrains the structure: **all parsing and
math must be callable without touching the filesystem, the clock, or a
terminal.**

- Every `/proc` and sysfs parser takes **string content as input**, not a path.
  Real snapshots from the target machine (captured in this document's probe) make
  good fixtures, including the awkward ones — the 15-interface `/proc/net/dev`,
  the hybrid-GPU sysfs layout.
- Metric normalization and smoothing take explicit `dt` and prior state. No
  reading the clock internally.
- Field evaluation, glyph selection, and palette quantization are pure functions
  of their inputs and are directly assertable.
- The renderer writes to an abstract sink, so a test can render a known field to
  a string and assert on the exact escape sequences — which is also the only
  practical way to verify the diffing and SGR-coalescing logic.

The sim's *aesthetic* tuning is not unit-testable and shouldn't be faked with
assertions on magic constants. Test the mechanics (blobs conserve volume on
merge, buoyancy sign is correct, `dt` clamping holds); judge the beauty by
looking.

### Terminal lifecycle

Enter the alternate screen buffer and hide the cursor on start; restore both on
exit, **including on signal and on crash**. A screensaver that leaves the
terminal wrecked is a bad citizen. `SIGINT`/`SIGTERM` set a flag that the main
loop observes and exits cleanly through the normal teardown path. `SIGWINCH`
triggers re-reading the terminal size and reallocating buffers, so resizing
mid-run works rather than corrupting the display.

Echo is disabled so stray keystrokes don't litter the screen, and stdin is
drained so they don't queue up for the shell after exit.

## Decisions left to the builder

Tuning is the actual craft here, and the numbers aren't specifiable in advance —
they have to be found by looking at it. The builder owns:

- All sim constants: blob counts, radii, buoyancy and drag coefficients,
  heating/cooling rates, merge and split thresholds, noise scale.
- The specific RGB control points for the cool and hot ramps, and the ramp
  length.
- Exact smoothing time constants and the dither threshold.
- Target framerate, and the internal structure of the code.

**Tune by watching it, not by reasoning about it.** When a constant and the
aesthetic disagree, the aesthetic wins.

## Calls made by the author, not the user

Flagged so they can be overridden:

- **`q` / `Esc` / `Ctrl-C` quit.** Strictly speaking the user said "nothing is
  interactive"; read in context that meant no controls affecting the *art*, and
  a screensaver you can't dismiss with `q` would be hostile.
- **A couple of debug-only CLI flags** (e.g. forcing a framerate, staying off
  the alternate screen) to make development and tuning tractable. Not user-facing
  configuration, and not documented as such.
- **No ncurses**, as reasoned above.
- **Blob/metaball sim over a grid fluid solver**, as reasoned above.
- **Linux-only** rather than abstracting the metric layer for portability.
