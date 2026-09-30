#include <gtkmm.h>
#include <math.h>

#include "gl-compat.h"

float clamp(float value, float min, float max)
{
	if(value < min) {
		return min;
	}
	else if(value > max) {
		return max;
	}
	return value;
}

// Checks if a->b overlaps c->d
float overlap_2D(float a, float b, float c, float d)
{
	if(a >= d || b <= c)
		return 0.0;

	if(a < c) {
		if(b < d) {
			return b - c;
		}
		else {
			return d - c;
		}
	}
	else {
		if(b < d) {
			return b - a;
		}
		else {
			return d - a;
		}
	}
}

void render_unit_square()
{
	glc::vertex2f(-0.5, 0.5);
	glc::vertex2f( 0.5, 0.5);
	glc::vertex2f( 0.5, -0.5);
	glc::vertex2f(-0.5, -0.5);
}
