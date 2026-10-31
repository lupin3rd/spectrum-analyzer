// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __SPECTRUM_DSP_H__
#define __SPECTRUM_DSP_H__

// Portable spectrum analysis core.
//
// This file contains no I/O, no threads, no platform headers and no GUI code:
// it turns a stream of mono float samples into the NUM_BARS smoothed bar
// magnitudes that the UI draws.  Everything the application does that is
// platform specific (capture, windowing, OSC) lives somewhere else, which
// keeps this core testable and reusable on any OS.
//
// The only external dependency is the FFT.  It is confined to spectrum-dsp.cc
// behind two opaque members (m_fft_in/m_fft_out/m_plan), so replacing FFTW
// with a permissively licensed implementation such as PocketFFT is a
// single-file change.

#include <cstddef>
#include <vector>

namespace sa {

// Fixed analysis geometry.  Kept as constants (not #defines) so they are
// usable from templates and tests.
enum : int {
	kFftSize = 4096,	// analysis window in samples (~93 ms at 44.1 kHz)
	kNumBars = 12		// log-spaced display bands
};

struct AnalysisConfig {
	// Analysis window length in samples.  Must be a power of two.
	int fft_size = kFftSize;
	// Number of log-spaced bands drawn on screen.
	int num_bars = kNumBars;
	// Lowest / highest displayed band, in Hz.  The upper edge is clamped to
	// the Nyquist frequency of the configured sample rate.
	double min_freq = 20.0;
	double max_freq = 20000.0;
	// Vertical scale: min_db maps to 0.0, max_db maps to 1.0.
	double min_db = -80.0;
	double max_db = 0.0;
	// Envelope follower time constants, in seconds.  Fast attack so transients
	// are visible, slow release so the bars do not flicker.
	double attack_seconds = 0.010;
	double release_seconds = 0.250;
};

class SpectrumDsp
{
public:
	SpectrumDsp();
	explicit SpectrumDsp(const AnalysisConfig& config);
	~SpectrumDsp();

	// Owns FFT buffers and a plan: not copyable, not movable.
	SpectrumDsp(const SpectrumDsp&) = delete;
	SpectrumDsp& operator=(const SpectrumDsp&) = delete;

	const AnalysisConfig& Config() const { return m_config; }

	// (Re)build the window coefficients, the band table and the FFT plan for
	// the given sample rate and the number of samples delivered per Push().
	// Returns false only if the FFT plan could not be created.
	bool Configure(unsigned rate, int hop_size);

	// Zero the envelope follower and the analysis window.
	void Reset();

	// Feed `count` mono samples (values nominally in [-1, 1]).  Exactly one
	// spectrum is computed per call, over the most recent fft_size samples,
	// once that many have been accumulated.  Returns 1 if a spectrum was
	// computed, 0 otherwise.
	int Push(const float* samples, std::size_t count);

	// Smoothed bar magnitudes in [0, 1].  Valid for NumBars() entries.
	const float* Magnitudes() const { return &m_bar_magnitudes[0]; }

	// Unsmoothed (instantaneous) bar magnitudes in [0, 1].  For tests/diagnostics.
	const std::vector<float>& RawMagnitudes() const { return m_raw_magnitudes; }

	// Band edges in Hz: num_bars + 1 ascending values.  For tests/labels.
	const std::vector<double>& BandEdgesHz() const { return m_band_edges_hz; }

	// FFT bin range [lo, hi] (inclusive) mapped to each band.  For tests.
	const std::vector<int>& BandBinLow() const { return m_band_bin_lo; }
	const std::vector<int>& BandBinHigh() const { return m_band_bin_hi; }

	int NumBars() const { return m_config.num_bars; }
	int FftSize() const { return m_config.fft_size; }
	unsigned Rate() const { return m_rate; }
	int HopSize() const { return m_hop; }
	bool IsConfigured() const { return m_plan != NULL; }

	// Index of the band whose edges contain `hz`, or -1 if outside the range.
	// Used by the tests, and handy for future frequency labels on screen.
	int BandForFrequency(double hz) const;

private:
	void ReleasePlan();
	void BuildWindow();
	void BuildBands();
	void Analyze();

	AnalysisConfig m_config;
	unsigned m_rate;
	int m_hop;

	double m_attack_coeff;
	double m_release_coeff;

	// Periodic Hann window, applied before the transform.
	std::vector<double> m_hann;
	// Sliding analysis window: the most recent fft_size samples.
	std::vector<float> m_window;
	int m_window_fill;

	// Log-spaced band definition.
	std::vector<double> m_band_edges_hz;
	std::vector<int> m_band_bin_lo;
	std::vector<int> m_band_bin_hi;

	// FFT scratch space.  Doubles because FFTW's double-precision transform has
	// the least surprising scaling; the sizes are tiny (32 kB) either way.
	std::vector<double> m_fft_in;
	std::vector<double> m_fft_out;	// 2 * (fft_size / 2 + 1) interleaved doubles
	void* m_plan;

	std::vector<float> m_bar_magnitudes;	// smoothed, written by Analyze()
	std::vector<float> m_raw_magnitudes;	// instantaneous, written by Analyze()
};

} // namespace sa

#endif
