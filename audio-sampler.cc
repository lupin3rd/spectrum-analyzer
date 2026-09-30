#include "audio-sampler.h"
#include "spectrum-analyzer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_PULSE
#include <pulse/pulseaudio.h>
#endif

#define NUM_CHANNELS (1)
#define DEFAULT_ALSA_DEVICE_NAME ("default")
#define PULSE_RATE (44100)
#define PULSE_FRAME_SIZE (256)

// ---------------------------------------------------------------------------
// Source enumeration
// ---------------------------------------------------------------------------

static std::string first_line(const char* s)
{
	if (!s)
		return "";
	std::string str(s);
	size_t nl = str.find('\n');
	if (nl != std::string::npos)
		str.erase(nl);
	return str;
}

#ifdef HAVE_PULSE
namespace {

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
		usleep(1000);
	}

	pa_context_disconnect(c);
	pa_context_unref(c);
	pa_mainloop_free(ml);

	return sources;
}

} // anonymous namespace
#endif // HAVE_PULSE

namespace {

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
			bool is_input = (ioid == NULL)
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// AudioSampler
// ---------------------------------------------------------------------------

AudioSampler::AudioSampler()
	: m_backend(BACKEND_NONE),
		m_rate(PULSE_RATE),
		m_frame_size(PULSE_FRAME_SIZE),
		m_alsa(NULL),
		m_frame_buffer(NULL),
#ifdef HAVE_PULSE
		m_pulse(NULL),
#endif
		m_window(NULL),
		m_window_fill(0),
		m_fftw_plan(NULL),
		m_fft_in(NULL),
		m_fft_out(NULL),
		m_bar_magnitudes(NULL),
		m_attack_coeff(1.0),
		m_release_coeff(1.0),
		m_overrun_count(0),
		m_underrun_count(0),
		m_running(false)
{
	// Allocate the magnitude array up-front so CopyMagnitudes() is always
	// valid, even before a source is opened.
	m_bar_magnitudes = (float*)calloc(NUM_BARS, sizeof(float));
}

AudioSampler::~AudioSampler()
{
	Close();
	if (m_bar_magnitudes) {
		free(m_bar_magnitudes);
		m_bar_magnitudes = NULL;
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

#ifdef HAVE_PULSE
	std::vector<AudioSourceInfo> pulse_sources = pulse_enumerate_sources();
	if (!pulse_sources.empty()) {
		sources.insert(sources.end(), pulse_sources.begin(), pulse_sources.end());
		return sources;
	}
#endif

	std::vector<AudioSourceInfo> alsa_sources = alsa_enumerate_sources();
	sources.insert(sources.end(), alsa_sources.begin(), alsa_sources.end());
	return sources;
}

bool AudioSampler::Open(const char* source_name)
{
	Close();

#ifdef HAVE_PULSE
	if (OpenPulse(source_name)) {
		m_backend = BACKEND_PULSE;
		if (!AllocateFftBuffers()) {
			Close();
			return false;
		}
		printf("Audio backend: PulseAudio (source: %s)\n", (source_name && source_name[0]) ? source_name : "default");
	}
	else
#endif
	if (OpenAlsa(source_name)) {
		m_backend = BACKEND_ALSA;
		if (!AllocateFftBuffers()) {
			Close();
			return false;
		}
	}
	else {
		return false;
	}

	// Capture runs on its own thread so the UI main loop is never blocked.
	m_running = true;
	m_thread = std::thread(&AudioSampler::CaptureLoop, this);
	return true;
}

void AudioSampler::Close()
{
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();

	ClosePulse();
	CloseAlsa();
	FreeFftBuffers();
	m_backend = BACKEND_NONE;
}

bool AudioSampler::IsOpen() const
{
	return m_backend != BACKEND_NONE;
}

const char* AudioSampler::GetBackendName() const
{
	switch (m_backend) {
		case BACKEND_PULSE: return "PulseAudio";
		case BACKEND_ALSA:  return "ALSA";
		default:            return "none";
	}
}

void AudioSampler::CaptureLoop()
{
	while (m_running) {
		if (Update())
			Analyze();
		else
			usleep(1000);		// don't spin on read errors
	}
}

bool AudioSampler::OpenPulse(const char* source_name)
{
#ifdef HAVE_PULSE
	pa_sample_spec ss;
	ss.format = PA_SAMPLE_FLOAT32LE;
	ss.rate = PULSE_RATE;
	ss.channels = NUM_CHANNELS;

	const char* dev = (source_name && source_name[0]) ? source_name : NULL;

	// Ask for a low-latency record stream.  With the default (server chosen)
	// buffer attributes PulseAudio hands the data over in large, bursty
	// chunks: dozens of reads return instantly and are then followed by a
	// gap of a few hundred milliseconds.  The analyzer bars therefore freeze
	// and then jump instead of moving smoothly.  Requesting one fragment per
	// period (and a small overall buffer) makes the reads arrive at a steady
	// rate again.
	pa_buffer_attr attr;
	attr.tlength   = (uint32_t)-1;   // playback only
	attr.prebuf    = (uint32_t)-1;   // playback only
	attr.minreq    = (uint32_t)-1;   // playback only
	attr.maxlength = (uint32_t)(PULSE_FRAME_SIZE * NUM_CHANNELS * sizeof(float) * 4);
	attr.fragsize  = (uint32_t)(PULSE_FRAME_SIZE * NUM_CHANNELS * sizeof(float));

	int error = 0;
	m_pulse = pa_simple_new(
		NULL,                          // default server (PulseAudio or pipewire-pulse)
		"Spectrum Analyzer",           // application name
		PA_STREAM_RECORD,              // direction
		dev,                           // source name (NULL = default)
		"Spectrum Analyzer Input",     // stream description
		&ss,                           // sample format
		NULL,                          // channel map (default)
		&attr,                         // low-latency buffer attributes
		&error);

	if (!m_pulse) {
		// A few servers reject very small buffers.  Rather than giving up on
		// PulseAudio entirely, retry with the server default; smooth motion
		// is lost, but capture still works.
		fprintf(stderr, "PulseAudio: low-latency buffer rejected (%s); retrying with defaults\n", pa_strerror(error));
		m_pulse = pa_simple_new(
			NULL, "Spectrum Analyzer", PA_STREAM_RECORD, dev,
			"Spectrum Analyzer Input", &ss, NULL, NULL, &error);
	}

	if (!m_pulse) {
		fprintf(stderr, "PulseAudio: %s\n", pa_strerror(error));
		return false;
	}

	m_rate = ss.rate;
	m_frame_size = PULSE_FRAME_SIZE;
	return true;
#else
	(void)source_name;
	return false;
#endif
}

bool AudioSampler::OpenAlsa(const char* source_name)
{
	int ret;
	int dir = 0;

	const char* device = (source_name && source_name[0]) ? source_name : DEFAULT_ALSA_DEVICE_NAME;

	m_rate = PULSE_RATE;
	m_frame_size = PULSE_FRAME_SIZE;

	ret = snd_pcm_open(&m_alsa, device, SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		fprintf(stderr, "ALSA: unable to open pcm device '%s': %s\n", device, snd_strerror(ret));
		return false;
	}

	// Get default params
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(m_alsa, params);

	// Set desired params
	snd_pcm_hw_params_set_access(m_alsa, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(m_alsa, params, SND_PCM_FORMAT_FLOAT);
	snd_pcm_hw_params_set_channels(m_alsa, params, NUM_CHANNELS);
	snd_pcm_hw_params_set_rate_near(m_alsa, params, &m_rate, &dir);
	snd_pcm_hw_params_set_period_size_near(m_alsa, params, &m_frame_size, &dir);

	// Write the parameters to the driver
	ret = snd_pcm_hw_params(m_alsa, params);
	if (ret < 0) {
		fprintf(stderr, "ALSA: unable to set hw parameters: %s\n", snd_strerror(ret));
		snd_pcm_close(m_alsa);
		m_alsa = NULL;
		return false;
	}
	snd_pcm_hw_params_get_period_size(params, &m_frame_size, &dir);
	snd_pcm_hw_params_get_rate(params, &m_rate, &dir);

	printf("ALSA rate: %u, frames: %u\n", m_rate, (unsigned int)m_frame_size);
	return true;
}

void AudioSampler::ClosePulse()
{
#ifdef HAVE_PULSE
	if (m_pulse) {
		pa_simple_free(m_pulse);
		m_pulse = NULL;
	}
#endif
}

void AudioSampler::CloseAlsa()
{
	if (m_alsa) {
		snd_pcm_close(m_alsa);
		m_alsa = NULL;
	}
}

bool AudioSampler::AllocateFftBuffers()
{
	FreeFftBuffers();

	m_frame_buffer = (float*)malloc(m_frame_size * sizeof(float));
	m_window = (float*)calloc(AUDIO_FFT_SIZE, sizeof(float));
	m_fft_in = (double*)fftw_malloc(AUDIO_FFT_SIZE * sizeof(double));
	m_fft_out = (fftw_complex*)fftw_malloc((AUDIO_FFT_SIZE / 2 + 1) * sizeof(fftw_complex));

	if (!m_frame_buffer || !m_window || !m_fft_in || !m_fft_out) {
		FreeFftBuffers();
		return false;
	}

	// Real-to-complex FFT of the analysis window.  FFTW reads m_fft_in and
	// writes m_fft_out directly, so the buffers are passed here.
	m_fftw_plan = fftw_plan_dft_r2c_1d(AUDIO_FFT_SIZE, m_fft_in, m_fft_out, FFTW_ESTIMATE);
	if (!m_fftw_plan) {
		FreeFftBuffers();
		return false;
	}

	// Periodic Hann window: drastically reduces spectral leakage, so a strong
	// bass tone no longer smears across every bar.
	static const double pi = 3.14159265358979323846;
	for (int i = 0; i < AUDIO_FFT_SIZE; i++)
		m_hann[i] = 0.5 - 0.5 * cos(2.0 * pi * (double)i / (double)AUDIO_FFT_SIZE);

	// Log-spaced band edges from AUDIO_MIN_FREQ up to AUDIO_MAX_FREQ (clamped
	// to Nyquist).  Equal ratio between adjacent bands, like fractional-octave
	// bands, which matches how music is perceived.
	double fmin = AUDIO_MIN_FREQ;
	double fmax = AUDIO_MAX_FREQ;
	double nyquist = 0.5 * (double)m_rate;
	if (fmax > nyquist)
		fmax = nyquist;

	m_band_edges_hz.resize(NUM_BARS + 1);
	double ratio = pow(fmax / fmin, 1.0 / (double)NUM_BARS);
	for (int i = 0; i <= NUM_BARS; i++)
		m_band_edges_hz[i] = (float)(fmin * pow(ratio, (double)i));

	// Map each band to its FFT bin range.
	m_band_bin_lo.resize(NUM_BARS);
	m_band_bin_hi.resize(NUM_BARS);
	const int half = AUDIO_FFT_SIZE / 2;
	for (int b = 0; b < NUM_BARS; b++) {
		double f0 = m_band_edges_hz[b];
		double f1 = m_band_edges_hz[b + 1];
		int klo = (int)ceil(f0 * (double)AUDIO_FFT_SIZE / (double)m_rate);
		int khi = (int)floor(f1 * (double)AUDIO_FFT_SIZE / (double)m_rate);
		if (klo < 1) klo = 1;
		if (khi > half) khi = half;
		if (khi < klo) {
			// Band narrower than one bin: use the closest single bin.
			int k = (int)lround(0.5 * (f0 + f1) * (double)AUDIO_FFT_SIZE / (double)m_rate);
			if (k < 1) k = 1;
			if (k > half) k = half;
			klo = khi = k;
		}
		m_band_bin_lo[b] = klo;
		m_band_bin_hi[b] = khi;
	}

	m_raw_magnitudes.assign(NUM_BARS, 0.0f);

	// Envelope-follower coefficients for the real analysis interval (one FFT
	// per captured block).
	double dt = (double)m_frame_size / (double)m_rate;
	m_attack_coeff = 1.0 - exp(-dt / AUDIO_ATTACK_SECONDS);
	m_release_coeff = 1.0 - exp(-dt / AUDIO_RELEASE_SECONDS);

	m_window_fill = 0;
	return true;
}

void AudioSampler::FreeFftBuffers()
{
	if (m_fftw_plan) {
		fftw_destroy_plan(m_fftw_plan);
		m_fftw_plan = NULL;
	}
	if (m_fft_in) {
		fftw_free(m_fft_in);
		m_fft_in = NULL;
	}
	if (m_fft_out) {
		fftw_free(m_fft_out);
		m_fft_out = NULL;
	}
	if (m_window) {
		free(m_window);
		m_window = NULL;
	}
	if (m_frame_buffer) {
		free(m_frame_buffer);
		m_frame_buffer = NULL;
	}
	m_window_fill = 0;
}

bool AudioSampler::Update()
{
	if (m_backend == BACKEND_PULSE) {
#ifdef HAVE_PULSE
		int error = 0;
		if (pa_simple_read(m_pulse, m_frame_buffer, m_frame_size * sizeof(float), &error) < 0) {
			fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
			return false;
		}
		return true;
#else
		return false;
#endif
	}
	else if (m_backend == BACKEND_ALSA) {
		int ret = snd_pcm_readi(m_alsa, m_frame_buffer, m_frame_size);
		if (ret == -EPIPE) {
			// EPIPE means overrun
			m_overrun_count++;
			snd_pcm_prepare(m_alsa);
			return false;
		} else if (ret < 0) {
			fprintf(stderr, "ALSA error from read: %s\n", snd_strerror(ret));
			return false;
		} else if (ret != (int)m_frame_size) {
			fprintf(stderr, "ALSA short read, read %d frames\n", ret);
			return false;
		}
		return true;
	}
	return false;
}

void AudioSampler::Analyze()
{
	if (!m_frame_buffer || !m_window || !m_fft_in || !m_fft_out || !m_bar_magnitudes)
		return;

	// Slide the freshly captured block into the analysis window, dropping the
	// oldest samples.  With a 256-frame block this is 87.5% overlap, so the
	// spectrum is recomputed ~172 times per second and moves smoothly.
	const int n = (int)m_frame_size;
	if (n >= AUDIO_FFT_SIZE) {
		memcpy(m_window, m_frame_buffer + (n - AUDIO_FFT_SIZE), AUDIO_FFT_SIZE * sizeof(float));
		m_window_fill = AUDIO_FFT_SIZE;
	}
	else {
		if (m_window_fill + n > AUDIO_FFT_SIZE) {
			int drop = m_window_fill + n - AUDIO_FFT_SIZE;
			memmove(m_window, m_window + drop, (m_window_fill - drop) * sizeof(float));
			m_window_fill -= drop;
		}
		memcpy(m_window + m_window_fill, m_frame_buffer, n * sizeof(float));
		m_window_fill += n;
	}

	if (m_window_fill < AUDIO_FFT_SIZE)
		return;		// still filling the very first window

	// Apply the Hann window and transform.
	for (int i = 0; i < AUDIO_FFT_SIZE; i++)
		m_fft_in[i] = (double)m_window[i] * m_hann[i];

	fftw_execute(m_fftw_plan);

	// Sum the power in each log band.  Summing (rather than averaging) gives a
	// flat response for pink noise, i.e. equal energy per octave.
	//
	// The 16/N^2 factor calibrates a full-scale (amplitude 1.0) sine, seen
	// through a Hann window, to roughly 0 dBFS.
	const double scale = 16.0 / ((double)AUDIO_FFT_SIZE * (double)AUDIO_FFT_SIZE);
	for (int b = 0; b < NUM_BARS; b++) {
		double power = 0.0;
		for (int k = m_band_bin_lo[b]; k <= m_band_bin_hi[b]; k++) {
			power += m_fft_out[k][0] * m_fft_out[k][0]
			       + m_fft_out[k][1] * m_fft_out[k][1];
		}
		power *= scale;

		double db = 10.0 * log10(power + 1e-12);
		float target = (float)((db - AUDIO_MIN_DB) / (AUDIO_MAX_DB - AUDIO_MIN_DB));
		if (target < 0.0f) target = 0.0f;
		if (target > 1.0f) target = 1.0f;
		m_raw_magnitudes[b] = target;
	}

	// Attack/release envelope follower so the bars move fluidly instead of
	// flickering with every FFT frame.
	std::lock_guard<std::mutex> lock(m_magnitudes_mutex);
	for (int b = 0; b < NUM_BARS; b++) {
		float& cur = m_bar_magnitudes[b];
		float target = m_raw_magnitudes[b];
		double coeff = (target > cur) ? m_attack_coeff : m_release_coeff;
		cur = (float)(cur + coeff * (target - cur));
	}
}

void AudioSampler::CopyMagnitudes(float* out, int count)
{
	std::lock_guard<std::mutex> lock(m_magnitudes_mutex);
	for (int i = 0; i < count && i < NUM_BARS; i++)
		out[i] = m_bar_magnitudes[i];
}
