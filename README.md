# lavalamp

A terminal lava lamp in the visual language of classic BBS/ANSI artwork, where
the heat comes from what your machine is actually doing.

The whole terminal is the inside of the lamp — no base, no cap, no border, no
stats overlay. Blobs of wax heat at the floor, rise, cool at the ceiling and
sink, merging and splitting on the way. It is drawn entirely out of CP437 block
and shading glyphs (`░▒▓█` and the upper half block `▀`) with a 256-colour
palette, so it bands and dithers like hand-made ANSI art rather than looking
like a smooth gradient. On truecolor terminals, palette changes fade over
1.5 seconds instead of switching the whole background at once. Terminals that
only advertise 256 colours retain indexed output.

Merges, splits, and network droplets morph over 350 ms instead of popping
between shapes. Network jets reuse existing wax, so long-running sessions
cannot flood into a solid colour.

## The machine is the heat source

| What it reads | What it changes |
|---|---|
| CPU utilization | Convection — how fast everything rises, and how many blobs there are |
| Temperature | The palette — deep purples and magentas when cool, climbing through orange and amber to white-hot |
| GPU utilization | Turbulence — blobs tear and swirl rather than simply moving faster |
| Network throughput | Jets of droplets fired up from the floor on bursts |
| RAM usage (excluding reclaimable cache) | Viscosity — fuller memory makes the wax move more heavily |
| Playback audio level | Rhythmic outward/inward pulses, stronger with louder sound |

Idle looks slow, fat and lazy. Start a build and the lamp gets violent. Every
channel degrades independently: missing counters or audio tools just mean
fewer moods, not a broken lamp.

CPU is read with this process's own time subtracted, so the renderer cannot
feed on its own load. RAM usage comes from `MemTotal` and `MemAvailable` in
`/proc/meminfo`, so filesystem caches do not make the lamp falsely sluggish.

Audio monitors the default **playback output**, never the microphone, using
`pw-record` (PipeWire) or `parec` (PulseAudio). The playback level is sampled at
20 Hz with fast attack and a short decay; it controls pulse strength, not
beat detection. Audio samples are processed in memory and never saved.
Without either helper or an accessible audio server, this channel stays off.

## Requirements

- **Linux.** Every metric here is Linux-specific plumbing (`/proc`, sysfs,
  `nvidia-smi`).
- A UTF-8, 256-colour terminal at least 20x10. Truecolor is recommended for
  smooth palette fades; detected through `COLORTERM=truecolor`,
  `COLORTERM=24bit`, or a `TERM` ending in `-direct`.
- C++20 compiler and CMake 3.20 or newer. No third-party dependencies.
- Optional: `nvidia-smi` for NVIDIA GPU utilization; AMD GPUs use sysfs.
- Optional: `pw-record` (PipeWire tools) or `parec` (PulseAudio utilities) for
  playback-reactive motion. No audio development libraries are needed.

## Build

```sh
git clone https://github.com/reklis/lavalamp.git
cd lavalamp
cmake -S . -B build
cmake --build build -j
```

On a machine without CMake installed but with Nix (a `shell.nix` is included):

```sh
nix-shell --run 'cmake -S . -B build && cmake --build build -j'
```

## Run

```sh
./build/lavalamp
```

Press `q`, `Esc`, or `Ctrl-C` to quit. The terminal is restored on every exit
path, including signals.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Or run a subset by name — the filter matches both test names and the file they
live in:

```sh
./build/lavalamp_tests sim
./build/lavalamp_tests palette
```

## Development flags

Not configuration — these exist for tuning and are not a supported interface.

| Flag | Effect |
|---|---|
| `--fps N` | Override the 60 fps target |
| `--trace` | Per-frame timing to stderr (dt, sim, render, sleep) |
| `--size WxH` | Force a size instead of asking the terminal |
| `--no-alt-screen` | Draw on the normal screen |

## Design

`docs/superpowers/specs/2026-10-26-ansi-lavalamp-design.md` covers the intent
and the reasoning, including the things it deliberately is not.

## License

Licensed under the [GNU General Public License v3.0](LICENSE)
(`GPL-3.0-only`).
