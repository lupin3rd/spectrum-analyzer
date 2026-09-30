#ifndef __AUDIO_SAMPLER_H__
#define __AUDIO_SAMPLER_H__

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define ALSA_PCM_NEW_HW_PARAMS_API		// use new API
#include <alsa/asoundlib.h>
#include <fftw3.h>

#ifdef HAVE_PULSE
#include <pulse/simple.h>
#endif

// ---------------------------------------------------------------------------
// Spectrum analysis configuration
// ---------------------------------------------------------------------------

#define AUDIO_FFT_SIZE		(4096)		// analysis window, in samples (~93 ms)
#define AUDIO_MIN_FREQ		(20.0)		// lowest displayed band, Hz
#define AUDIO_MAX_FREQ		(20000.0)	// highest displayed band, Hz
#define AUDIO_MIN_DB		(-80.0)		// bottom of the display
#define AUDIO_MAX_DB		(0.0)		// top of the display (full scale)

// Envelope-follower time constants, in seconds.  Fast attack so transients
// are visible, slow release so the bars do not flicker.
#define AUDIO_ATTACK_SECONDS	(0.010)
#define AUDIO_RELEASE_SECONDS	(0.250)

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
	virtual ~AudioSampler();

	// List available capture sources.  Prefers PulseAudio when it is
	// available (it works on both PulseAudio and PipeWire via
	// pipewire-pulse); otherwise falls back to ALSA device names.
	static std::vector<AudioSourceInfo> EnumerateSources();

	// Open a capture source.  NULL or "" means "default".
	// Tries PulseAudio first, then falls back to ALSA.
	// Capture runs on its own thread, so this never blocks the UI loop.
	bool Open(const char* source_name);
	void Close();

	bool IsOpen() const;
	const char* GetBackendName() const;

	// Copy the latest bar magnitudes (thread-safe snapshot) into out[].
	void CopyMagnitudes(float* out, int count);

private:
	bool OpenPulse(const char* source_name);
	bool OpenAlsa(const char* source_name);
	void ClosePulse();
	void CloseAlsa();
	bool AllocateFftBuffers();
	void FreeFftBuffers();

	void CaptureLoop();   // runs on the audio thread
	bool Update();        // one blocking read (audio thread)
	void Analyze();       // window + FFT + bands -> m_bar_magnitudes (audio thread)

	enum Backend { BACKEND_NONE, BACKEND_PULSE, BACKEND_ALSA };
	Backend m_backend;

	unsigned int m_rate;
	snd_pcm_uframes_t m_frame_size;
	snd_pcm_t* m_alsa;
	float* m_frame_buffer;

#ifdef HAVE_PULSE
	pa_simple* m_pulse;
#endif

	// Overlapping analysis window.  Audio is read in small blocks and pushed
	// into this sliding buffer; one FFT is run per block on the most recent
	// AUDIO_FFT_SIZE samples.
	float* m_window;
	int m_window_fill;

	// Hann window coefficients and log-spaced band table.
	double m_hann[AUDIO_FFT_SIZE];
	std::vector<float> m_band_edges_hz;
	std::vector<int> m_band_bin_lo;
	std::vector<int> m_band_bin_hi;

	fftw_plan m_fftw_plan;
	double* m_fft_in;
	fftw_complex* m_fft_out;

	float* m_bar_magnitudes;         // shared, smoothed, 0..1
	std::vector<float> m_raw_magnitudes;  // audio-thread only

	double m_attack_coeff;
	double m_release_coeff;

	int m_overrun_count;
	int m_underrun_count;

	std::thread m_thread;
	std::atomic<bool> m_running;
	std::mutex m_magnitudes_mutex;
};

#endif
