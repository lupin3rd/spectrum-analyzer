// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "spectrum-dsp.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sa {

namespace {

// Calibrates a full-scale (amplitude 1.0) sine, seen through a Hann window, to
// roughly 0 dBFS after summing the power over a band.
const double kPowerScaleFactor = 16.0;

} // anonymous namespace

SpectrumDsp::SpectrumDsp()
	: m_rate(44100),
		m_hop(256),
		m_attack_coeff(1.0),
		m_release_coeff(1.0),
		m_window_fill(0),
		m_plan(NULL)
{
}

SpectrumDsp::SpectrumDsp(const AnalysisConfig& config)
	: m_config(config),
		m_rate(44100),
		m_hop(256),
		m_attack_coeff(1.0),
		m_release_coeff(1.0),
		m_window_fill(0),
		m_plan(NULL)
{
}

SpectrumDsp::~SpectrumDsp()
{
	ReleasePlan();
}

void SpectrumDsp::ReleasePlan()
{
	if (m_plan) {
		fftw_destroy_plan((fftw_plan)m_plan);
		m_plan = NULL;
	}
	m_fft_in.clear();
	m_fft_out.clear();
}

bool SpectrumDsp::Configure(unsigned rate, int hop_size)
{
	ReleasePlan();

	if (rate == 0)
		return false;
	if (m_config.fft_size < 16 || (m_config.fft_size & (m_config.fft_size - 1)) != 0) {
		fprintf(stderr, "SpectrumDsp: fft_size must be a power of two, got %d\n", m_config.fft_size);
		return false;
	}
	if (m_config.num_bars <= 0)
		return false;

	m_rate = rate;
	m_hop = (hop_size > 0) ? hop_size : (m_config.fft_size / 4);

	const std::size_t n = (std::size_t)m_config.fft_size;
	m_fft_in.assign(n, 0.0);
	m_fft_out.assign(2 * (n / 2 + 1), 0.0);
	m_window.assign(n, 0.0f);
	m_window_fill = 0;

	m_bar_magnitudes.assign((std::size_t)m_config.num_bars, 0.0f);
	m_raw_magnitudes.assign((std::size_t)m_config.num_bars, 0.0f);

	BuildWindow();
	BuildBands();

	// The plan is created against the very buffers Analyze() will use, so
	// FFTW never sees a stale pointer.  The vectors are never resized again
	// until the next Configure().
	m_plan = (void*)fftw_plan_dft_r2c_1d(
		m_config.fft_size,
		&m_fft_in[0],
		(fftw_complex*)&m_fft_out[0],
		FFTW_ESTIMATE);

	if (!m_plan) {
		fprintf(stderr, "SpectrumDsp: could not create the FFT plan\n");
		return false;
	}

	// Envelope-follower coefficients for the real analysis interval, which is
	// one FFT per delivered block.
	const double dt = (double)m_hop / (double)m_rate;
	m_attack_coeff = 1.0 - std::exp(-dt / m_config.attack_seconds);
	m_release_coeff = 1.0 - std::exp(-dt / m_config.release_seconds);
	return true;
}

void SpectrumDsp::Reset()
{
	std::fill(m_window.begin(), m_window.end(), 0.0f);
	std::fill(m_bar_magnitudes.begin(), m_bar_magnitudes.end(), 0.0f);
	std::fill(m_raw_magnitudes.begin(), m_raw_magnitudes.end(), 0.0f);
	m_window_fill = 0;
}

void SpectrumDsp::BuildWindow()
{
	// Periodic Hann window: drastically reduces spectral leakage, so a strong
	// bass tone no longer smears across every bar.
	const double pi = 3.14159265358979323846;
	m_hann.assign((std::size_t)m_config.fft_size, 0.0);
	for (int i = 0; i < m_config.fft_size; i++)
		m_hann[(std::size_t)i] = 0.5 - 0.5 * std::cos(2.0 * pi * (double)i / (double)m_config.fft_size);
}

void SpectrumDsp::BuildBands()
{
	// Log-spaced band edges from min_freq up to max_freq (clamped to Nyquist).
	// Equal ratio between adjacent bands, like fractional-octave bands, which
	// matches how music is perceived.
	const double fmin = m_config.min_freq;
	double fmax = m_config.max_freq;
	const double nyquist = 0.5 * (double)m_rate;
	if (fmax > nyquist)
		fmax = nyquist;
	if (fmax <= fmin)
		fmax = fmin * 2.0;

	m_band_edges_hz.assign((std::size_t)m_config.num_bars + 1, 0.0);
	const double ratio = std::pow(fmax / fmin, 1.0 / (double)m_config.num_bars);
	for (int i = 0; i <= m_config.num_bars; i++)
		m_band_edges_hz[(std::size_t)i] = fmin * std::pow(ratio, (double)i);

	// Map each band to its FFT bin range.
	m_band_bin_lo.assign((std::size_t)m_config.num_bars, 0);
	m_band_bin_hi.assign((std::size_t)m_config.num_bars, 0);

	const int half = m_config.fft_size / 2;
	for (int b = 0; b < m_config.num_bars; b++) {
		const double f0 = m_band_edges_hz[(std::size_t)b];
		const double f1 = m_band_edges_hz[(std::size_t)b + 1];

		int klo = (int)std::ceil(f0 * (double)m_config.fft_size / (double)m_rate);
		int khi = (int)std::floor(f1 * (double)m_config.fft_size / (double)m_rate);

		if (klo < 1) klo = 1;
		if (khi > half) khi = half;
		if (khi < klo) {
			// Band narrower than one bin: use the closest single bin.
			int k = (int)std::lround(0.5 * (f0 + f1) * (double)m_config.fft_size / (double)m_rate);
			if (k < 1) k = 1;
			if (k > half) k = half;
			klo = khi = k;
		}
		m_band_bin_lo[(std::size_t)b] = klo;
		m_band_bin_hi[(std::size_t)b] = khi;
	}
}

int SpectrumDsp::BandForFrequency(double hz) const
{
	const int bars = m_config.num_bars;
	if (bars <= 0 || m_band_edges_hz.empty())
		return -1;
	if (hz < m_band_edges_hz.front() || hz > m_band_edges_hz.back())
		return -1;

	for (int b = 0; b < bars; b++) {
		if (hz >= m_band_edges_hz[(std::size_t)b] && hz <= m_band_edges_hz[(std::size_t)b + 1])
			return b;
	}
	return -1;
}

int SpectrumDsp::Push(const float* samples, std::size_t count)
{
	if (!samples || count == 0 || !IsConfigured())
		return 0;

	// Slide the freshly captured block into the analysis window, dropping the
	// oldest samples.  With a 256-frame block and a 4096-frame window this is
	// 87.5% overlap, so the spectrum is recomputed ~172 times per second and
	// the bars move smoothly.
	const int n = (int)count;
	const int size = m_config.fft_size;

	if (n >= size) {
		std::memcpy(&m_window[0], samples + (n - size), (std::size_t)size * sizeof(float));
		m_window_fill = size;
	}
	else {
		if (m_window_fill + n > size) {
			const int drop = m_window_fill + n - size;
			std::memmove(&m_window[0], &m_window[(std::size_t)drop],
				(std::size_t)(m_window_fill - drop) * sizeof(float));
			m_window_fill -= drop;
		}
		std::memcpy(&m_window[(std::size_t)m_window_fill], samples, (std::size_t)n * sizeof(float));
		m_window_fill += n;
	}

	if (m_window_fill < size)
		return 0;	// still filling the very first window

	Analyze();
	return 1;
}

void SpectrumDsp::Analyze()
{
	const int size = m_config.fft_size;

	// Apply the Hann window and transform.
	for (int i = 0; i < size; i++)
		m_fft_in[(std::size_t)i] = (double)m_window[(std::size_t)i] * m_hann[(std::size_t)i];

	fftw_execute((fftw_plan)m_plan);

	const fftw_complex* out = (const fftw_complex*)&m_fft_out[0];

	// Sum the power in each log band.  Summing (rather than averaging) gives a
	// flat response for pink noise, i.e. equal energy per octave.
	const double scale = kPowerScaleFactor / (double)((long)size * (long)size);
	const double range = m_config.max_db - m_config.min_db;

	for (int b = 0; b < m_config.num_bars; b++) {
		double power = 0.0;
		for (int k = m_band_bin_lo[(std::size_t)b]; k <= m_band_bin_hi[(std::size_t)b]; k++) {
			power += out[k][0] * out[k][0] + out[k][1] * out[k][1];
		}
		power *= scale;

		const double db = 10.0 * std::log10(power + 1e-12);
		double target = (db - m_config.min_db) / range;
		if (target < 0.0) target = 0.0;
		if (target > 1.0) target = 1.0;
		m_raw_magnitudes[(std::size_t)b] = (float)target;
	}

	// Attack/release envelope follower so the bars move fluidly instead of
	// flickering with every FFT frame.
	for (int b = 0; b < m_config.num_bars; b++) {
		float& cur = m_bar_magnitudes[(std::size_t)b];
		const float target = m_raw_magnitudes[(std::size_t)b];
		const double coeff = (target > cur) ? m_attack_coeff : m_release_coeff;
		cur = (float)(cur + coeff * (double)(target - cur));
	}
}

} // namespace sa
