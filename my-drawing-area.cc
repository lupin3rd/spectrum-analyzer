// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "my-drawing-area.h"

#include <epoxy/gl.h>
#include "gl-compat.h"

MyDrawingArea::MyDrawingArea()
{
	// Gtk::GLArea only supports OpenGL 3.2+ core profiles, so we request a
	// modern context and render through glc (a small fixed-function shim).
	set_required_version(3, 2);
}

MyDrawingArea::~MyDrawingArea()
{
}

void MyDrawingArea::trigger_redraw()
{
	queue_draw();
}

void MyDrawingArea::on_realize()
{
	Gtk::GLArea::on_realize();

	make_current();

	glc::init();

	// One-time OpenGL state.
	glEnable(GL_BLEND);
	glc::blend_alpha();
	glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
}

void MyDrawingArea::on_unrealize()
{
	// The GL objects belong to the context that created them; release them
	// while the context is still current, otherwise a re-realize leaves glc
	// pointing at dead ids.
	if (get_realized()) {
		make_current();
		glc::shutdown();
	}

	Gtk::GLArea::on_unrealize();
}

void MyDrawingArea::on_resize(int width, int height)
{
	Gtk::GLArea::on_resize(width, height);

	if (width <= 0 || height <= 0)
		return;

	make_current();

	glViewport(0, 0, width, height);

	// The original code used a 90 degree perspective + lookAt + modelview
	// scale, which together reduced to a plain 2x orthographic mapping of
	// the [-0.5, 0.5] x [-0.5, 0.5] world square onto the widget.
	glc::matrix_mode(glc::MATRIX_PROJECTION);
	glc::load_identity();
	glc::ortho(-0.5f, 0.5f, -0.5f, 0.5f);

	glc::matrix_mode(glc::MATRIX_MODELVIEW);
	glc::load_identity();
}
