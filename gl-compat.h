#ifndef __GL_COMPAT_H__
#define __GL_COMPAT_H__

// Minimal replacement for the legacy fixed-function OpenGL API
// (glBegin/glEnd, glMatrixMode, glPushMatrix, ...) built on top of an
// OpenGL 3.2 core profile, which is all Gtk::GLArea supports.
//
// The original project used immediate mode + the fixed-function matrix
// stack.  Those were removed from core-profile OpenGL, so this shim
// re-implements just the pieces the spectrum analyzer actually uses.

namespace glc {

enum MatrixMode {
	MATRIX_MODELVIEW,
	MATRIX_PROJECTION
};

// One-time initialisation.  Must be called with a current GL context.
void init();

// Clear with a colour (replaces glClearColor + glClear).
void clear(float r, float g, float b, float a);

// Matrix stack.
void matrix_mode(MatrixMode mode);
void load_identity();
void push_matrix();
void pop_matrix();
void translate(float x, float y);
void scale(float x, float y);
void ortho(float left, float right, float bottom, float top);

// Immediate-mode style vertex submission.
// Supported modes: GL_TRIANGLES, GL_LINES, GL_LINE_LOOP, GL_QUADS.
void begin(unsigned int mode);
void color4f(float r, float g, float b, float a);
void vertex2f(float x, float y);
void vertex3f(float x, float y, float z);
void end();

} // namespace glc

#endif
