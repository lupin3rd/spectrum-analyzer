// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "filter.h"

#include <math.h>

#include "math-utils.h"

#define MIN_WIDTH (0.05)
#define MAX_WIDTH (1.0)
#define MIN_HEIGHT (0.05)
#define MAX_HEIGHT (1.0)

Filter::Filter(float x, float y, const std::string& name, float r, float g, float b)
	: m_x(x),
		m_y(y),
		m_name(name),
		m_red(r),
		m_green(g),
		m_blue(b),
		m_width(0.15),
		m_height(0.15),
		m_hover(false),
		m_grabbed_move(false),
		m_grabbed_up(false),
		m_grabbed_down(false),
		m_grabbed_right(false),
		m_grabbed_left(false),
		m_activation(0.0),
		m_previous_activation(0.0)
{
}

Filter::~Filter()
{
}

bool Filter::Update(const float* bar_magnitudes, int num_bars)
{
	float x1 = 0.5+m_x - m_width / 2.0;
	float y1 = 0.5+m_y - m_height / 2.0;
	float x2 = 0.5+m_x + m_width / 2.0;
	float y2 = 0.5+m_y + m_height / 2.0;

	float width_per_bar = 1.0 / (float)num_bars;

	float total_area_covered = 0.0;
	int i;
	for(i=0 ; i<num_bars ; i++) {
		total_area_covered += (overlap_2D(x1, x2, width_per_bar * i, width_per_bar * (i+1)) * overlap_2D(y1, y2, 0.0, bar_magnitudes[i]));
	}
	m_previous_activation = m_activation;
	m_activation = total_area_covered / this->Area();

	return (m_activation != m_previous_activation);
}

bool Filter::PointerPress(int button, float x, float y)
{
	// Hit test here instead of trusting a previous PointerMovement: a click can
	// arrive without any motion (pointer already inside the box when the window
	// gets focus, touch input, synthetic events).
	if (!HitTest(x, y)) {
		m_hover = false;
		return false;
	}
	m_hover = true;

	// Center square (or second/third mouse button) is a drag
	if(button != 1 || ((fabs((float)m_x - (float)x) < (m_width / 6.0)) && (fabs((float)m_y - (float)y) < (m_height / 6.0)))) {
		m_grabbed_move = true;
	}
	else {
		m_grabbed_right = (x > m_x);
		m_grabbed_left = !m_grabbed_right;
		m_grabbed_down = (y < m_y);
		m_grabbed_up = !m_grabbed_down;
	}
	return true;
}

void Filter::PointerMovement(float x, float y, float delta_x, float delta_y)
{
	if(m_grabbed_move) {
		m_x = clamp(m_x + delta_x, -0.5 + (m_width / 2.0), 0.5 - m_width / 2.0);
		m_y = clamp(m_y + delta_y, -0.5 + (m_height / 2.0), 0.5 - m_height / 2.0);
	}

	if(m_grabbed_right) {
		m_width = clamp(m_width + delta_x, MIN_WIDTH, MAX_WIDTH);
		m_x = clamp(m_x + (delta_x / 2.0), -0.5 + (m_width / 2.0), 0.5 - m_width / 2.0);
	}
	else if(m_grabbed_left) {
		m_width = clamp(m_width - delta_x, MIN_WIDTH, MAX_WIDTH);
		m_x = clamp(m_x + (delta_x / 2.0), -0.5 + (m_width / 2.0), 0.5 - m_width / 2.0);
	}

	if(m_grabbed_up) {
		m_height = clamp(m_height + delta_y, MIN_HEIGHT, MAX_HEIGHT);
		m_y = clamp(m_y + (delta_y / 2.0), -0.5 + (m_height / 2.0), 0.5 - m_height / 2.0);
	}
	else if(m_grabbed_down) {
		m_height = clamp(m_height - delta_y, MIN_HEIGHT, MAX_HEIGHT);
		m_y = clamp(m_y + (delta_y / 2.0), -0.5 + (m_height / 2.0), 0.5 - m_height / 2.0);
	}

	m_hover = HitTest(x, y);
}

bool Filter::HitTest(float x, float y) const
{
	return ((fabs((float)m_x - (float)x) < (m_width / 2.0)) && (fabs((float)m_y - (float)y) < (m_height / 2.0)));
}

void Filter::PointerRelease(int button, float x, float y)
{
	// A release ends any interaction regardless of which button it was.
	(void)button;
	(void)x;
	(void)y;

	m_grabbed_move = false;
	m_grabbed_up = false;
	m_grabbed_down = false;
	m_grabbed_right = false;
	m_grabbed_left = false;
}
