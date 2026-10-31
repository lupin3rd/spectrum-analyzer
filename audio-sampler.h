// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __AUDIO_SAMPLER_H__
#define __AUDIO_SAMPLER_H__

// Audio capture front end.
//
// Two backends are supported today, both Linux only: PulseAudio (which also
// covers PipeWire through pipewire-pulse) and raw ALSA.  All backend specific
// types are hidden behind Impl, so this header stays free of <alsa/...> and
// <pulse/...> and compiles on any platform.  Adding WASAPI/CoreAudio means
// adding one Impl method pair, not touching the header.
//
// The analysis itself lives in spectrum-dsp.h and knows nothing about capture.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "spectrum-dsp.h"

// Human-readable description of a capture source (microphone, "Monitor of ...", etc.).
struct AudioSourceInfo
{
	std::string id;          // backend-specific id; "" means "default"
	std::string description; // human readable name
	bool is_monitor;         // true for "Monitor of ..." sources
};

class AudioSampler
{
public:
	AudioSampler();
	~AudioSampler();

	AudioSampler(const AudioSampler&) = delete;
	AudioSampler& operator=(const AudioSampler&) = delete;

	// List available capture sources.  Prefers PulseAudio when it is
	// available (it works on both PulseAudio and PipeWire via
	// pipewire-pulse); otherwise falls back to ALSA device names.
	static std::vector<AudioSourceInfo> EnumerateSources();

	// Open a capture source.  "" means "default".
	// Tries PulseAudio first, then falls back to ALSA.
	// Capture runs on its own thread, so this never blocks the UI loop.
	bool Open(const std::string& source_name);
	void Close();

	bool IsOpen() const { return m_open.load(); }
	const char* GetBackendName() const;
	int NumBars() const { return m_dsp.NumBars(); }

	// Number of times the capture stream had to be reopened because the sound
	// server or the device disappeared (PipeWire restart, USB unplug, ...).
	unsigned long ReconnectCount() const { return m_reconnects.load(); }

	// Copy the latest bar magnitudes (thread-safe snapshot) into out[].
	// Safe to call before Open(): the buffer then holds zeros.
	void CopyMagnitudes(float* out, int count);

private:
	struct Impl;

	enum ReadStatus { READ_OK, READ_RETRY, READ_FATAL };

	void CaptureLoop();
	bool OpenBackend(const std::string& source_name);
	void CloseBackend();
	ReadStatus ReadBlock();
	// Sleep for `milliseconds`, returning false early if Close() asked the
	// capture thread to stop.
	bool WaitWhileRunning(unsigned milliseconds);

	std::unique_ptr<Impl> m_impl;

	sa::SpectrumDsp m_dsp;
	std::string m_source_name;

	std::thread m_thread;
	std::atomic<bool> m_running;
	std::atomic<bool> m_open;
	std::atomic<unsigned long> m_reconnects;

	// Backend handles are only ever touched by Open()/Close() and by the
	// capture thread while it owns the stream; this mutex additionally guards
	// m_dsp against concurrent Configure()/CopyMagnitudes().
	mutable std::mutex m_mutex;
};

#endif
