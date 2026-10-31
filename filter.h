// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __FILTER_H__
#define __FILTER_H__

#include <string>

// A draggable/resizable box drawn over the spectrum.  Its "activation" is how
// much of the box area is currently filled by the bars, which is what gets sent
// out over OSC.
//
// Pure geometry and arithmetic: no OpenGL, no GTK, no SDL.  Each front end draws
// the box itself from the accessors below, which keeps this class testable
// (tests/filter-test.cc) and reusable on any toolkit.
class Filter
{
public:
	Filter(float x, float y, const std::string& name, float r, float g, float b);
	~Filter();

	bool PointerPress(int button, float x, float y);
	void PointerMovement(float x, float y, float delta_x, float delta_y);
	void PointerRelease(int button, float x, float y);

	// Recompute the activation from the current bar magnitudes.
	// Returns true if it changed since the previous call.
	bool Update(const float* bar_magnitudes, int num_bars);

	float GetActivation() const { return m_activation; }
	const char* GetName() const { return m_name.c_str(); }
	float Area() const { return m_width * m_height; }
	bool HitTest(float x, float y) const;

	// Geometry, in the [-0.5, 0.5] x [-0.5, 0.5] world space the UI draws in.
	float X() const { return m_x; }
	float Y() const { return m_y; }
	float Width() const { return m_width; }
	float Height() const { return m_height; }
	bool Hovered() const { return m_hover; }

	float Red() const { return m_red; }
	float Green() const { return m_green; }
	float Blue() const { return m_blue; }

private:
	float m_x;
	float m_y;

	std::string m_name;

	float m_red;
	float m_green;
	float m_blue;

	float m_width;
	float m_height;
	bool m_hover;
	bool m_grabbed_move;

	bool m_grabbed_up;
	bool m_grabbed_down;
	bool m_grabbed_right;
	bool m_grabbed_left;

	float m_activation;
	float m_previous_activation;
};

#endif
