# Spectrum Analyzer

Real-time audio spectrum analyzer for Linux, with OSC output.

![Spectrum Analyzer running](screenshot.png)

An updated and improved fork of [luz](https://github.com/lighttroupe/luz):
ported to GTK 3 and modern OpenGL, with low-latency PulseAudio/ALSA capture
and a log-spaced analysis covering 20 Hz–20 kHz.

## Build

    sudo apt-get install build-essential libasound2-dev libfftw3-dev liblo-dev \
        libgtkmm-3.0-dev libepoxy-dev libpulse-dev
    make
    ./spectrum-analyzer

Run the tests (no display, no audio device required):

    make test

Build with the address/undefined-behaviour sanitizers:

    make asan && ./spectrum-analyzer-asan

## Usage

    ./spectrum-analyzer                       # default capture source
    ./spectrum-analyzer --list-sources        # list capture sources and exit
    ./spectrum-analyzer <source>              # capture from a specific source
    ./spectrum-analyzer --version

The toolbar selects the capture source and the analyzer number (`01`…`100`); the
bottom bar sets the OSC destination (host + UDP port, `Enter` applies it). Drag
the coloured boxes over the spectrum to select bands: each box streams its
activation over OSC at

    /spectrum-analyzer/<nn>/<Name>     <float 0..1>

## Layout

| File | Role |
|---|---|
| `spectrum-dsp.{h,cc}` | Analysis core: windowing, FFT, log bands, envelope follower. No I/O, no threads, no platform headers — this is what `make test` exercises. |
| `audio-sampler.{h,cc}` | Capture thread and backends (PulseAudio → ALSA fallback), with automatic reconnect. Platform types are hidden behind `Impl`. |
| `message-bus.{h,cc}` | OSC destination and senders (liblo). |
| `spectrum-analyzer-window.{h,cc}`, `my-drawing-area.{h,cc}` | GTK3 window, widgets and the GL canvas. |
| `gl-compat.{h,cc}` | Small shim providing immediate-mode style drawing on an OpenGL 3.2 core profile. |
| `math-utils.h` | Dependency-free geometry helpers. |
| `tests/dsp-test.cc` | Headless test suite for the analysis core. |

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

The program links FFTW, which is distributed under the GNU GPL v2 or later;
GPL-3.0 is compatible with that. GTK 3, liblo, libepoxy, ALSA and PulseAudio are
LGPL-2.1+/MIT.

## Docs

- [`docs/AUDIT.md`](docs/AUDIT.md) — code audit: fixed defects, open decisions,
  licensing note (FFTW is GPL).
- [`docs/CROSS-PLATFORM.md`](docs/CROSS-PLATFORM.md) — what a macOS/Windows port
  actually requires, and the recommended packaging per OS.
