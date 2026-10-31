// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "utils.h"

#include "gl-compat.h"

void render_unit_square()
{
	glc::vertex2f(-0.5, 0.5);
	glc::vertex2f( 0.5, 0.5);
	glc::vertex2f( 0.5, -0.5);
	glc::vertex2f(-0.5, -0.5);
}
