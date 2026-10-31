// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "audio-sampler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <unistd.h>
#endif

#ifdef HAVE_PULSE
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#endif

#if defined(__linux__)
#define ALSA_PCM_NEW_HW_PARAMS_API		// use new API
#include <alsa/asoundlib.h>
#endif

namespace {

const char* kDefaultAlsaDeviceName = "default";

// Capture format.  44.1 kHz mono float is what the analysis assumes; the sound
// server resamples transparently if the device runs at another rate.
const unsigned kCaptureRate = 44100;
const unsigned kCaptureFrameSize = 256;

// Reconnect policy when the stream dies (sound server restart, device unplug).
const unsigned kReconnectInitialDelayMs = 100;
const unsigned kReconnectMaxDelayMs = 2000;
// Do not spam the log while the source stays unavailable.
const unsigned long kReconnectLogEvery = 30;

enum BackendId {
	BACKEND_NONE = 0,
	BACKEND_PULSE = 1,
	BACKEND_ALSA = 2
};

std::string first_line(const char* s)
{
	if (!s)
		return "";
	std::string str(s);
	const size_t nl = str.find('\n');
	if (nl != std::string::npos)
		str.erase(nl);
	return str;
}

#ifdef HAVE_PULSE
struct PulseEnumState
{
	std::vector<AudioSourceInfo>* sources;
	bool done;
};

void pulse_source_cb(pa_context*, const pa_source_info* i, int eol, void* userdata)
{
	PulseEnumState* st = (PulseEnumState*)userdata;
	if (eol) {
		st->done = true;
		return;
	}
	if (!i)
		return;

	AudioSourceInfo info;
	info.id = i->name ? i->name : "";
	info.description = first_line(i->description ? i->description : i->name);
	info.is_monitor = (i->monitor_of_sink != PA_INVALID_INDEX);
	st->sources->push_back(info);
}

void pulse_state_cb(pa_context* c, void* userdata)
{
	PulseEnumState* st = (PulseEnumState*)userdata;
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY: {
			pa_operation* op = pa_context_get_source_info_list(c, pulse_source_cb, st);
			if (op)
				pa_operation_unref(op);
			break;
		}
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			st->done = true;
			break;
		default:
			break;
	}
}

std::vector<AudioSourceInfo> pulse_enumerate_sources()
{
	std::vector<AudioSourceInfo> sources;

	pa_mainloop* ml = pa_mainloop_new();
	if (!ml)
		return sources;

	pa_mainloop_api* api = pa_mainloop_get_api(ml);

	PulseEnumState st;
	st.sources = &sources;
	st.done = false;

	pa_context* c = pa_context_new(api, "Spectrum Analyzer");
	if (!c) {
		pa_mainloop_free(ml);
		return sources;
	}

	pa_context_set_state_callback(c, pulse_state_cb, &st);
	pa_context_connect(c, NULL, PA_CONTEXT_NOFLAGS, NULL);

	// Pump the event loop until the list arrives or we time out (~2 s).
	for (int i = 0; i < 2000 && !st.done; i++) {
		if (pa_mainloop_iterate(ml, 0, NULL) < 0)
			break;
#if defined(__linux__)
		usleep(1000);
#endif
	}

	pa_context_disconnect(c);
	pa_context_unref(c);
	pa_mainloop_free(ml);

	return sources;
}
#endif // HAVE_PULSE

#if defined(__linux__)
std::vector<AudioSourceInfo> alsa_enumerate_sources()
{
	std::vector<AudioSourceInfo> sources;

	void** hints = NULL;
	if (snd_device_name_hint(-1, "pcm", &hints) != 0)
		return sources;

	for (int i = 0; hints && hints[i]; i++) {
		char* name = snd_device_name_get_hint(hints[i], "NAME");
		char* desc = snd_device_name_get_hint(hints[i], "DESC");
		char* ioid = snd_device_name_get_hint(hints[i], "IOID");

		if (name && name[0]
			&& strcmp(name, "null") != 0
			&& strcmp(name, "default") != 0) {
			const bool is_input = (ioid == NULL)
				|| (strcmp(ioid, "Input") == 0)
				|| (strcmp(ioid, "Input/Output") == 0);
			if (is_input) {
				AudioSourceInfo info;
				info.id = name;
				info.description = first_line(desc && desc[0] ? desc : name);
				info.is_monitor = false;
				sources.push_back(info);
			}
		}

		free(name);
		free(desc);
		free(ioid);
	}

	snd_device_name_free_hint(hints);
	return sources;
}
#endif // __linux__

} // anonymous namespace

// ---------------------------------------------------------------------------
// Backend state.  Defined here so no platform header leaks into the interface.
// ---------------------------------------------------------------------------

struct AudioSampler::Impl
{
	std::atomic<int> backend;
	unsigned rate;
	unsigned frame_size;			// frames delivered per ReadBlock()
	std::vector<float> frame_buffer;

#ifdef HAVE_PULSE
	pa_simple* pulse;
#endif

#if defined(__linux__)
	snd_pcm_t* alsa;
#endif

	Impl()
		: backend(BACKEND_NONE),
			rate(kCaptureRate),
			frame_size(kCaptureFrameSize)
#ifdef HAVE_PULSE
			, pulse(NULL)
#endif
#if defined(__linux__)
			, alsa(NULL)
#endif
	{
	}
};

// ---------------------------------------------------------------------------
// AudioSampler
// ---------------------------------------------------------------------------

AudioSampler::AudioSampler()
	: m_impl(new Impl()),
		m_running(false),
		m_open(false),
		m_reconnects(0)
{
}

AudioSampler::~AudioSampler()
{
	Close();
}

std::vector<AudioSourceInfo> AudioSampler::EnumerateSources()
{
	std::vector<AudioSourceInfo> sources;

	AudioSourceInfo def;
	def.id = "";
	def.description = "Default";
	def.is_monitor = false;
	sources.push_back(def);

#ifdef HAVE_PULSE
	const std::vector<AudioSourceInfo> pulse_sources = pulse_enumerate_sources();
	if (!pulse_sources.empty()) {
		sources.insert(sources.end(), pulse_sources.begin(), pulse_sources.end());
		return sources;
	}
#endif

#if defined(__linux__)
	const std::vector<AudioSourceInfo> alsa_sources = alsa_enumerate_sources();
	sources.insert(sources.end(), alsa_sources.begin(), alsa_sources.end());
#endif

	return sources;
}

bool AudioSampler::Open(const std::string& source_name)
{
	// Stops the capture thread and releases the previous stream first, so the
	// backend handles below are never shared with a running thread.
	Close();
	m_source_name = source_name;

	if (!OpenBackend(source_name)) {
		fprintf(stderr, "AudioSampler: could not open capture source '%s'\n",
			source_name.empty() ? "(default)" : source_name.c_str());
		return false;
	}

	m_open = true;
	m_running = true;
	m_thread = std::thread(&AudioSampler::CaptureLoop, this);
	return true;
}

void AudioSampler::Close()
{
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();

	CloseBackend();
	m_open = false;

	std::lock_guard<std::mutex> lock(m_mutex);
	m_dsp.Reset();
}

const char* AudioSampler::GetBackendName() const
{
	switch (m_impl->backend.load()) {
		case BACKEND_PULSE: return "PulseAudio";
		case BACKEND_ALSA:  return "ALSA";
		default:            return "none";
	}
}

bool AudioSampler::OpenBackend(const std::string& source_name)
{
	CloseBackend();

	const char* device = source_name.empty() ? NULL : source_name.c_str();

#ifdef HAVE_PULSE
	pa_sample_spec ss;
	ss.format = PA_SAMPLE_FLOAT32LE;
	ss.rate = kCaptureRate;
	ss.channels = 1;

	// Ask for a low-latency record stream.  With the default (server chosen)
	// buffer attributes PulseAudio hands the data over in large, bursty
	// chunks: dozens of reads return instantly and are then followed by a
	// gap of a few hundred milliseconds.  The analyzer bars then freeze and
	// jump instead of moving smoothly.  Requesting one fragment per period
	// (and a small overall buffer) makes the reads arrive at a steady rate.
	pa_buffer_attr attr;
	attr.tlength   = (uint32_t)-1;   // playback only
	attr.prebuf    = (uint32_t)-1;   // playback only
	attr.minreq    = (uint32_t)-1;   // playback only
	attr.maxlength = (uint32_t)(kCaptureFrameSize * sizeof(float) * 4);
	attr.fragsize  = (uint32_t)(kCaptureFrameSize * sizeof(float));

	int error = 0;
	pa_simple* pulse = pa_simple_new(
		NULL,                          // default server (PulseAudio or pipewire-pulse)
		"Spectrum Analyzer",           // application name
		PA_STREAM_RECORD,              // direction
		device,                        // source name (NULL = default)
		"Spectrum Analyzer Input",     // stream description
		&ss,                           // sample format
		NULL,                          // channel map (default)
		&attr,                         // low-latency buffer attributes
		&error);

	if (!pulse) {
		// A few servers reject very small buffers.  Rather than giving up on
		// PulseAudio entirely, retry with the server default; smooth motion is
		// lost, but capture still works.
		fprintf(stderr, "PulseAudio: low-latency buffer rejected (%s); retrying with defaults\n",
			pa_strerror(error));
		error = 0;
		pulse = pa_simple_new(
			NULL, "Spectrum Analyzer", PA_STREAM_RECORD, device,
			"Spectrum Analyzer Input", &ss, NULL, NULL, &error);
	}

	if (pulse) {
		m_impl->pulse = pulse;
		m_impl->rate = kCaptureRate;
		m_impl->frame_size = kCaptureFrameSize;
		m_impl->backend.store(BACKEND_PULSE);
	}
#endif // HAVE_PULSE

#if defined(__linux__)
	if (m_impl->backend.load() == BACKEND_NONE) {
		snd_pcm_t* handle = NULL;
		const char* alsa_device = device ? device : kDefaultAlsaDeviceName;
		unsigned rate = kCaptureRate;
		snd_pcm_uframes_t frame_size = kCaptureFrameSize;
		int dir = 0;

		int ret = snd_pcm_open(&handle, alsa_device, SND_PCM_STREAM_CAPTURE, 0);
		if (ret < 0) {
			fprintf(stderr, "ALSA: unable to open pcm device '%s': %s\n",
				alsa_device, snd_strerror(ret));
		}
		else {
			snd_pcm_hw_params_t* params;
			snd_pcm_hw_params_alloca(&params);
			snd_pcm_hw_params_any(handle, params);

			snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
			snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_FLOAT);
			snd_pcm_hw_params_set_channels(handle, params, 1);
			snd_pcm_hw_params_set_rate_near(handle, params, &rate, &dir);
			snd_pcm_hw_params_set_period_size_near(handle, params, &frame_size, &dir);

			ret = snd_pcm_hw_params(handle, params);
			if (ret < 0) {
				fprintf(stderr, "ALSA: unable to set hw parameters: %s\n", snd_strerror(ret));
				snd_pcm_close(handle);
				handle = NULL;
			}
			else {
				snd_pcm_hw_params_get_period_size(params, &frame_size, &dir);
				snd_pcm_hw_params_get_rate(params, &rate, &dir);

				if (frame_size == 0)
					frame_size = kCaptureFrameSize;

				m_impl->alsa = handle;
				m_impl->rate = rate;
				m_impl->frame_size = (unsigned)frame_size;
				m_impl->backend.store(BACKEND_ALSA);
				printf("ALSA rate: %u, frames: %u\n", rate, (unsigned int)frame_size);
			}
		}
	}
#endif // __linux__

	if (m_impl->backend.load() == BACKEND_NONE)
		return false;

	if (m_impl->frame_size == 0)
		m_impl->frame_size = kCaptureFrameSize;
	m_impl->frame_buffer.assign(m_impl->frame_size, 0.0f);

	// The DSP hop is one delivered block: one FFT per read, no timer coupling.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_dsp.Configure(m_impl->rate, (int)m_impl->frame_size)) {
			CloseBackend();
			return false;
		}
	}
	return true;
}

void AudioSampler::CloseBackend()
{
#ifdef HAVE_PULSE
	if (m_impl->pulse) {
		pa_simple_free(m_impl->pulse);
		m_impl->pulse = NULL;
	}
#endif

#if defined(__linux__)
	if (m_impl->alsa) {
		snd_pcm_close(m_impl->alsa);
		m_impl->alsa = NULL;
	}
#endif

	m_impl->backend.store(BACKEND_NONE);
}

bool AudioSampler::WaitWhileRunning(unsigned milliseconds)
{
	const unsigned kSlice = 25;
	unsigned waited = 0;
	while (waited < milliseconds) {
		if (!m_running.load())
			return false;
		const unsigned step = std::min(kSlice, milliseconds - waited);
		std::this_thread::sleep_for(std::chrono::milliseconds(step));
		waited += step;
	}
	return m_running.load();
}

AudioSampler::ReadStatus AudioSampler::ReadBlock()
{
	const int backend = m_impl->backend.load();
	const unsigned frames = m_impl->frame_size;

	if (frames == 0 || m_impl->frame_buffer.size() < frames)
		return READ_FATAL;

	float* buffer = &m_impl->frame_buffer[0];

	if (backend == BACKEND_PULSE) {
#ifdef HAVE_PULSE
		int error = 0;
		if (pa_simple_read(m_impl->pulse, buffer, (size_t)frames * sizeof(float), &error) < 0) {
			if (m_running.load())
				fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
			return READ_FATAL;
		}
		return READ_OK;
#else
		return READ_FATAL;
#endif
	}

	if (backend == BACKEND_ALSA) {
#if defined(__linux__)
		// A short read is normal, not an error: keep asking until the block is
		// full so the DSP always receives frames == configured hop.
		unsigned got = 0;
		while (got < frames) {
			snd_pcm_sframes_t ret = snd_pcm_readi(m_impl->alsa, buffer + got, frames - got);
			if (ret == -EPIPE) {
				// Overrun: the buffer was not read fast enough.  Recover and
				// drop this block, the next one is fine.
				snd_pcm_prepare(m_impl->alsa);
				return READ_RETRY;
			}
			if (ret == -ESTRPIPE) {
				// The device was suspended (e.g. the whole bus slept).
				if (snd_pcm_resume(m_impl->alsa) < 0)
					snd_pcm_prepare(m_impl->alsa);
				return READ_RETRY;
			}
			if (ret < 0) {
				fprintf(stderr, "ALSA error from read: %s\n", snd_strerror((int)ret));
				return READ_FATAL;
			}
			if (ret == 0)
				return READ_RETRY;
			got += (unsigned)ret;
		}
		return READ_OK;
#else
		return READ_FATAL;
#endif
	}

	return READ_FATAL;
}

void AudioSampler::CaptureLoop()
{
	unsigned delay_ms = kReconnectInitialDelayMs;
	unsigned long failures = 0;

	while (m_running.load()) {
		const ReadStatus status = ReadBlock();

		if (status == READ_OK) {
			if (failures > 0)
				fprintf(stderr, "AudioSampler: capture recovered after %lu failed attempt(s)\n", failures);
			failures = 0;
			delay_ms = kReconnectInitialDelayMs;

			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_dsp.IsConfigured())
				m_dsp.Push(&m_impl->frame_buffer[0], m_impl->frame_buffer.size());
			continue;
		}

		if (status == READ_RETRY) {
			// Transient (ALSA overrun/suspend): the stream is still valid.
			WaitWhileRunning(2);
			continue;
		}

		// READ_FATAL: the sound server or the device went away.  Tear the
		// stream down and try to reopen it with exponential backoff, so a
		// PipeWire restart or a replugged interface recovers on its own
		// instead of freezing the bars forever.
		++failures;
		m_open.store(false);
		CloseBackend();

		if (failures == 1 || (failures % kReconnectLogEvery) == 0) {
			fprintf(stderr, "AudioSampler: capture lost, reconnecting (attempt %lu, retry in %u ms)\n",
				failures, delay_ms);
		}

		if (!WaitWhileRunning(delay_ms))
			break;

		delay_ms = std::min(delay_ms * 2, kReconnectMaxDelayMs);

		if (OpenBackend(m_source_name)) {
			m_open.store(true);
			m_reconnects.fetch_add(1);
			fprintf(stderr, "AudioSampler: reconnected to '%s' (%s)\n",
				m_source_name.empty() ? "(default)" : m_source_name.c_str(), GetBackendName());
			failures = 0;
			delay_ms = kReconnectInitialDelayMs;
		}
	}
}

void AudioSampler::CopyMagnitudes(float* out, int count)
{
	if (!out || count <= 0)
		return;

	std::lock_guard<std::mutex> lock(m_mutex);

	for (int i = 0; i < count; i++)
		out[i] = 0.0f;

	if (!m_dsp.IsConfigured())
		return;

	const float* src = m_dsp.Magnitudes();
	const int n = std::min(count, m_dsp.NumBars());
	for (int i = 0; i < n; i++)
		out[i] = src[i];
}
