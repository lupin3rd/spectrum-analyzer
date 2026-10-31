// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
//
// SDL3 front end.
//
// This is the portable replacement for the GTK window.  It owns the window, the
// renderer, the input handling and the frame loop; the analysis (spectrum-dsp),
// the capture (audio-sampler) and the OSC output (message-bus) are the same
// modules the GTK front end uses, unchanged.
//
// Nothing here needs an OpenGL profile, a shader or a widget toolkit: the scene
// is a grid, twelve bars and four boxes, all drawn with SDL_RenderFillRect and
// SDL_RenderLine.  Text uses SDL_RenderDebugText so there is no font file to
// ship either.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio-sampler.h"
#include "filter.h"
#include "message-bus.h"
#include "spectrum-dsp.h"
#include "ui-config.h"
#include "version.h"

#define APPLICATION_NAME ("Spectrum Analyzer")

// Minimum change in activation worth an OSC message (same rule as the GTK
// front end: the smoothed value always differs in the last decimal).
#define OSC_ACTIVATION_DEADBAND (0.002f)

#define ADDRESS_BUFFER_SIZE (200)

namespace {

// ---------------------------------------------------------------------------
// Layout, in points (SDL_RenderDebugText does not scale, so the bars are sized
// for the 8 pixel debug font).
// ---------------------------------------------------------------------------
const float kBarHeight = 30.0f;
const float kTopBarHeight = 30.0f;
const float kBottomBarHeight = 30.0f;
const float kPadding = 8.0f;

struct Rect
{
	float x, y, w, h;

	Rect() : x(0.0f), y(0.0f), w(0.0f), h(0.0f) {}
	Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

	bool contains(float px, float py) const
	{
		return px >= x && px < x + w && py >= y && py < y + h;
	}
};

// Horizontal extent of one text string, in points.
float text_width(const std::string& text)
{
	return (float)text.size() * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
}

class App
{
public:
	App();
	~App();

	bool init(const std::string& device);
	void run();

private:
	// --- frame ------------------------------------------------------------
	void handle_event(const SDL_Event& event);
	void handle_key(const SDL_KeyboardEvent& key);
	void handle_mouse_down(float x, float y);
	void handle_mouse_up(float x, float y);
	void handle_motion(float x, float y);

	void step();			// OSC + status, once per frame
	void render();

	// --- scene ------------------------------------------------------------
	void render_grid();
	void render_samples();
	void render_filters();
	void render_filter(const Filter& filter);
	void render_chrome();

	// --- coordinate helpers ----------------------------------------------
	Rect canvas_rect() const;
	float to_world_x(float screen_x) const;
	float to_world_y(float screen_y) const;

	// --- actions ----------------------------------------------------------
	void cycle_source();
	void select_source(int index);
	void open_source_menu();
	void close_source_menu();

	// --- source drop-down ------------------------------------------------
	float menu_item_height() const;
	Rect menu_rect() const;
	int menu_item_at(float x, float y) const;
	void render_source_menu();
	void change_number(int delta);
	void toggle_fullscreen();
	void commit_osc_field();
	void refresh_status();

	SDL_Window* m_window;
	SDL_Renderer* m_renderer;
	int m_width;
	int m_height;

	std::unique_ptr<AudioSampler> m_audio;
	std::unique_ptr<MessageBus> m_bus;
	std::vector<std::unique_ptr<Filter> > m_filters;
	std::vector<float> m_last_sent;

	std::vector<AudioSourceInfo> m_sources;
	int m_source_index;
	std::string m_source_argument;

	int m_number;
	bool m_fullscreen;
	bool m_quit;
	bool m_editing_osc;

	std::string m_osc_field;		// "host:port", as typed
	std::string m_osc_field_backup;	// restored on Escape
	bool m_field_valid;

	bool m_source_menu_open;
	int m_source_menu_highlight;

	bool m_pointer_down;
	bool m_pointer_tracking;
	float m_pointer_x;
	float m_pointer_y;

	float m_magnitudes[sa::kNumBars];
	char m_status[160];
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

App::App()
	: m_window(NULL),
		m_renderer(NULL),
		m_width(480),
		m_height(300),
		m_source_index(0),
		m_number(1),
		m_fullscreen(false),
		m_quit(false),
		m_editing_osc(false),
		m_osc_field(DEFAULT_OSC_HOST),
		m_field_valid(true),
		m_source_menu_open(false),
		m_source_menu_highlight(0),
		m_pointer_down(false),
		m_pointer_tracking(false),
		m_pointer_x(0.0f),
		m_pointer_y(0.0f)
{
	m_osc_field += ":";
	m_osc_field += DEFAULT_OSC_PORT;

	m_filters.push_back(std::unique_ptr<Filter>(new Filter(-0.375f, 0.41f, "Red", 1.0f, 0.0f, 0.0f)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter(-0.125f, 0.41f, "Green", 0.0f, 1.0f, 0.0f)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter( 0.125f, 0.41f, "Blue", 0.0f, 0.0f, 1.0f)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter( 0.375f, 0.41f, "Yellow", 1.0f, 1.0f, 0.0f)));
	m_last_sent.assign(m_filters.size(), -1.0f);

	m_status[0] = '\0';
}

App::~App()
{
	if (m_renderer)
		SDL_DestroyRenderer(m_renderer);
	if (m_window)
		SDL_DestroyWindow(m_window);
}

bool App::init(const std::string& device)
{
	m_source_argument = device;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	m_window = SDL_CreateWindow(APPLICATION_NAME, m_width, m_height,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!m_window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return false;
	}

	m_renderer = SDL_CreateRenderer(m_window, NULL);
	if (!m_renderer) {
		fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return false;
	}

	// Without a hardware renderer we still work, just without the frame pacing.
	SDL_SetRenderVSync(m_renderer, 1);

	m_bus.reset(new MessageBus());
	m_audio.reset(new AudioSampler());

	m_sources = AudioSampler::EnumerateSources();

	// The command line accepts an id or a name; resolving it against the
	// enumeration also tells us which entry to select in the drop-down.
	if (!m_source_argument.empty()) {
		m_source_argument = resolve_audio_source(m_source_argument);
		for (size_t i = 0; i < m_sources.size(); i++) {
			if (!m_source_argument.empty() && m_sources[i].id == m_source_argument)
				m_source_index = (int)i;
		}
	}

	const std::string source = (m_sources.empty() || m_source_index == 0) ? m_source_argument : m_sources[(size_t)m_source_index].id;

	if (!m_audio->Open(source)) {
		// Not fatal: the UI still works, and the device may appear later.  The
		// capture thread retries on its own once a stream existed; for a stream
		// that never opened, retry here.
		fprintf(stderr, "Audio: could not open '%s'\n", source.empty() ? "default" : source.c_str());
	}

	change_number(0);	// sets the window title
	refresh_status();
	return true;
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

Rect App::canvas_rect() const
{
	Rect r;
	r.x = 0.0f;
	r.y = kTopBarHeight;
	r.w = (float)m_width;
	r.h = (float)m_height - kTopBarHeight - kBottomBarHeight;
	if (r.h < 1.0f)
		r.h = 1.0f;
	return r;
}

float App::to_world_x(float screen_x) const
{
	const Rect c = canvas_rect();
	return (screen_x - c.x) / c.w - 0.5f;
}

float App::to_world_y(float screen_y) const
{
	const Rect c = canvas_rect();
	return 0.5f - (screen_y - c.y) / c.h;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::handle_key(const SDL_KeyboardEvent& key)
{
	const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;

	if (m_source_menu_open) {
		switch (key.key) {
			case SDLK_ESCAPE:
				close_source_menu();
				return;
			case SDLK_UP:
				m_source_menu_highlight = std::max(0, m_source_menu_highlight - 1);
				return;
			case SDLK_DOWN:
				m_source_menu_highlight = std::min((int)m_sources.size() - 1, m_source_menu_highlight + 1);
				return;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				select_source(m_source_menu_highlight);
				close_source_menu();
				return;
			default:
				return;		// swallow everything else: the menu is modal
		}
	}

	if (m_editing_osc) {
		switch (key.key) {
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				m_editing_osc = false;
				SDL_StopTextInput(m_window);
				commit_osc_field();
				return;
			case SDLK_ESCAPE:
				m_editing_osc = false;
				SDL_StopTextInput(m_window);
				m_osc_field = m_osc_field_backup;
				m_field_valid = true;
				return;
			case SDLK_BACKSPACE:
				if (!m_osc_field.empty())
					m_osc_field.erase(m_osc_field.size() - 1);
				return;
			default:
				return;
		}
	}

	switch (key.key) {
		case SDLK_ESCAPE:
			m_quit = true;
			break;
		case SDLK_Q:
			if (ctrl)
				m_quit = true;
			break;
		case SDLK_F:
			toggle_fullscreen();
			break;
		case SDLK_S:
			cycle_source();
			break;
		case SDLK_PLUS:
		case SDLK_EQUALS:
		case SDLK_KP_PLUS:
			change_number(1);
			break;
		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			change_number(-1);
			break;
		default:
			break;
	}
}

void App::handle_mouse_down(float x, float y)
{
	const Rect c = canvas_rect();

	// The drop-down is modal: a click either picks an entry or dismisses it.
	if (m_source_menu_open) {
		const int item = menu_item_at(x, y);
		close_source_menu();
		if (item >= 0)
			select_source(item);
		return;
	}

	// OSC field: click to start editing.
	const std::string osc_label = "OSC: ";
	float field_x = kPadding + text_width(osc_label);
	const Rect field(field_x, (float)m_height - kBottomBarHeight,
		text_width(m_osc_field) + 2.0f * kPadding, kBottomBarHeight);
	if (field.contains(x, y)) {
		m_editing_osc = true;
		m_osc_field_backup = m_osc_field;
		SDL_StartTextInput(m_window);
		return;
	}

	if (!c.contains(x, y))
		return;

	const float wx = to_world_x(x);
	const float wy = to_world_y(y);

	m_pointer_down = true;
	m_pointer_tracking = false;	// first motion has no meaningful delta
	m_pointer_x = wx;
	m_pointer_y = wy;

	for (size_t i = 0; i < m_filters.size(); i++) {
		if (m_filters[i]->PointerPress(SDL_BUTTON_LEFT, wx, wy))
			break;
	}
}

void App::handle_mouse_up(float, float)
{
	m_pointer_down = false;
	m_pointer_tracking = false;

	for (size_t i = 0; i < m_filters.size(); i++)
		m_filters[i]->PointerRelease(SDL_BUTTON_LEFT, 0.0f, 0.0f);
}

void App::handle_motion(float x, float y)
{
	const Rect c = canvas_rect();
	const float wx = to_world_x(x);
	const float wy = to_world_y(y);

	const float dx = (m_pointer_tracking && c.contains(x, y)) ? (wx - m_pointer_x) : 0.0f;
	const float dy = (m_pointer_tracking && c.contains(x, y)) ? (wy - m_pointer_y) : 0.0f;

	if (m_pointer_down) {
		for (size_t i = 0; i < m_filters.size(); i++)
			m_filters[i]->PointerMovement(wx, wy, dx, dy);
	}

	m_pointer_x = wx;
	m_pointer_y = wy;
	m_pointer_tracking = c.contains(x, y);
}

void App::handle_event(const SDL_Event& event)
{
	switch (event.type) {
		case SDL_EVENT_QUIT:
			m_quit = true;
			break;

		case SDL_EVENT_KEY_DOWN:
			if (!event.key.repeat)
				handle_key(event.key);
			break;

		case SDL_EVENT_TEXT_INPUT:
			if (m_editing_osc && std::strlen(event.text.text) < 64)
				m_osc_field += event.text.text;
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (event.button.button == SDL_BUTTON_LEFT) {
				// Toolbar / bottom bar hit tests use the button position.
				const float mx = event.button.x;
				if (event.button.y < kTopBarHeight) {
					// Same rectangles render_chrome() draws.
					if (mx >= 4.0f && mx < 26.0f)
						change_number(-1);
					else if (mx >= 52.0f && mx < 74.0f)
						change_number(1);
					else
						open_source_menu();
				}
				else {
					handle_mouse_down(mx, event.button.y);
				}
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.button == SDL_BUTTON_LEFT)
				handle_mouse_up(event.button.x, event.button.y);
			break;

		case SDL_EVENT_MOUSE_MOTION:
			handle_motion(event.motion.x, event.motion.y);
			break;

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			SDL_GetWindowSize(m_window, &m_width, &m_height);
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			if (m_editing_osc) {
				m_editing_osc = false;
				SDL_StopTextInput(m_window);
				commit_osc_field();
			}
			break;

		default:
			break;
	}
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void App::select_source(int index)
{
	if (m_sources.empty())
		return;

	if (index < 0)
		index = 0;
	if (index >= (int)m_sources.size())
		index = (int)m_sources.size() - 1;

	m_source_index = index;
	const std::string id = m_sources[(size_t)index].id;

	if (!m_audio->Open(id))
		fprintf(stderr, "Audio: could not open '%s'\n", id.empty() ? "default" : id.c_str());

	refresh_status();
}

void App::cycle_source()
{
	if (m_sources.empty())
		return;
	select_source((m_source_index + 1) % (int)m_sources.size());
}

void App::open_source_menu()
{
	if (m_sources.empty())
		return;
	m_source_menu_open = true;
	m_source_menu_highlight = m_source_index;
}

void App::close_source_menu()
{
	m_source_menu_open = false;
}

// --- source drop-down ------------------------------------------------------

float App::menu_item_height() const
{
	// 16 px per entry is comfortable; shrink to fit the canvas when a machine
	// exposes dozens of ALSA devices, but never below something clickable.
	const float wanted = 16.0f;
	const float count = (float)m_sources.size();
	if (count <= 0.0f)
		return wanted;

	const Rect c = canvas_rect();
	const float fits = (c.h - kPadding) / count;
	return std::max(10.0f, std::min(wanted, fits));
}

Rect App::menu_rect() const
{
	Rect r;
	r.x = 88.0f;					// under the "Source:" label
	r.y = kTopBarHeight;
	r.w = 220.0f;
	r.h = menu_item_height() * (float)m_sources.size();

	if (r.x + r.w > (float)m_width)
		r.x = std::max(0.0f, (float)m_width - r.w - kPadding);
	return r;
}

int App::menu_item_at(float x, float y) const
{
	if (m_sources.empty())
		return -1;

	const Rect r = menu_rect();
	if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h)
		return -1;

	const int index = (int)((y - r.y) / menu_item_height());
	if (index < 0 || index >= (int)m_sources.size())
		return -1;
	return index;
}

void App::change_number(int delta)
{
	m_number += delta;
	if (m_number < 1) m_number = 1;
	if (m_number > 100) m_number = 100;

	char title[64];
	snprintf(title, sizeof(title), "%s %02d", APPLICATION_NAME, m_number);
	SDL_SetWindowTitle(m_window, title);
}

void App::toggle_fullscreen()
{
	m_fullscreen = !m_fullscreen;
	SDL_SetWindowFullscreen(m_window, m_fullscreen);
}

void App::commit_osc_field()
{
	// "host:port", "host" or ":port".
	const std::string::size_type colon = m_osc_field.rfind(':');
	std::string host = m_osc_field;
	std::string port = DEFAULT_OSC_PORT;
	if (colon != std::string::npos) {
		host = m_osc_field.substr(0, colon);
		port = m_osc_field.substr(colon + 1);
	}

	if (m_bus->set_destination(host.c_str(), port.c_str())) {
		char normalised[96];
		snprintf(normalised, sizeof(normalised), "%s:%s",
			m_bus->destination_host(), m_bus->destination_port());
		m_osc_field = normalised;
		m_field_valid = true;
	}
	else {
		m_field_valid = false;
	}
}

void App::refresh_status()
{
	const bool open = m_audio && m_audio->IsOpen();
	const unsigned long reconnects = m_audio ? m_audio->ReconnectCount() : 0;

	if (!open)
		snprintf(m_status, sizeof(m_status), "no audio input");
	else if (reconnects > 0)
		snprintf(m_status, sizeof(m_status), "%s (reconnects: %lu)",
			m_audio->GetBackendName(), reconnects);
	else
		snprintf(m_status, sizeof(m_status), "%s", m_audio->GetBackendName());
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void App::step()
{
	m_audio->Poll();	// reopen the stream if the device went away

	// A stream that never opened gets one retry per second.
	static unsigned retry_counter = 0;
	if (m_audio && !m_audio->IsOpen() && ++retry_counter % 60 == 0) {
		const std::string id = m_sources.empty() ? m_source_argument : m_sources[(size_t)m_source_index].id;
		m_audio->Open(id);
	}

	m_audio->CopyMagnitudes(m_magnitudes, sa::kNumBars);

	char address[ADDRESS_BUFFER_SIZE + 1];
	for (size_t i = 0; i < m_filters.size() && i < m_last_sent.size(); i++) {
		Filter* filter = m_filters[i].get();
		filter->Update(m_magnitudes, sa::kNumBars);

		const float value = filter->GetActivation();
		if (std::fabs(value - m_last_sent[i]) < OSC_ACTIVATION_DEADBAND)
			continue;

		m_last_sent[i] = value;
		snprintf(address, sizeof(address), "/spectrum-analyzer/%02d/%s", m_number, filter->GetName());
		m_bus->send_float(address, value);
	}

	refresh_status();
}

void App::render_grid()
{
	const Rect c = canvas_rect();

	SDL_SetRenderDrawColor(m_renderer, 51, 51, 51, 255);

	for (int i = 0; i < NUM_HORIZONTAL_LINES; i++) {
		const float y = c.y + c.h * (float)i / (float)NUM_HORIZONTAL_LINES;
		SDL_RenderLine(m_renderer, c.x, y, c.x + c.w, y);
	}
	for (int i = 0; i < NUM_VERTICAL_LINES; i++) {
		const float x = c.x + c.w * (float)i / (float)NUM_VERTICAL_LINES;
		SDL_RenderLine(m_renderer, x, c.y, x, c.y + c.h);
	}
}

void App::render_samples()
{
	const Rect c = canvas_rect();
	const float bar_width = c.w / (float)sa::kNumBars;

	SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 200);

	for (int i = 0; i < sa::kNumBars; i++) {
		const float h = m_magnitudes[i] * c.h;
		if (h <= 0.0f)
			continue;

		SDL_FRect bar;
		bar.x = c.x + bar_width * (float)i + 1.0f;
		bar.y = c.y + c.h - h;
		bar.w = bar_width - 2.0f;
		bar.h = h;
		SDL_RenderFillRect(m_renderer, &bar);
	}
}

void App::render_filter(const Filter& filter)
{
	const Rect c = canvas_rect();

	// World -> screen: the filter box is centred on (X, Y) with size (W, H).
	const float box_w = filter.Width() * c.w;
	const float box_h = filter.Height() * c.h;
	SDL_FRect box;
	box.w = box_w;
	box.h = box_h;
	box.x = c.x + (filter.X() + 0.5f) * c.w - box_w * 0.5f;
	box.y = c.y + (0.5f - filter.Y()) * c.h - box_h * 0.5f;

	// Background (translucent, like the additive blend of the GTK version).
	SDL_SetRenderDrawColor(m_renderer,
		(Uint8)(filter.Red() * 255.0f),
		(Uint8)(filter.Green() * 255.0f),
		(Uint8)(filter.Blue() * 255.0f),
		150);
	SDL_RenderFillRect(m_renderer, &box);

	// Border.
	SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 60);
	SDL_RenderRect(m_renderer, &box);

	// Quadrant lines while hovered.
	if (filter.Hovered()) {
		SDL_RenderLine(m_renderer, box.x + box.w * 0.5f, box.y,
			box.x + box.w * 0.5f, box.y + box.h);
		SDL_RenderLine(m_renderer, box.x, box.y + box.h * 0.5f,
			box.x + box.w, box.y + box.h * 0.5f);
	}

	// Activation bar, centred on the box.
	const float bar_w = box.w * 0.333f;
	const float bar_h = box.h * 0.333f;

	SDL_FRect track;
	track.x = box.x + (box.w - bar_w) * 0.5f;
	track.y = box.y + (box.h - bar_h) * 0.5f;
	track.w = bar_w;
	track.h = bar_h;

	SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 230);
	SDL_RenderFillRect(m_renderer, &track);

	SDL_FRect fill = track;
	fill.h = bar_h * filter.GetActivation();
	fill.y = track.y + bar_h - fill.h;
	SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 200);
	SDL_RenderFillRect(m_renderer, &fill);
}

void App::render_filters()
{
	for (size_t i = 0; i < m_filters.size(); i++)
		render_filter(*m_filters[i]);
}

void App::render_chrome()
{
	const int top_baseline = (int)((kTopBarHeight - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * 0.5f) + 4;

	SDL_SetRenderDrawColor(m_renderer, 26, 30, 32, 255);
	SDL_FRect top = { 0.0f, 0.0f, (float)m_width, kTopBarHeight };
	SDL_FRect bottom = { 0.0f, (float)m_height - kBottomBarHeight, (float)m_width, kBottomBarHeight };
	SDL_RenderFillRect(m_renderer, &top);
	SDL_RenderFillRect(m_renderer, &bottom);

	const SDL_Color normal = { 230, 230, 230, 255 };
	const SDL_Color dim = { 150, 150, 150, 255 };

	// Analyzer number with its two buttons.
	SDL_SetRenderDrawColor(m_renderer, 60, 60, 60, 255);
	SDL_FRect minus = { 4.0f, 4.0f, 22.0f, kTopBarHeight - 8.0f };
	SDL_FRect plus = { 52.0f, 4.0f, 22.0f, kTopBarHeight - 8.0f };
	SDL_RenderFillRect(m_renderer, &minus);
	SDL_RenderFillRect(m_renderer, &plus);

	SDL_SetRenderDrawColor(m_renderer, normal.r, normal.g, normal.b, normal.a);
	SDL_RenderDebugText(m_renderer, minus.x + 7.0f, (float)top_baseline, "-");
	SDL_RenderDebugText(m_renderer, plus.x + 7.0f, (float)top_baseline, "+");

	char number[8];
	snprintf(number, sizeof(number), "%02d", m_number);
	SDL_RenderDebugText(m_renderer, 30.0f, (float)top_baseline, number);

	// Source name (click anywhere else in the toolbar to cycle).
	std::string source = "Source: default";
	if (!m_sources.empty() && m_source_index >= 0 && m_source_index < (int)m_sources.size()) {
		source = "Source: " + m_sources[(size_t)m_source_index].description;
		if (m_sources[(size_t)m_source_index].is_monitor)
			source += " (monitor)";
	}
	if (source.size() > 46)
		source = source.substr(0, 44) + "...";
	SDL_RenderDebugText(m_renderer, 88.0f, (float)top_baseline, source.c_str());

	// Bottom bar: OSC destination field.
	const int bottom_baseline = (int)((float)m_height - kBottomBarHeight
		+ (kBottomBarHeight - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * 0.5f) + 4;

	SDL_SetRenderDrawColor(m_renderer, dim.r, dim.g, dim.b, dim.a);
	SDL_RenderDebugText(m_renderer, kPadding, (float)bottom_baseline, "OSC:");

	const float field_x = kPadding + text_width("OSC: ");
	const std::string shown = m_editing_osc ? (m_osc_field + "|") : m_osc_field;

	SDL_SetRenderDrawColor(m_renderer, 39, 42, 47, 255);
	SDL_FRect field = { field_x, (float)m_height - kBottomBarHeight + 4.0f,
		text_width(shown) + 2.0f * kPadding, kBottomBarHeight - 8.0f };
	SDL_RenderFillRect(m_renderer, &field);

	SDL_SetRenderDrawColor(m_renderer,
		m_field_valid ? normal.r : 220,
		m_field_valid ? normal.g : 80,
		m_field_valid ? normal.b : 80,
		255);
	SDL_RenderDebugText(m_renderer, field_x + kPadding, (float)bottom_baseline, shown.c_str());

	// Bottom bar, right aligned: backend state.
	const float status_x = (float)m_width - text_width(m_status) - kPadding;
	SDL_SetRenderDrawColor(m_renderer, dim.r, dim.g, dim.b, dim.a);
	SDL_RenderDebugText(m_renderer, status_x, (float)bottom_baseline, m_status);

}

void App::render_source_menu()
{
	if (!m_source_menu_open || m_sources.empty())
		return;

	const Rect r = menu_rect();
	const float item_h = menu_item_height();

	SDL_SetRenderDrawColor(m_renderer, 20, 23, 25, 245);
	SDL_FRect panel = { r.x, r.y, r.w, r.h };
	SDL_RenderFillRect(m_renderer, &panel);

	for (int i = 0; i < (int)m_sources.size(); i++) {
		const bool selected = (i == m_source_index);
		const bool highlighted = (i == m_source_menu_highlight);

		if (highlighted || selected) {
			SDL_SetRenderDrawColor(m_renderer,
				highlighted ? 51 : 38,
				highlighted ? 102 : 70,
				highlighted ? 51 : 60,
				255);
			SDL_FRect row = { r.x + 1.0f, r.y + item_h * (float)i, r.w - 2.0f, item_h };
			SDL_RenderFillRect(m_renderer, &row);
		}

		std::string label = m_sources[(size_t)i].description;
		if (m_sources[(size_t)i].is_monitor)
			label += " (monitor)";
		if (label.size() > 26)
			label = label.substr(0, 24) + "..";

		SDL_SetRenderDrawColor(m_renderer, selected ? 255 : 215, selected ? 255 : 215, selected ? 255 : 215, 255);
		SDL_RenderDebugText(m_renderer, r.x + kPadding * 0.5f,
			r.y + item_h * (float)i + (item_h - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * 0.5f,
			label.c_str());
	}

	SDL_SetRenderDrawColor(m_renderer, 120, 120, 120, 255);
	SDL_RenderRect(m_renderer, &panel);
}

void App::render()
{
	// Keep drawing coordinates in points, whatever the display scale is.
	int out_w = 0, out_h = 0;
	SDL_GetCurrentRenderOutputSize(m_renderer, &out_w, &out_h);
	if (m_width > 0 && m_height > 0 && out_w > 0 && out_h > 0)
		SDL_SetRenderScale(m_renderer, (float)out_w / (float)m_width, (float)out_h / (float)m_height);

	SDL_SetRenderDrawColor(m_renderer, 13, 13, 13, 255);
	SDL_RenderClear(m_renderer);

	render_samples();
	render_grid();
	render_filters();
	render_chrome();
	render_source_menu();		// on top of the canvas and the chrome

	SDL_RenderPresent(m_renderer);
}

void App::run()
{
	Uint64 previous = SDL_GetTicks();

	while (!m_quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event))
			handle_event(event);

		const Uint64 now = SDL_GetTicks();
		// 60 fps ceiling: with vsync the present blocks anyway, without it this
		// keeps the loop from spinning.
		if (now - previous >= 16) {
			previous = now;
			step();
			render();
		}
		else {
			SDL_Delay(1);
		}
	}
}

} // anonymous namespace

int main(int argc, char* argv[])
{
	if (argc > 1 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0)) {
		printf("%s %s (SDL3)\n", APPLICATION_NAME, SPECTRUM_ANALYZER_VERSION);
		return 0;
	}

	if (argc > 1 && std::strcmp(argv[1], "--list-sources") == 0) {
		const std::vector<AudioSourceInfo> sources = AudioSampler::EnumerateSources();
		for (size_t i = 0; i < sources.size(); i++) {
			printf("%-55s %s%s\n",
				sources[i].id.empty() ? "(default)" : sources[i].id.c_str(),
				sources[i].description.c_str(),
				sources[i].is_monitor ? " [monitor]" : "");
		}
		return 0;
	}

	int result = 0;
	{
		// The App lives in its own scope: its destructor destroys the renderer
		// and the window, and only then may SDL_Quit() run.
		App app;
		const std::string device = (argc > 1) ? argv[1] : std::string();

		if (!app.init(device))
			result = 1;
		else
			app.run();
	}

	SDL_Quit();
	return result;
}
