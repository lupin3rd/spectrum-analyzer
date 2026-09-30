#include "gl-compat.h"

#include <epoxy/gl.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>

namespace glc {

namespace {

struct Vertex {
	float x, y, r, g, b, a;
};

GLuint program = 0;
GLuint vao = 0;
GLuint vbo = 0;
GLint u_projection = -1;
GLint u_modelview = -1;

float projection[16];
float modelview[16];
float* current_matrix = modelview;
std::vector<std::array<float, 16> > matrix_stack;

std::vector<Vertex> verts;
GLenum current_mode = GL_TRIANGLES;
float current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

void mat4_identity(float* m)
{
	static const float id[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	std::memcpy(m, id, sizeof(id));
}

void mat4_mul(float* out, const float* a, const float* b)
{
	// Column-major 4x4 multiply: out = a * b.
	float r[16];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			float sum = 0.0f;
			for (int k = 0; k < 4; k++)
				sum += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = sum;
		}
	}
	std::memcpy(out, r, sizeof(r));
}

GLuint compile_shader(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);

	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "glc: shader compile error: %s\n", log);
	}
	return s;
}

void draw_vertices(const std::vector<Vertex>& v, GLenum mode)
{
	if (v.empty())
		return;

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);

	glUseProgram(program);
	glUniformMatrix4fv(u_projection, 1, GL_FALSE, projection);
	glUniformMatrix4fv(u_modelview, 1, GL_FALSE, modelview);
	glDrawArrays(mode, 0, (GLsizei)v.size());

	glBindVertexArray(0);
}

} // anonymous namespace

void init()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	const char* vs =
		"#version 150\n"
		"uniform mat4 u_projection;\n"
		"uniform mat4 u_modelview;\n"
		"in vec2 a_position;\n"
		"in vec4 a_color;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"    gl_Position = u_projection * u_modelview * vec4(a_position, 0.0, 1.0);\n"
		"    v_color = a_color;\n"
		"}\n";

	const char* fs =
		"#version 150\n"
		"in vec4 v_color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"    fragColor = v_color;\n"
		"}\n";

	GLuint vs_id = compile_shader(GL_VERTEX_SHADER, vs);
	GLuint fs_id = compile_shader(GL_FRAGMENT_SHADER, fs);

	program = glCreateProgram();
	glAttachShader(program, vs_id);
	glAttachShader(program, fs_id);
	glBindAttribLocation(program, 0, "a_position");
	glBindAttribLocation(program, 1, "a_color");
	glLinkProgram(program);

	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		fprintf(stderr, "glc: program link error: %s\n", log);
	}

	glDeleteShader(vs_id);
	glDeleteShader(fs_id);

	u_projection = glGetUniformLocation(program, "u_projection");
	u_modelview = glGetUniformLocation(program, "u_modelview");

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

	glBindVertexArray(0);

	mat4_identity(projection);
	mat4_identity(modelview);
}

void clear(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);
}

void matrix_mode(MatrixMode mode)
{
	current_matrix = (mode == MATRIX_PROJECTION) ? projection : modelview;
}

void load_identity()
{
	mat4_identity(current_matrix);
}

void push_matrix()
{
	std::array<float, 16> m;
	std::memcpy(m.data(), modelview, sizeof(modelview));
	matrix_stack.push_back(m);
}

void pop_matrix()
{
	if (!matrix_stack.empty()) {
		std::memcpy(modelview, matrix_stack.back().data(), sizeof(modelview));
		matrix_stack.pop_back();
	}
}

void translate(float x, float y)
{
	float t[16];
	mat4_identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = 0.0f;
	mat4_mul(current_matrix, current_matrix, t);
}

void scale(float x, float y)
{
	float s[16];
	mat4_identity(s);
	s[0] = x;
	s[5] = y;
	s[10] = 1.0f;
	mat4_mul(current_matrix, current_matrix, s);
}

void ortho(float left, float right, float bottom, float top)
{
	float m[16];
	mat4_identity(m);
	m[0] = 2.0f / (right - left);
	m[5] = 2.0f / (top - bottom);
	m[12] = -(right + left) / (right - left);
	m[13] = -(top + bottom) / (top - bottom);
	std::memcpy(current_matrix, m, sizeof(m));
}

void begin(unsigned int mode)
{
	current_mode = mode;
	verts.clear();
}

void color4f(float r, float g, float b, float a)
{
	current_color[0] = r;
	current_color[1] = g;
	current_color[2] = b;
	current_color[3] = a;
}

void vertex2f(float x, float y)
{
	Vertex v;
	v.x = x;
	v.y = y;
	v.r = current_color[0];
	v.g = current_color[1];
	v.b = current_color[2];
	v.a = current_color[3];
	verts.push_back(v);
}

void vertex3f(float x, float y, float z)
{
	(void)z;
	vertex2f(x, y);
}

void end()
{
	if (current_mode == GL_QUADS) {
		std::vector<Vertex> tris;
		tris.reserve(verts.size() * 3 / 2);
		for (size_t i = 0; i + 3 < verts.size(); i += 4) {
			const Vertex& v0 = verts[i];
			const Vertex& v1 = verts[i + 1];
			const Vertex& v2 = verts[i + 2];
			const Vertex& v3 = verts[i + 3];
			tris.push_back(v0);
			tris.push_back(v1);
			tris.push_back(v2);
			tris.push_back(v0);
			tris.push_back(v2);
			tris.push_back(v3);
		}
		draw_vertices(tris, GL_TRIANGLES);
	}
	else if (current_mode == GL_LINE_LOOP) {
		std::vector<Vertex> lines;
		if (verts.size() >= 2) {
			for (size_t i = 0; i < verts.size(); i++) {
				lines.push_back(verts[i]);
				lines.push_back(verts[(i + 1) % verts.size()]);
			}
		}
		draw_vertices(lines, GL_LINES);
	}
	else {
		draw_vertices(verts, current_mode);
	}

	verts.clear();
}

} // namespace glc
