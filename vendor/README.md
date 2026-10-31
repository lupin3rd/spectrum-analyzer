# Vendored dependencies

## miniaudio 0.11.26

- Upstream: <https://github.com/mackron/miniaudio>
- Files: `miniaudio.h` (single header, ~97k lines), `miniaudio-impl.cc` (our
  implementation unit), `miniaudio-LICENSE`.
- License: public domain (Unlicense) *or* MIT No Attribution, at your option.
  Compatible with this project's GPL-3.0-or-later.

Why it is vendored: audio capture is the one subsystem with no portable OS API.
miniaudio drives PulseAudio/PipeWire, ALSA and JACK on Linux, WASAPI on Windows
and CoreAudio on macOS behind one interface, and it loads those libraries at
runtime with `dlopen` — so the build has no link-time dependency on
`libpulse` or `libasound`, and the produced binary does not require them.

The implementation lives in its own translation unit
(`miniaudio-impl.cc`) so editing our code does not recompile 97k lines. Only
the used features are enabled (`MA_NO_DECODING`, `MA_NO_ENCODING`,
`MA_NO_RESOURCE_MANAGER`, `MA_NO_NODE_GRAPH`, `MA_NO_ENGINE`,
`MA_NO_GENERATION`).

### Updating

```sh
curl -sSL -o vendor/miniaudio.h \
    https://raw.githubusercontent.com/mackron/miniaudio/<version>/miniaudio.h
curl -sSL -o vendor/miniaudio-LICENSE \
    https://raw.githubusercontent.com/mackron/miniaudio/<version>/LICENSE
```

Then rebuild and check the API surface used in `audio-sampler.cc`:
`ma_context_init`, `ma_context_get_devices`, `ma_device_config_init`,
`ma_device_init`, `ma_device_start`, `ma_device_uninit`,
`ma_device_notification_type_*`, `ma_get_backend_name`.
