// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "audio-sampler.h"

#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "spectrum-dsp.h"

namespace {

// Frames requested per callback.  Small enough for ~6 ms of latency at 44.1 kHz
// (which is what makes the bars move smoothly), large enough not to burn CPU.
const ma_uint32 kPeriodSizeInFrames = 256;

// Reconnect policy when the stream dies.
const unsigned kReconnectInitialDelayMs = 100;
const unsigned kReconnectMaxDelayMs = 2000;
const unsigned long kReconnectLogEvery = 10;

// Suffix PulseAudio gives to the monitor source of every sink: capturing from
// one of these analyzes what is playing instead of what the microphone hears.
const char* kMonitorSuffix = ".monitor";

bool looks_like_monitor(const char* name)
{
	if (!name)
		return false;
	const size_t name_len = std::strlen(name);
	const size_t suffix_len = std::strlen(kMonitorSuffix);
	return name_len > suffix_len
		&& std::strcmp(name + name_len - suffix_len, kMonitorSuffix) == 0;
}

// Device ids are opaque binary blobs whose layout depends on the backend, so
// they are carried around as hex.  Trailing zero bytes are dropped to keep the
// string short (a PulseAudio id is an ASCII name padded with zeros).
std::string device_id_to_string(const ma_device_id& id)
{
	const unsigned char* bytes = (const unsigned char*)&id;
	std::size_t used = sizeof(id);
	while (used > 0 && bytes[used - 1] == 0)
		used--;

	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(used * 2);
	for (std::size_t i = 0; i < used; i++) {
		out.push_back(hex[bytes[i] >> 4]);
		out.push_back(hex[bytes[i] & 0x0f]);
	}
	return out;
}

int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool string_to_device_id(const std::string& text, ma_device_id* out_id)
{
	if (text.empty() || text.size() % 2 != 0 || text.size() / 2 > sizeof(*out_id))
		return false;

	std::memset(out_id, 0, sizeof(*out_id));
	unsigned char* bytes = (unsigned char*)out_id;

	for (std::size_t i = 0; i < text.size() / 2; i++) {
		const int hi = hex_value(text[2 * i]);
		const int lo = hex_value(text[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		bytes[i] = (unsigned char)((hi << 4) | lo);
	}
	return true;
}

// One ordered list of backends for every platform: the first one that
// initialises wins.  PulseAudio comes first on purpose (see kMonitorSuffix).
unsigned build_backend_list(ma_backend* backends, unsigned capacity)
{
	unsigned count = 0;
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	if (count < capacity) backends[count++] = ma_backend_pulseaudio;
	if (count < capacity) backends[count++] = ma_backend_alsa;
	if (count < capacity) backends[count++] = ma_backend_jack;
	if (count < capacity) backends[count++] = ma_backend_sndio;
#elif defined(__APPLE__)
	if (count < capacity) backends[count++] = ma_backend_coreaudio;
#elif defined(_WIN32)
	if (count < capacity) backends[count++] = ma_backend_wasapi;
#endif
	if (count < capacity) backends[count++] = ma_backend_null;
	return count;
}

bool init_context(ma_context* context)
{
	ma_backend backends[8];
	const unsigned count = build_backend_list(backends, 8);
	return ma_context_init(backends, count, NULL, context) == MA_SUCCESS;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Capture state.  Defined here so no audio header leaks into the interface.
// ---------------------------------------------------------------------------

struct AudioSampler::Impl
{
	ma_context context;
	ma_device device;
	bool context_ready;
	bool device_ready;
	ma_backend backend;

	// The device currently open, kept for the reconnect path.
	std::string source_name;

	// Set by the notification callback, consumed by Poll().
	std::atomic<bool> needs_reopen;
	std::atomic<bool> closing;

	// Backoff between reopen attempts.
	std::chrono::steady_clock::time_point next_attempt;
	unsigned delay_ms;
	unsigned long attempts;

	// Guards dsp: written by the miniaudio capture thread, read by the UI.
	std::mutex mutex;
	sa::SpectrumDsp dsp;

	Impl()
		: context_ready(false),
			device_ready(false),
			backend(ma_backend_null),
			needs_reopen(false),
			closing(false),
			delay_ms(kReconnectInitialDelayMs),
			attempts(0)
	{
		std::memset(&context, 0, sizeof(context));
		std::memset(&device, 0, sizeof(device));
	}
};

namespace {

void ma_data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
	(void)output;
	if (!input || frame_count == 0)
		return;

	AudioSampler::Impl* self = (AudioSampler::Impl*)device->pUserData;
	if (!self)
		return;

	std::lock_guard<std::mutex> lock(self->mutex);
	if (self->dsp.IsConfigured())
		self->dsp.Push((const float*)input, (std::size_t)frame_count);
}

void ma_notification_callback(const ma_device_notification* notification)
{
	if (!notification || !notification->pDevice)
		return;

	AudioSampler::Impl* self = (AudioSampler::Impl*)notification->pDevice->pUserData;
	if (!self)
		return;

	switch (notification->type) {
		case ma_device_notification_type_interruption_began:
		case ma_device_notification_type_interruption_ended:
		case ma_device_notification_type_rerouted:
			self->needs_reopen.store(true);
			break;

		case ma_device_notification_type_stopped:
			// Only a problem when we did not stop it ourselves: this also
			// fires on a normal ma_device_uninit().
			if (!self->closing.load())
				self->needs_reopen.store(true);
			break;

		case ma_device_notification_type_started:
		case ma_device_notification_type_unlocked:
		default:
			break;
	}
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// AudioSampler
// ---------------------------------------------------------------------------

AudioSampler::AudioSampler()
	: m_impl(new Impl()),
		m_open(false),
		m_reconnects(0)
{
}

AudioSampler::~AudioSampler()
{
	Close();
	if (m_impl->context_ready) {
		ma_context_uninit(&m_impl->context);
		m_impl->context_ready = false;
	}
}

std::vector<AudioSourceInfo> AudioSampler::EnumerateSources()
{
	std::vector<AudioSourceInfo> sources;

	AudioSourceInfo def;
	def.id = "";
	def.description = "Default";
	def.is_monitor = false;
	sources.push_back(def);

	ma_context context;
	if (!init_context(&context)) {
		fprintf(stderr, "AudioSampler: no audio backend available\n");
		return sources;
	}

	ma_device_info* playback = NULL;
	ma_uint32 playback_count = 0;
	ma_device_info* capture = NULL;
	ma_uint32 capture_count = 0;

	if (ma_context_get_devices(&context, &playback, &playback_count, &capture, &capture_count) == MA_SUCCESS) {
		for (ma_uint32 i = 0; i < capture_count; i++) {
			AudioSourceInfo info;
			info.id = device_id_to_string(capture[i].id);
			info.description = capture[i].name;
			info.is_monitor = looks_like_monitor(capture[i].name);
			sources.push_back(info);
		}
	}

	ma_context_uninit(&context);
	return sources;
}

bool AudioSampler::Open(const std::string& source_name)
{
	// Release the previous device first so the capture thread is gone before the
	// device struct is reused.
	CloseDevice();

	if (!OpenDevice(source_name)) {
		fprintf(stderr, "AudioSampler: could not open capture device '%s'\n",
			source_name.empty() ? "(default)" : source_name.c_str());
		return false;
	}

	m_open.store(true);
	return true;
}

void AudioSampler::Close()
{
	CloseDevice();
	m_open.store(false);
}

const char* AudioSampler::GetBackendName() const
{
	if (!m_open.load())
		return "none";
	return ma_get_backend_name(m_impl->backend);
}

int AudioSampler::NumBars() const
{
	return m_impl->dsp.NumBars();
}

bool AudioSampler::OpenDevice(const std::string& source_name)
{
	Impl* self = m_impl.get();

	if (!self->context_ready) {
		if (!init_context(&self->context)) {
			fprintf(stderr, "AudioSampler: no audio backend available\n");
			return false;
		}
		self->context_ready = true;
	}

	ma_device_config config = ma_device_config_init(ma_device_type_capture);
	config.capture.format = ma_format_f32;
	config.capture.channels = 1;		// mono: miniaudio downmixes for us
	config.sampleRate = 0;			// native rate: no resampling in the way
	config.periodSizeInFrames = kPeriodSizeInFrames;
	config.dataCallback = ma_data_callback;
	config.notificationCallback = ma_notification_callback;
	config.pUserData = self;

	ma_device_id device_id;
	if (!source_name.empty()) {
		if (!string_to_device_id(source_name, &device_id)) {
			fprintf(stderr, "AudioSampler: malformed device id\n");
			return false;
		}
		config.capture.pDeviceID = &device_id;
	}

	const ma_result result = ma_device_init(&self->context, &config, &self->device);
	if (result != MA_SUCCESS) {
		fprintf(stderr, "AudioSampler: ma_device_init failed: %s\n", ma_result_description(result));
		return false;
	}
	self->device_ready = true;
	self->backend = self->context.backend;
	self->source_name = source_name;
	self->closing.store(false);
	self->needs_reopen.store(false);

	const unsigned rate = self->device.sampleRate > 0 ? self->device.sampleRate : 44100;
	{
		std::lock_guard<std::mutex> lock(self->mutex);
		if (!self->dsp.Configure(rate, (int)kPeriodSizeInFrames)) {
			CloseDevice();
			return false;
		}
	}

	if (ma_device_start(&self->device) != MA_SUCCESS) {
		fprintf(stderr, "AudioSampler: ma_device_start failed\n");
		CloseDevice();
		return false;
	}

	printf("Audio backend: %s, %u Hz, %u frames\n",
		ma_get_backend_name(self->backend), rate, (unsigned)kPeriodSizeInFrames);
	return true;
}

void AudioSampler::CloseDevice()
{
	Impl* self = m_impl.get();

	if (self->device_ready) {
		// Tell the notification callback that this stop is intentional, so it
		// does not schedule a reconnect for a device we are closing.
		self->closing.store(true);
		ma_device_uninit(&self->device);
		self->device_ready = false;
		self->closing.store(false);
	}

	{
		std::lock_guard<std::mutex> lock(self->mutex);
		self->dsp.Reset();
	}

	self->needs_reopen.store(false);
}

void AudioSampler::Poll()
{
	Impl* self = m_impl.get();

	// A stream that never opened is the front end's business (it retries with
	// its own pacing); here we only fix a stream that was running and died.
	if (!self->needs_reopen.load() || !m_open.load())
		return;

	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now < self->next_attempt)
		return;

	self->attempts++;
	if (self->attempts == 1 || (self->attempts % kReconnectLogEvery) == 0) {
		fprintf(stderr, "AudioSampler: capture lost, reopening '%s' (attempt %lu)\n",
			self->source_name.empty() ? "(default)" : self->source_name.c_str(), self->attempts);
	}

	// CloseDevice() clears needs_reopen, so remember what we are recovering from.
	if (OpenDevice(self->source_name)) {
		m_reconnects.fetch_add(1);
		self->attempts = 0;
		self->delay_ms = kReconnectInitialDelayMs;
		self->next_attempt = now;
		fprintf(stderr, "AudioSampler: reconnected (%s)\n", GetBackendName());
	}
	else {
		m_open.store(false);
		self->needs_reopen.store(true);
		self->delay_ms = std::min(self->delay_ms * 2, kReconnectMaxDelayMs);
		self->next_attempt = now + std::chrono::milliseconds(self->delay_ms);
	}
}

void AudioSampler::CopyMagnitudes(float* out, int count)
{
	if (!out || count <= 0)
		return;

	std::lock_guard<std::mutex> lock(m_impl->mutex);

	for (int i = 0; i < count; i++)
		out[i] = 0.0f;

	if (!m_impl->dsp.IsConfigured())
		return;

	const float* src = m_impl->dsp.Magnitudes();
	const int n = std::min(count, m_impl->dsp.NumBars());
	for (int i = 0; i < n; i++)
		out[i] = src[i];
}
