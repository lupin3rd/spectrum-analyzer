// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __MATH_UTILS_H__
#define __MATH_UTILS_H__

// Small, dependency-free geometry helpers.
//
// These used to live in utils.cc, which pulled in <gtkmm.h> and <epoxy/gl.h>
// just to expose two inline arithmetic functions.  Keeping them here means the
// unit tests can use them without a GUI or a GL context.

inline float clamp(float value, float min, float max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

// Length of the overlap between [a, b] and [c, d].
inline float overlap_2D(float a, float b, float c, float d)
{
	if (a >= d || b <= c)
		return 0.0f;

	if (a < c) {
		if (b < d)
			return b - c;
		return d - c;
	}

	if (b < d)
		return b - a;
	return d - a;
}

#endif
