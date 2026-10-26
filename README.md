# lavalamp

A terminal lava lamp in the visual language of classic BBS/ANSI artwork, where
the heat comes from what your machine is actually doing.

The whole terminal is the inside of the lamp — no base, no cap, no border, no
stats overlay. Blobs of wax heat at the floor, rise, cool at the ceiling and
sink, merging and splitting on the way. It is drawn entirely out of CP437 block
and shading glyphs (`░▒▓█` and the upper half block `▀`) in 256-colour mode, so
it bands and dithers the way hand-made ANSI art does rather than looking like a
smooth gradient.

## The machine is the heat source

| What it reads | What it changes |
|---|---|
| CPU utilization | Convection — how fast everything rises, and how many blobs there are |
| Temperature | The palette — deep purples and magentas when cool, climbing through orange and amber to white-hot |
| GPU utilization | Turbulence — blobs tear and swirl rather than simply moving faster |
| Network throughput | Jets of droplets fired up from the floor on bursts |

Idle looks slow, fat and lazy. Start a build and the lamp gets violent. Every
channel degrades independently: no readable GPU counter, no thermal zone or no
network just means fewer moods, not a broken lamp.

CPU is read with this process's own time subtracted, so the renderer cannot
feed on its own load.

## Requirements

- **Linux.** Every metric here is Linux-specific plumbing (`/proc`, sysfs,
  `nvidia-smi`).
- A UTF-8, 256-colour terminal at least 20x10.
- C++20 compiler and CMake. No third-party dependencies.

## Build

```sh
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
