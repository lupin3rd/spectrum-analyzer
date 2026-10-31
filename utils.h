// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __UTILS_H__
#define __UTILS_H__

// Geometry helpers (clamp, overlap_2D) live in math-utils.h so they can be
// unit tested without GTK or GL.  What remains here needs a current GL context.

// Submit the four corners of a unit square centred on the origin.
// Must be called between glc::begin() and glc::end().
void render_unit_square();

#endif
