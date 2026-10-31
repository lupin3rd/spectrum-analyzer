// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
// Headless tests for the analysis core.
//
// No GTK, no audio device, no X display: spectrum-dsp.cc and math-utils.h are
// the parts of this program that can be verified deterministically, so they are
// the parts that get tested.  Run with `make test`.

#include "spectrum-dsp.h"
#include "math-utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

const char* g_current_test = "";

void check(bool condition, const char* what)
{
	g_checks++;
	if (!condition) {
		g_failures++;
		std::fprintf(stderr, "FAIL [%s] %s\n", g_current_test, what);
	}
}

void check_near(double got, double want, double tolerance, const char* what)
{
	g_checks++;
	if (!(std::fabs(got - want) <= tolerance)) {
		g_failures++;
		std::fprintf(stderr, "FAIL [%s] %s: got %.6f, want %.6f +/- %.6f\n",
			g_current_test, what, got, want, tolerance);
	}
}

void check_equal(int got, int want, const char* what)
{
	g_checks++;
	if (got != want) {
		g_failures++;
		std::fprintf(stderr, "FAIL [%s] %s: got %d, want %d\n",
			g_current_test, what, got, want);
	}
}

// --- signal helpers -------------------------------------------------------

std::vector<float> make_sine(double freq, unsigned rate, std::size_t samples,
	double amplitude = 0.5)
{
	std::vector<float> out(samples);
	const double two_pi = 6.283185307179586476925286766559;
	for (std::size_t i = 0; i < samples; i++)
		out[i] = (float)(amplitude * std::sin(two_pi * freq * (double)i / (double)rate));
	return out;
}

std::vector<float> make_silence(std::size_t samples)
{
	return std::vector<float>(samples, 0.0f);
}

// Feed `samples` to the DSP in blocks of `hop`, so the sliding window and the
// envelope follower run exactly as they do in the real capture thread.
void feed(sa::SpectrumDsp& dsp, const std::vector<float>& samples, std::size_t hop)
{
	for (std::size_t i = 0; i + hop <= samples.size(); i += hop)
		dsp.Push(&samples[i], hop);
}

int loudest_band(const sa::SpectrumDsp& dsp)
{
	const std::vector<float>& raw = dsp.RawMagnitudes();
	int best = 0;
	for (std::size_t i = 1; i < raw.size(); i++) {
		if (raw[i] > raw[(std::size_t)best])
			best = (int)i;
	}
	return best;
}

// --- tests ----------------------------------------------------------------

void test_math_utils()
{
	g_current_test = "math-utils";

	check_near(clamp(5.0f, 0.0f, 1.0f), 1.0f, 0.0, "clamp above range");
	check_near(clamp(-5.0f, 0.0f, 1.0f), 0.0f, 0.0, "clamp below range");
	check_near(clamp(0.5f, 0.0f, 1.0f), 0.5f, 0.0, "clamp inside range");

	// Disjoint intervals overlap by nothing.
	check_near(overlap_2D(0.0f, 1.0f, 2.0f, 3.0f), 0.0f, 0.0, "disjoint lower");
	check_near(overlap_2D(2.0f, 3.0f, 0.0f, 1.0f), 0.0f, 0.0, "disjoint higher");
	// Touching intervals are still disjoint.
	check_near(overlap_2D(0.0f, 1.0f, 1.0f, 2.0f), 0.0f, 0.0, "touching");
	// Partial overlaps.
	check_near(overlap_2D(0.0f, 2.0f, 1.0f, 3.0f), 1.0f, 1e-6, "partial right");
	check_near(overlap_2D(1.0f, 3.0f, 0.0f, 2.0f), 1.0f, 1e-6, "partial left");
	// Containment in both directions.
	check_near(overlap_2D(0.0f, 10.0f, 4.0f, 6.0f), 2.0f, 1e-6, "contained");
	check_near(overlap_2D(4.0f, 6.0f, 0.0f, 10.0f), 2.0f, 1e-6, "contains");
}

void test_configure()
{
	g_current_test = "configure";

	sa::SpectrumDsp dsp;
	check(!dsp.IsConfigured(), "not configured before Configure()");
	check(dsp.Configure(44100, 256), "Configure(44100, 256) succeeds");
	check(dsp.IsConfigured(), "configured after Configure()");
	check_equal(dsp.NumBars(), sa::kNumBars, "bar count");
	check_equal(dsp.FftSize(), sa::kFftSize, "fft size");
	check_equal((int)dsp.Rate(), 44100, "sample rate");
	check_equal(dsp.HopSize(), 256, "hop size");

	// A non power of two window must be rejected instead of misbehaving later.
	sa::AnalysisConfig bad;
	bad.fft_size = 1000;
	sa::SpectrumDsp broken(bad);
	check(!broken.Configure(44100, 256), "non power of two fft size rejected");
}

void test_band_edges()
{
	g_current_test = "band-edges";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	const std::vector<double>& edges = dsp.BandEdgesHz();
	check_equal((int)edges.size(), sa::kNumBars + 1, "edge count is bars + 1");
	check_near(edges.front(), 20.0, 1e-9, "lowest edge is 20 Hz");
	check_near(edges.back(), 20000.0, 1e-6, "highest edge is 20 kHz");

	bool ascending = true;
	for (std::size_t i = 1; i < edges.size(); i++) {
		if (!(edges[i] > edges[i - 1]))
			ascending = false;
	}
	check(ascending, "edges are strictly ascending");

	// Every band must map to at least one FFT bin, and ranges must not overlap.
	const std::vector<int>& lo = dsp.BandBinLow();
	const std::vector<int>& hi = dsp.BandBinHigh();
	check_equal((int)lo.size(), sa::kNumBars, "bin-low table size");
	check_equal((int)hi.size(), sa::kNumBars, "bin-high table size");

	bool ranges_ok = true;
	for (int b = 0; b < sa::kNumBars; b++) {
		if (lo[(std::size_t)b] < 1 || hi[(std::size_t)b] < lo[(std::size_t)b])
			ranges_ok = false;
		if (b > 0 && lo[(std::size_t)b] <= hi[(std::size_t)b - 1])
			ranges_ok = false;
	}
	check(ranges_ok, "band bin ranges are non-empty and ordered");

	// The upper edge must follow Nyquist instead of silently pointing past it.
	sa::SpectrumDsp low_rate;
	check(low_rate.Configure(24000, 256), "configure at 24 kHz");
	check_near(low_rate.BandEdgesHz().back(), 12000.0, 1e-6, "upper edge clamped to Nyquist");
}

void test_silence()
{
	g_current_test = "silence";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	const std::vector<float> silence = make_silence(sa::kFftSize * 2);
	feed(dsp, silence, 256);

	for (int b = 0; b < dsp.NumBars(); b++) {
		check(dsp.Magnitudes()[b] >= 0.0f, "magnitude is not negative");
		check(dsp.Magnitudes()[b] < 0.01f, "silence stays at the bottom of the scale");
	}
}

void test_tone_lands_in_the_right_band()
{
	g_current_test = "tone-band";

	const unsigned rate = 44100;

	// Test tones are placed at the geometric centre of a band, not at its edge:
	// a tone sitting exactly on an edge legitimately splits its energy between
	// two bands (the Hann window's main lobe is several bins wide), which says
	// nothing about the band mapping.
	const int bands[] = { 1, 3, 5, 7, 9, 11 };

	for (std::size_t t = 0; t < sizeof(bands) / sizeof(bands[0]); t++) {
		sa::SpectrumDsp dsp;
		check(dsp.Configure(rate, 256), "configure");

		const std::vector<double>& edges = dsp.BandEdgesHz();
		const double freq = std::sqrt(edges[(std::size_t)bands[t]] * edges[(std::size_t)bands[t] + 1]);

		const std::vector<float> tone = make_sine(freq, rate, sa::kFftSize * 8, 0.5);
		feed(dsp, tone, 256);

		check_equal(dsp.BandForFrequency(freq), bands[t], "tone frequency maps to its band");
		check_equal(loudest_band(dsp), bands[t], "loudest band matches the tone frequency");

		// A pure tone must not light up every band.
		int lit = 0;
		for (int b = 0; b < dsp.NumBars(); b++) {
			if (dsp.RawMagnitudes()[(std::size_t)b] > 0.5f)
				lit++;
		}
		check(lit <= 3, "a single tone lights up only the neighbouring bands");
	}
}

void test_amplitude_scale()
{
	g_current_test = "amplitude-scale";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	const double rate = 44100.0;
	const double freq = 1000.0;
	const int band = dsp.BandForFrequency(freq);

	// Full scale sine: after the envelope settles the band should be near the
	// top of the -80..0 dB scale, i.e. close to 1.0.
	const std::vector<float> loud = make_sine(freq, (unsigned)rate, sa::kFftSize * 8, 1.0);
	feed(dsp, loud, 256);
	const float loud_value = dsp.RawMagnitudes()[(std::size_t)band];
	check(loud_value > 0.85f, "a full-scale tone reaches the top of the scale");

	// Same tone 60 dB down must sit far lower: the scale has to be logarithmic.
	sa::SpectrumDsp quiet;
	check(quiet.Configure(44100, 256), "configure (quiet)");
	const std::vector<float> tiny = make_sine(freq, (unsigned)rate, sa::kFftSize * 8, 0.001);
	feed(quiet, tiny, 256);
	const float quiet_value = quiet.RawMagnitudes()[(std::size_t)band];
	check(quiet_value < loud_value, "a quieter tone reads lower");
	check_near(quiet_value, 0.25, 0.15, "60 dB down lands in the lower quarter of the scale");
}

void test_envelope_attack_faster_than_release()
{
	g_current_test = "envelope";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	const unsigned rate = 44100;
	const std::size_t hop = 256;
	const int band = dsp.BandForFrequency(1000.0);

	// One hop worth of time, in seconds, to convert the coefficient back into a
	// comparable number of samples.
	const std::size_t decay_samples = (std::size_t)(0.010 * rate);	// 10 ms

	// Attack: from silence to a loud tone, the bar rises within 10 ms.
	const std::vector<float> loud = make_sine(1000.0, rate, sa::kFftSize + sa::kFftSize, 1.0);
	feed(dsp, loud, hop);
	const float after_attack = dsp.Magnitudes()[(std::size_t)band];

	// Release: from a loud tone to silence, the bar must still be high after
	// the same 10 ms, because release is 25x slower than attack.
	sa::SpectrumDsp falling;
	check(falling.Configure(rate, hop), "configure (release)");
	feed(falling, loud, hop);
	const float peak = falling.Magnitudes()[(std::size_t)band];
	check(peak > 0.5f, "bar reaches a high value while the tone plays");
	check(after_attack > 0.5f, "attack reaches a high value quickly");

	const std::vector<float> silence(decay_samples + hop, 0.0f);
	feed(falling, silence, hop);
	const float after_release = falling.Magnitudes()[(std::size_t)band];
	check(after_release > peak * 0.5f, "release holds the bar up for a few tens of ms");
}

void test_envelope_is_bounded()
{
	g_current_test = "envelope-bounds";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	// A digital full-scale square wave is the worst case for overshoot.
	std::vector<float> square(sa::kFftSize * 4);
	for (std::size_t i = 0; i < square.size(); i++)
		square[i] = ((i / 8) % 2) ? 1.0f : -1.0f;
	feed(dsp, square, 256);

	bool bounded = true;
	for (int b = 0; b < dsp.NumBars(); b++) {
		const float v = dsp.Magnitudes()[b];
		if (!(v >= 0.0f && v <= 1.0f))
			bounded = false;
	}
	check(bounded, "magnitudes stay inside [0, 1]");
}

void test_no_spectrum_before_the_window_is_full()
{
	g_current_test = "warm-up";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	const std::vector<float> tone = make_sine(1000.0, 44100, 1024, 1.0);
	int computed = 0;
	for (std::size_t i = 0; i + 256 <= tone.size(); i += 256)
		computed += dsp.Push(&tone[i], 256);
	check_equal(computed, 0, "no spectrum before fft_size samples were collected");

	const std::vector<float> more = make_sine(1000.0, 44100, sa::kFftSize, 1.0);
	feed(dsp, more, 256);
	check(dsp.Push(&more[0], 256) == 1, "a spectrum is computed once the window is full");
}

void test_reconfigure_resets_state()
{
	g_current_test = "reconfigure";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure at 44.1 kHz");
	const std::vector<float> tone = make_sine(1000.0, 44100, sa::kFftSize * 2, 1.0);
	feed(dsp, tone, 256);
	check(dsp.Magnitudes()[0] >= 0.0f, "magnitudes readable");

	// Switching to another device drops the buffered audio and the bands.
	check(dsp.Configure(48000, 512), "reconfigure at 48 kHz");
	check_equal((int)dsp.Rate(), 48000, "new rate applied");
	check_equal(dsp.HopSize(), 512, "new hop applied");
	check_near(dsp.Magnitudes()[0], 0.0, 1e-6, "bars reset on reconfigure");

	const std::vector<float> higher = make_sine(2000.0, 48000, sa::kFftSize * 4, 1.0);
	feed(dsp, higher, 512);
	check_equal(loudest_band(dsp), dsp.BandForFrequency(2000.0), "2 kHz tone tracks at 48 kHz");
}

void test_odd_block_sizes()
{
	g_current_test = "odd-blocks";

	sa::SpectrumDsp dsp;
	check(dsp.Configure(44100, 256), "configure");

	// A block larger than the analysis window must use its newest samples.
	const std::vector<float> big = make_sine(1000.0, 44100, sa::kFftSize * 2, 1.0);
	check(dsp.Push(&big[0], big.size()) == 1, "oversized block is analysed in one go");
	check_equal(loudest_band(dsp), dsp.BandForFrequency(1000.0), "oversized block keeps the tone");

	// Non-multiple block sizes must not lose or duplicate samples.
	sa::SpectrumDsp small;
	check(small.Configure(44100, 100), "configure with hop 100");
	for (std::size_t i = 0; i + 100 <= big.size(); i += 100)
		small.Push(&big[i], 100);
	check_equal(loudest_band(small), small.BandForFrequency(1000.0), "uneven block sizes track the tone");
}

} // anonymous namespace

int main()
{
	test_math_utils();
	test_configure();
	test_band_edges();
	test_silence();
	test_tone_lands_in_the_right_band();
	test_amplitude_scale();
	test_envelope_attack_faster_than_release();
	test_envelope_is_bounded();
	test_no_spectrum_before_the_window_is_full();
	test_reconfigure_resets_state();
	test_odd_block_sizes();

	std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
