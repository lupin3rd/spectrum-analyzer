// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
//
// Tests for the filter box: hit testing, dragging, resizing and the activation
// arithmetic.  Filter is pure geometry, so this runs headless like the DSP
// tests (`make test`).

#include "filter.h"

#include <cmath>
#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_current = "";

void check(bool condition, const char* what)
{
	g_checks++;
	if (!condition) {
		g_failures++;
		std::fprintf(stderr, "FAIL [%s] %s\n", g_current, what);
	}
}

void check_near(double got, double want, double tolerance, const char* what)
{
	g_checks++;
	if (!(std::fabs(got - want) <= tolerance)) {
		g_failures++;
		std::fprintf(stderr, "FAIL [%s] %s: got %.6f, want %.6f +/- %.6f\n",
			g_current, what, got, want, tolerance);
	}
}

// A box of 0.15 x 0.15 at (-0.375, 0.41): the default "Red" filter.
Filter make_box()
{
	return Filter(-0.375f, 0.41f, "Red", 1.0f, 0.0f, 0.0f);
}

void test_defaults()
{
	g_current = "defaults";

	Filter f = make_box();
	check_near(f.Area(), 0.15 * 0.15, 1e-9, "default area");
	check_near(f.X(), -0.375, 1e-9, "default x");
	check_near(f.Y(), 0.41, 1e-6, "default y");
	check_near(f.Width(), 0.15, 1e-6, "default width");
	check_near(f.Height(), 0.15, 1e-6, "default height");
	check_near(f.GetActivation(), 0.0, 1e-9, "activation starts at zero");
	check(f.GetName() && std::string(f.GetName()) == "Red", "name is stored");
	check_near(f.Red(), 1.0, 1e-9, "red channel");
	check_near(f.Green(), 0.0, 1e-9, "green channel");
	check_near(f.Blue(), 0.0, 1e-9, "blue channel");
}

void test_hit_test()
{
	g_current = "hit-test";

	Filter f = make_box();
	check(f.HitTest(-0.375f, 0.41f), "centre is inside");
	check(f.HitTest(-0.30f, 0.41f), "near the right edge is inside");
	check(!f.HitTest(-0.10f, 0.41f), "far away is outside");
	check(!f.HitTest(-0.375f, -0.4f), "same column, other end is outside");

	// A press outside must not grab anything.
	check(!f.PointerPress(1, -0.10f, 0.41f), "press outside returns false");
}

void test_press_without_motion()
{
	g_current = "press-without-motion";

	Filter f = make_box();

	// No PointerMovement() first: a click straight onto the box must still grab.
	check(f.PointerPress(1, -0.375f, 0.41f), "centre press grabs");
	check(f.Hovered(), "hover is set by the press");

	// Dragging from the centre moves the box by the delta.
	f.PointerMovement(-0.375f + 0.02f, 0.41f + 0.01f, 0.02f, 0.01f);
	check_near(f.X(), -0.355, 1e-6, "x follows the drag");
	check_near(f.Y(), 0.42, 1e-6, "y follows the drag");
	check_near(f.Width(), 0.15, 1e-6, "dragging does not resize");
	f.PointerRelease(1, 0.0f, 0.0f);

	// After release the box stops following the pointer.
	const float x_before = f.X();
	f.PointerMovement(-0.10f, 0.10f, -0.25f, -0.30f);
	check_near(f.X(), x_before, 1e-9, "release stops the drag");
}

void test_drag_is_clamped()
{
	g_current = "drag-clamped";

	Filter f = make_box();
	check(f.PointerPress(1, -0.375f, 0.41f), "grab");

	// Push far past the right edge: the box must stay fully inside [-0.5, 0.5].
	f.PointerMovement(-0.375f, 0.41f, 5.0f, 5.0f);
	check(f.X() <= 0.5f - f.Width() / 2.0f + 1e-6f, "right edge clamps");
	check(f.Y() <= 0.5f - f.Height() / 2.0f + 1e-6f, "top edge clamps");
	f.PointerRelease(1, 0.0f, 0.0f);

	Filter g = make_box();
	check(g.PointerPress(1, -0.375f, 0.41f), "grab (negative)");
	g.PointerMovement(-0.375f, 0.41f, -5.0f, -5.0f);
	check(g.X() >= -0.5f + g.Width() / 2.0f - 1e-6f, "left edge clamps");
	check(g.Y() >= -0.5f + g.Height() / 2.0f - 1e-6f, "bottom edge clamps");
	g.PointerRelease(1, 0.0f, 0.0f);
}

void test_resize_with_the_edges()
{
	g_current = "resize";

	Filter f = make_box();

	// A press away from the centre grabs the nearest edge.
	check(f.PointerPress(1, -0.375f + 0.07f, 0.41f), "press near the right edge");
	f.PointerMovement(-0.375f + 0.08f, 0.41f, 0.05f, 0.0f);
	check(f.Width() > 0.15f, "right edge widens the box");
	check_near(f.Height(), 0.15, 1e-6, "height is untouched");
	f.PointerRelease(1, 0.0f, 0.0f);

	// Width is clamped to a sane minimum no matter how far the pointer goes.
	Filter g = make_box();
	check(g.PointerPress(1, -0.375f + 0.07f, 0.41f), "press near the right edge");
	g.PointerMovement(-0.375f, 0.41f, -5.0f, 0.0f);
	check(g.Width() >= 0.05f - 1e-6f, "width has a floor");
	g.PointerRelease(1, 0.0f, 0.0f);
}

void test_activation()
{
	g_current = "activation";

	float bars[12];
	for (int i = 0; i < 12; i++)
		bars[i] = 0.0f;

	// The default box covers x 0.05..0.20 and y 0.835..0.985 (screen space).
	Filter f = make_box();
	check(!f.Update(bars, 12), "zero bars leave the activation at zero");
	check_near(f.GetActivation(), 0.0, 1e-9, "activation stays zero");

	// Bars that never reach the box height contribute nothing.
	for (int i = 0; i < 12; i++)
		bars[i] = 0.5f;
	f.Update(bars, 12);
	check_near(f.GetActivation(), 0.0, 1e-9, "bars below the box contribute nothing");

	// Full height everywhere: the box is completely covered.
	for (int i = 0; i < 12; i++)
		bars[i] = 1.0f;
	f.Update(bars, 12);
	check_near(f.GetActivation(), 1.0, 1e-5, "full bars fill the box");

	// Only the bar under the box counts.  The box is 0.15 wide and one bar is
	// 1/12 = 0.0833 wide, so that single bar covers 0.0833/0.15 of the box.
	for (int i = 0; i < 12; i++)
		bars[i] = 0.0f;
	bars[1] = 1.0f;
	f.Update(bars, 12);
	check_near(f.GetActivation(), (1.0 / 12.0) / 0.15, 1e-4, "one bar covers its share of the box");

	// Partial height inside the box.
	for (int i = 0; i < 12; i++)
		bars[i] = 0.9f;
	f.Update(bars, 12);
	const float expected = (float)((0.9 - 0.835) / 0.15);
	check_near(f.GetActivation(), expected, 1e-4, "partial coverage scales linearly");
}

void test_activation_reports_changes()
{
	g_current = "activation-change";

	float bars[12];
	for (int i = 0; i < 12; i++)
		bars[i] = 0.0f;

	Filter f = make_box();
	check(!f.Update(bars, 12), "first update does not report a change from zero");

	for (int i = 0; i < 12; i++)
		bars[i] = 1.0f;
	check(f.Update(bars, 12), "a change is reported");
	check(!f.Update(bars, 12), "the same input reports no change");
}

void test_activation_is_clamped()
{
	g_current = "activation-clamped";

	Filter f = make_box();

	// Defensive: magnitudes above 1 must not produce an activation above 1.
	float bars[12];
	for (int i = 0; i < 12; i++)
		bars[i] = 1.5f;
	f.Update(bars, 12);
	check(f.GetActivation() >= 0.0f, "activation is not negative");

	// A degenerate box must not divide by zero.
	Filter tiny(0.0f, 0.0f, "Tiny", 0.0f, 0.0f, 0.0f);
	float flat[12];
	for (int i = 0; i < 12; i++)
		flat[i] = 0.0f;
	tiny.Update(flat, 12);
	check(!std::isnan(tiny.GetActivation()), "activation is never NaN");
}

} // anonymous namespace

int main()
{
	test_defaults();
	test_hit_test();
	test_press_without_motion();
	test_drag_is_clamped();
	test_resize_with_the_edges();
	test_activation();
	test_activation_reports_changes();
	test_activation_is_clamped();

	std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
