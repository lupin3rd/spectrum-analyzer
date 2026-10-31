# Changelog

All notable changes to this project, newest first.  Versions follow
[Semantic Versioning](https://semver.org/): this is an application, so a major
bump means "the product changed shape" (a front end removed or replaced), not
"an API broke".

## [1.1.2] — 2026-10-31

### Fixed

- `spectrum-analyzer <source>` only accepted the opaque hex device id, so the
  name that `--list-sources` prints as a description was rejected with
  "malformed device id" and the program silently fell back to the default
  device.  The argument is now resolved against the enumeration: an exact id, an
  exact description, or a case-insensitive substring of one.  The SDL front end
  also selects the resolved device in the drop-down.

### Changed

- **New screenshot** of the SDL front end, the one the downloads contain.
- **Support section**: the project is free and GPL-3.0, with a Buy Me a Coffee
  link for anyone who wants to say thank you.

## [1.1.1] — 2026-10-31

Documentation and packaging only: no code changes.

### Changed

- README rewritten around what a visitor needs: a download table for the three
  platforms, the controls, per-platform build instructions (Linux, macOS,
  Windows/MSYS2) and the packaging commands.
- `docs/RELEASING.md` records the two-repository policy — Gitea keeps the full
  development history and the agent transcripts, GitHub carries one commit per
  release — and `scripts/build-public-release.sh` automates it.
- This repository is now the release repository: the history contains one
  commit per release instead of the development history.  The full history,
  including the audit, the SDL 3 front end and the miniaudio migration, lives in
  the private Gitea instance.

### Fixed

- The README said the Windows build produces a single `.exe`.  It does not:
  liblo ships no static library in MSYS2, so the download is an `.exe` plus the
  DLLs it needs.
- The README described the Windows and macOS CI jobs as "best effort, never
  executed".  All three platforms are green and the macOS job attaches its own
  artifact to the release.

## [1.1.0] — 2026-10-31

The cross-platform release.  Two front ends ship side by side: the GTK 3 one
that Linux users already know, and a new SDL 3 one that is built and packaged
for Linux, Windows and macOS.

### Added

- **SDL 3 front end** (`make sdl`): the same analysis, capture and OSC code
  behind a portable window. No OpenGL profile, no shader, no widget toolkit —
  the scene is rectangles and lines, the text uses SDL's debug font, so there
  is no runtime to bundle and no font file to ship.
- **Source drop-down in the SDL front end**: pick a capture device from a list
  instead of cycling through them with `S`. Previously the only way to reach
  the fourth monitor was four key presses.
- **Packaging**: `make appimage` (8 MB AppImage of the portable front end),
  `packaging/build-macos-app.sh` (`.app` bundle), `make STATIC=1` (single
  `.exe` on Windows).
- **CI** on Linux, Windows and macOS; pushing a `v*` tag attaches the three
  artifacts to a GitHub release.
- `make test`: 147 headless assertions covering the analysis core and the
  filter boxes.
- `docs/AUDIT.md` and `docs/CROSS-PLATFORM.md`.

### Changed

- **Audio capture now goes through miniaudio**, so the same code drives
  PulseAudio/PipeWire, ALSA and JACK on Linux, WASAPI on Windows and CoreAudio
  on macOS. The device runs at its **native rate** (48 kHz here) instead of a
  forced 44.1 kHz, so PipeWire no longer resamples on the way in, and the build
  no longer needs `libpulse-dev`/`libasound2-dev`.
- The capture stream **reconnects with backoff** after a sound server restart
  or an unplugged device, and the number of recoveries is shown in the status
  bar.
- OSC keeps the same addresses and the same `float 0..1` values, but messages
  are now sent only when the activation changes by more than 0.002 instead of
  on every frame (240 packets/s of near-identical values before).
- The program starts even when the capture device is unavailable, retrying in
  the background instead of exiting.

### Fixed

- Tray icon destroyed immediately after creation.
- Capture thread spinning at 1 kHz forever after a read error.
- ALSA short reads discarding whole blocks.
- GL objects surviving a rebuilt context, which left the canvas black.
- Blend state leaking between frames: the bars' colour depended on the draw
  order.
- `Gtk::Label`, `Filter` and the quit dialog leaking.
- Drag deltas jumping when the pointer re-entered the canvas.
- `Filter::PointerPress` requiring a prior motion event, so a click without
  movement did not grab the box.

### Known issues

- The GTK source combo renders an empty label in its closed state (present
  since before this release); the popup lists the devices correctly.
- macOS builds are arm64 only: GitHub has been retiring its Intel runners, and
  an x86_64 binary cannot be produced from an arm64 runner without building
  every dependency as a universal binary.  Intel Macs build from source.  The
  `.app` is ad-hoc signed, so the first launch needs right-click -> Open.
- The Windows download is an `.exe` plus the DLLs it needs, zipped together,
  not a single file: liblo ships no static library in MSYS2, so a fully static
  link is impossible today.  Dropping liblo for ~80 lines of UDP would remove
  that limitation.
- The audio reconnection has not been exercised against a real sound server
  restart.

### Repository layout

The public repository on GitHub carries one commit per release, with the
development history and the `agent/` transcripts kept in the private Gitea
repository.  See `docs/RELEASING.md`.

## [1.0.1] — GTK maintenance line (`1.x` branch)

Everything above from the audit, applied to the GTK-only code base: tray icon,
capture reconnection, ALSA short reads, startup without an audio device, GL
context lifetime, OSC flooding, widget/filter leaks. No new technology.

## [1.0.0]

Modernized and improved fork of [luz](https://github.com/lighttroupe/luz):
ported to GTK 3 and modern OpenGL, low-latency PulseAudio/ALSA capture,
log-spaced analysis from 20 Hz to 20 kHz, OSC output.

[1.1.2]: https://github.com/lupin3rd/spectrum-analyzer/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/lupin3rd/spectrum-analyzer/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/lupin3rd/spectrum-analyzer/compare/v1.0.0...v1.1.0
[1.0.1]: https://github.com/lupin3rd/spectrum-analyzer/compare/v1.0.0...1.x
[1.0.0]: https://github.com/lupin3rd/spectrum-analyzer/releases/tag/v1.0.0
