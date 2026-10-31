// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __AUDIO_SAMPLER_H__
#define __AUDIO_SAMPLER_H__

// Audio capture front end.
//
// Capture goes through miniaudio (vendored in vendor/miniaudio.h), so the same
// code drives PulseAudio/PipeWire, ALSA and JACK on Linux, WASAPI on Windows
// and CoreAudio on macOS.  miniaudio loads the platform libraries at runtime,
// so this program does not link against libpulse or libasound at all.
//
// All backend types are hidden behind Impl: including this header pulls in no
// audio headers, and the analysis (spectrum-dsp.h) knows nothing about capture.

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct AudioSourceInfo
{
	std::string id;          // opaque device id; empty means "default device"
	std::string description; // human readable name
	bool is_monitor;         // true for "Monitor of ..." sources
};

// Turn what the user typed on the command line into a device id.
//
// The ids miniaudio uses are opaque blobs (hex-encoded, see audio-sampler.cc),
// so the name that --list-sources prints next to an id is what people will
// paste.  Accepts, in order: an exact id, an exact description, or a
// case-insensitive substring of a description.  Returns the argument unchanged
// when nothing matches, so the caller still reports a sensible error.
std::string resolve_audio_source(const std::string& argument);

class AudioSampler
{
public:
	// Capture state.  Declared here so the miniaudio callbacks can reach it,
	// defined in audio-sampler.cc; the type is incomplete, so clients cannot
	// touch anything.
	struct Impl;

	AudioSampler();
	~AudioSampler();

	AudioSampler(const AudioSampler&) = delete;
	AudioSampler& operator=(const AudioSampler&) = delete;

	// List the capture devices the default backend offers.  Prefers
	// PulseAudio on Linux, which is what exposes the "Monitor of ..." sources
	// needed to analyze what is playing rather than what is recorded.
	static std::vector<AudioSourceInfo> EnumerateSources();

	// Open a capture device.  An empty id means "default".
	// Capture runs on miniaudio's own thread: this never blocks the UI loop.
	bool Open(const std::string& source_name);
	void Close();

	// Call once per frame from the UI thread.  Reopens the device when the
	// sound server or the hardware went away (PipeWire restart, USB unplug,
	// macOS media services reset).  Cheap when there is nothing to do.
	void Poll();

	bool IsOpen() const { return m_open.load(); }
	const char* GetBackendName() const;
	int NumBars() const;

	// Number of times the capture stream had to be reopened.
	unsigned long ReconnectCount() const { return m_reconnects.load(); }

	// Copy the latest bar magnitudes (thread-safe snapshot) into out[].
	// Safe to call before Open(): the buffer then holds zeros.
	void CopyMagnitudes(float* out, int count);

private:
	bool OpenDevice(const std::string& source_name);
	void CloseDevice();

	std::unique_ptr<Impl> m_impl;
	std::atomic<bool> m_open;
	std::atomic<unsigned long> m_reconnects;
};

#endif
