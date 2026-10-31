// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __FILTER_H__
#define __FILTER_H__

#include <string>

// A draggable/resizable box drawn over the spectrum.  Its "activation" is how
// much of the box area is currently filled by the bars, which is what gets sent
// out over OSC.
class Filter
{
public:
	Filter(float x, float y, const std::string& name, float r, float g, float b);
	virtual ~Filter();

	virtual void Render(void);
	virtual bool PointerPress(int button, float x, float y);
	virtual void PointerMovement(float x, float y, float delta_x, float delta_y);
	virtual void PointerRelease(int button, float x, float y);
	virtual bool Update(float* bar_magnitudes, int num_bars);
	virtual float GetActivation() const;
	virtual const char* GetName() const;

	virtual float Area() const;
	virtual bool HitTest(float x, float y);

protected:
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
