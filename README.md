# Spectrum Analyzer

Real-time audio spectrum analyzer for Linux, with OSC output.

![Spectrum Analyzer running](screenshot.png)

An updated and improved fork of [luz](https://github.com/lighttroupe/luz):
ported to GTK 3 and modern OpenGL, with low-latency PulseAudio/ALSA capture
and a log-spaced analysis covering 20 Hz–20 kHz.

## Build

    sudo apt-get install build-essential libfftw3-dev liblo-dev \
        libgtkmm-3.0-dev libepoxy-dev
    make
    ./spectrum-analyzer

No `libpulse`/`libasound` development package is needed: capture goes through
the vendored miniaudio (see [`vendor/README.md`](vendor/README.md)), which loads
the sound server at runtime and also gives WASAPI on Windows and CoreAudio on
macOS.

Build the portable SDL3 front end (see below):

    sudo apt-get install libsdl3-dev
    make sdl
    ./spectrum-analyzer-sdl

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

## Packaging

| Platform | Command | Output |
|---|---|---|
| Linux | `make appimage` | `Spectrum_Analyzer-x86_64.AppImage`, ~8 MB (SDL front end, no toolkit runtime) |
| Linux | `make install PREFIX=/usr/local` | GTK front end + desktop file + icons |
| Windows | `make sdl STATIC=1` | a single `spectrum-analyzer-sdl.exe` |
| macOS | `./packaging/build-macos-app.sh` | `Spectrum Analyzer.app` in a zip |

CI (`.github/workflows/ci.yml`) builds all three on every push and attaches the
artifacts to a GitHub release when a `v*` tag is pushed.  The Windows and macOS
jobs are best-effort until they have run once: only the Linux workflow steps
have been executed locally.

## Front ends

Two user interfaces share the same analysis, capture and OSC modules:

| Target | Binary | Toolkit | Status |
|---|---|---|---|
| `make` | `spectrum-analyzer` | GTK 3 + OpenGL core | Linux desktop UI; frozen at 1.0.1 on the `1.x` branch, still the default build |
| `make sdl` | `spectrum-analyzer-sdl` | SDL 3 (rectangles only) | the portable one, built and packaged for Linux, Windows and macOS |

The SDL front end exists because GTK 3's `GtkGLArea` does not work on macOS at
all; SDL 3 builds and runs identically on Linux, Windows and macOS. See
[`docs/CROSS-PLATFORM.md`](docs/CROSS-PLATFORM.md).

SDL front end controls: click `Source: … v` for the list of capture devices
(`S` cycles without opening it, `↑`/`↓` + Enter work inside the list),
`+`/`-` change the analyzer number, `F` toggles fullscreen, `Esc` quits, and
clicking the `OSC:` field lets you type a new `host:port` (Enter applies it).

## Layout

| File | Role |
|---|---|
| `spectrum-dsp.{h,cc}` | Analysis core: windowing, FFT, log bands, envelope follower. No I/O, no threads, no platform headers — this is what `make test` exercises. |
| `audio-sampler.{h,cc}` | Capture via miniaudio (PulseAudio/PipeWire → ALSA → JACK on Linux, WASAPI on Windows, CoreAudio on macOS), with automatic reconnect. Platform types stay behind `Impl`. |
| `message-bus.{h,cc}` | OSC destination and senders (liblo). |
| `spectrum-analyzer-window.{h,cc}`, `my-drawing-area.{h,cc}` | GTK3 window, widgets and the GL canvas. |
| `app-sdl.cc` | SDL3 front end: window, renderer, input, frame loop. |
| `filter.{h,cc}` | The draggable boxes: hit testing, dragging, resizing, activation. Pure geometry. |
| `gl-compat.{h,cc}` | Small shim providing immediate-mode style drawing on an OpenGL 3.2 core profile. |
| `math-utils.h` | Dependency-free geometry helpers. |
| `vendor/miniaudio.h` | Vendored capture library, see [`vendor/README.md`](vendor/README.md). |
| `tests/dsp-test.cc`, `tests/filter-test.cc` | Headless test suites (147 assertions). |

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

The program links FFTW, which is distributed under the GNU GPL v2 or later;
GPL-3.0 is compatible with that. GTK 3, liblo and libepoxy are LGPL-2.1+/MIT,
and the vendored miniaudio is public domain / MIT-0.

## Versioning

`version.h` holds the only copy of the version number.  Two release lines exist:

| Line | Branch | Content |
|---|---|---|
| `1.0.x` | `1.x` | GTK-only, for everyone who stays on the desktop GTK build |
| `1.1.x` | `main` | both front ends, portable audio, packaged for three platforms |

Whether the GTK front end is dropped in a future 2.0 depends on how 1.1.x is
received.

## Docs

- [`CHANGELOG.md`](CHANGELOG.md) — what changed, release by release.
- [`docs/AUDIT.md`](docs/AUDIT.md) — code audit: fixed defects, open decisions,
  licensing note (FFTW is GPL).
- [`docs/CROSS-PLATFORM.md`](docs/CROSS-PLATFORM.md) — what a macOS/Windows port
  actually requires, and the recommended packaging per OS.
