// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#define WINDOW_TITLE ("Spectrum Analyzer %02d")

#include <gtkmm.h>
#include <epoxy/gl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gl-compat.h"
#include "math-utils.h"
#include "spectrum-analyzer.h"
#include "spectrum-analyzer-window.h"

#define MIN_DEVICE_NUMBER (1)
#define MAX_DEVICE_NUMBER (100)

#define WINDOW_MINIMUM_WIDTH (240)
#define MSG_CONFIRM_QUIT ("Confirm Quit?")

// Minimum change in activation worth an OSC message.  Without this every bar
// tick produces 4 packets because the smoothed value always differs in the last
// decimal, which floods the network and the receiving software for no visible
// benefit.
#define OSC_ACTIVATION_DEADBAND (0.002f)

#define ADDRESS_BUFFER_SIZE (200)

static void render_grid(int horizontal, int vertical)
{
	int i;

	glc::color4f(0.2, 0.2, 0.2, 1.0);
	glc::push_matrix();
		glc::translate(-0.5f, -0.5f);		// Put 0.0 in bottom left corner

		glc::begin(GL_LINES);
		for(i=0 ; i<horizontal ; i++) {
			glc::vertex3f(0.0, (float)i / (float)horizontal, 0.0);
			glc::vertex3f(1.0, (float)i / (float)horizontal, 0.0);
		}
		for(i=0 ; i<vertical ; i++) {
			glc::vertex3f((float)i / (float)vertical, 0.0, 0.0);
			glc::vertex3f((float)i / (float)vertical, 1.0, 0.0);
		}
		glc::end();
	glc::pop_matrix();
}

static void render_samples(float* bar_heights, int count)
{
	int i;

	glc::push_matrix();
		glc::translate(-0.5f, -0.5f);		// Put 0.0 in bottom left corner

		glc::begin(GL_QUADS);
		for(i=0 ; i<count ; i++) {
			float x1 = (GLfloat)i/(GLfloat)count;
			float y1 = bar_heights[i];

			float x2 = (GLfloat)(i+1)/(GLfloat)count;
			float y2 = 0.0;

			glc::color4f(0.5, 0.5, 0.5, 0.4);
			glc::vertex3f(x2, y1, 0.0);
			glc::vertex3f(x2, y2, 0.0);

			glc::color4f(0.5, 0.5, 0.5, 0.8);
			glc::vertex3f(x1, y2, 0.0);
			glc::vertex3f(x1, y1, 0.0);
		}
		glc::end();
	glc::pop_matrix();
}

SpectrumAnalyzerWindow::SpectrumAnalyzerWindow()
	: m_window_vbox(Gtk::ORIENTATION_VERTICAL),
		m_toolbar(Gtk::ORIENTATION_HORIZONTAL),
		m_osc_bar(Gtk::ORIENTATION_HORIZONTAL),
		m_spectrum_analyzer_number_spinbutton(),
		m_fullscreen_button(),
		m_audio_sampler(NULL),
		m_spectrum_analyzer_number(1),
		m_pointer_tracking(false),
		m_pointer_x(0.0f),
		m_pointer_y(0.0f),
		m_is_fullscreen(false)
{
	m_quit_dialog.reset(new Gtk::Dialog(MSG_CONFIRM_QUIT, *this));
	m_quit_dialog->add_button("_Cancel", Gtk::RESPONSE_CANCEL);
	m_quit_dialog->add_button("_Quit", Gtk::RESPONSE_OK);
	m_quit_dialog->set_default_response(Gtk::RESPONSE_CANCEL);
	m_quit_dialog->signal_response().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_quit_dialog_response));

	m_filters.push_back(std::unique_ptr<Filter>(new Filter(-0.375, 0.41, "Red", 1.0, 0.0, 0.0)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter(-0.125, 0.41, "Green", 0.0, 1.0, 0.0)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter( 0.125, 0.41, "Blue", 0.0, 0.0, 1.0)));
	m_filters.push_back(std::unique_ptr<Filter>(new Filter( 0.375, 0.41, "Yellow", 1.0, 1.0, 0.0)));
	m_last_sent_activation.assign(m_filters.size(), -1.0f);

	update_window_title();
	set_border_width(0);
	set_resizable(true);
	set_default_size(480, 300);		// comfortable start; the window can still shrink to its minimum

	m_toolbar.set_size_request(WINDOW_MINIMUM_WIDTH,-1);		// floor for the toolbar width; it can still grow/shrink with the window

	// One vbox to rule them all
	m_window_vbox.set_spacing(0);		// aesthetics
	add(m_window_vbox);

	// Window dragging (in unused space)
	add_events(Gdk::BUTTON_PRESS_MASK);
	signal_button_press_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_window_button_press_event));

	// Keep track of the fullscreen state (so the toolbar button toggles).
	signal_window_state_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_window_state_event));

	//
	// Toolbar is the first item in vbox
	//
	m_toolbar.set_spacing(STANDARD_WIDGET_SPACING);

		// Toolbar: Spectrum Analyzer Number Spinbutton
		m_spectrum_analyzer_number_spinbutton.set_tooltip_text("Spectrum Analyzer Number");
		m_spectrum_analyzer_number_spinbutton.set_width_chars(3);
		m_spectrum_analyzer_number_spinbutton.set_range((double)MIN_DEVICE_NUMBER, (double)MAX_DEVICE_NUMBER);
		m_spectrum_analyzer_number_spinbutton.set_increments(1.0, 1.0);
		m_spectrum_analyzer_number_spinbutton.set_value(m_spectrum_analyzer_number);
		m_toolbar.pack_start(m_spectrum_analyzer_number_spinbutton, false, true);		// booleans mean: expand and fill extra space
		m_spectrum_analyzer_number_spinbutton.signal_value_changed().connect(
			sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_spectrum_analyzer_number_spinbox_changed)
		);

		// Toolbar: Input source selector (fixed width, ellipsized, full name in tooltip)
		Gtk::Label* source_label = Gtk::manage(new Gtk::Label("Source:"));
		m_toolbar.pack_start(*source_label, false, false);

		m_source_model = Gtk::ListStore::create(m_source_columns);
		m_source_combo.set_model(m_source_model);
		Gtk::CellRendererText* source_cell = Gtk::manage(new Gtk::CellRendererText());
		source_cell->property_ellipsize() = Pango::ELLIPSIZE_END;
		source_cell->property_width() = 90;
		m_source_combo.pack_start(*source_cell, false);
		m_source_combo.add_attribute(source_cell->property_text(), m_source_columns.m_description);
		m_toolbar.pack_start(m_source_combo, false, false);

		m_source_combo.signal_changed().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_source_changed));
		populate_sources();

		// Toolbar: spacer to push further items to the far right
		Gtk::Label* spacer = Gtk::manage(new Gtk::Label());
		m_toolbar.pack_start(*spacer, true, true);		// booleans mean: expand and fill extra space

		// Toolbar: Fullscreen Button
		m_fullscreen_button.set_tooltip_text("Toggle Fullscreen");
		m_fullscreen_button.set_image_from_icon_name("view-fullscreen", Gtk::ICON_SIZE_SMALL_TOOLBAR, true);
		m_fullscreen_button.set_relief(Gtk::RELIEF_NONE);
		m_toolbar.pack_start(m_fullscreen_button, false, true);		// booleans mean: expand and fill extra space
		m_fullscreen_button.signal_clicked().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_fullscreen_button_clicked));

	m_window_vbox.pack_start(m_toolbar, false, false);

	//
	// DrawingArea (OpenGL)
	//
	m_drawing_area.set_size_request(128, 96);
	m_window_vbox.pack_start(m_drawing_area, true, true);

	m_drawing_area.signal_render().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_drawing_area_render), false);

	m_drawing_area.add_events(Gdk::POINTER_MOTION_MASK);
	m_drawing_area.signal_motion_notify_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_motion_notify_event), false);

	m_drawing_area.add_events(Gdk::BUTTON_PRESS_MASK);
	m_drawing_area.signal_button_press_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_drawing_area_button_press_event), false);

	m_drawing_area.add_events(Gdk::BUTTON_RELEASE_MASK);
	m_drawing_area.signal_button_release_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_drawing_area_button_press_event), false);

	// Pointer leaving the canvas must not leave a stale delta behind: the next
	// motion event would otherwise jump by the distance travelled outside.
	m_drawing_area.add_events(Gdk::LEAVE_NOTIFY_MASK);
	m_drawing_area.signal_leave_notify_event().connect([this](GdkEventCrossing*) {
		m_pointer_tracking = false;
		return false;
	}, false);

	//
	// OSC output bar, at the bottom of the window
	//
	m_osc_bar.set_spacing(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_top(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_bottom(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_start(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_end(STANDARD_WIDGET_SPACING);

		// OSC: destination IP / host
		Gtk::Label* osc_label = Gtk::manage(new Gtk::Label("OSC:"));
		m_osc_bar.pack_start(*osc_label, false, false);

		m_osc_host_entry.set_text(DEFAULT_OSC_HOST);
		m_osc_host_entry.set_width_chars(8);
		m_osc_host_entry.set_tooltip_text("Destination IP / host for OSC messages (Enter to apply)");
		m_osc_bar.pack_start(m_osc_host_entry, false, false);

		// OSC: destination UDP port
		m_osc_port_spinbutton.set_range(1, 65535);
		m_osc_port_spinbutton.set_increments(1, 100);
		m_osc_port_spinbutton.set_value(atoi(DEFAULT_OSC_PORT));
		m_osc_port_spinbutton.set_tooltip_text("Destination UDP port for OSC messages");
		m_osc_bar.pack_start(m_osc_port_spinbutton, false, false);

		// The port is cheap to apply (no name lookup), the host is not: applying
		// it per keystroke used to resolve a hostname on the UI thread each time.
		m_osc_port_spinbutton.signal_value_changed().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_osc_destination_changed));
		m_osc_host_entry.signal_activate().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_osc_host_activate));
		m_osc_host_entry.add_events(Gdk::FOCUS_CHANGE_MASK);
		m_osc_host_entry.signal_focus_out_event().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_osc_host_focus_out));

		// keep the controls left-aligned
		Gtk::Label* osc_spacer = Gtk::manage(new Gtk::Label());
		m_osc_bar.pack_start(*osc_spacer, true, true);

		m_status_label.set_ellipsize(Pango::ELLIPSIZE_END);
		m_status_label.set_tooltip_text("Audio backend state");
		m_osc_bar.pack_end(m_status_label, false, false);

	m_window_vbox.pack_start(m_osc_bar, false, false);

	update_status_label();
}

bool SpectrumAnalyzerWindow::on_window_state_event(GdkEventWindowState* event)
{
	m_is_fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) == GDK_WINDOW_STATE_FULLSCREEN;
	return false;
}

void SpectrumAnalyzerWindow::on_fullscreen_button_clicked()
{
	if(m_is_fullscreen)
		this->unfullscreen();
	else
		this->fullscreen();
}

void SpectrumAnalyzerWindow::on_spectrum_analyzer_number_spinbox_changed()
{
	m_spectrum_analyzer_number = m_spectrum_analyzer_number_spinbutton.get_value_as_int();
	update_window_title();
}

void SpectrumAnalyzerWindow::update_window_title()
{
	char buffer[201];
	snprintf(buffer, sizeof(buffer), WINDOW_TITLE, m_spectrum_analyzer_number);
	set_title(buffer);
}

void SpectrumAnalyzerWindow::populate_sources()
{
	m_source_model->clear();

	std::vector<AudioSourceInfo> sources = AudioSampler::EnumerateSources();
	for (size_t i = 0; i < sources.size(); i++) {
		std::string label = sources[i].description;
		if (sources[i].is_monitor)
			label += " (Monitor)";

		// "default" is a sentinel meaning "use the default source".
		Gtk::TreeModel::Row row = *m_source_model->append();
		row[m_source_columns.m_id] = sources[i].id.empty() ? "default" : sources[i].id;
		row[m_source_columns.m_description] = label;
	}

	if (!sources.empty())
		m_source_combo.set_active(0);
}

void SpectrumAnalyzerWindow::on_source_changed()
{
	Gtk::TreeModel::iterator iter = m_source_combo.get_active();
	if (!iter)
		return;

	// Show the full (untruncated) source name in the tooltip.
	m_source_combo.set_tooltip_text((*iter)[m_source_columns.m_description]);

	if (!m_audio_sampler)
		return;

	Glib::ustring id = (*iter)[m_source_columns.m_id];
	const char* source = (id.empty() || id == "default") ? NULL : id.c_str();

	if (!m_audio_sampler->Open(source ? std::string(source) : std::string())) {
		fprintf(stderr, "ERROR: could not open source '%s'; falling back to default\n", source ? source : "default");
		if (!m_audio_sampler->Open(std::string())) {
			fprintf(stderr, "ERROR: could not open the default source either\n");
		}
		// Reflect the fallback in the UI (row 0 is always "Default").
		if (m_source_combo.get_active_row_number() != 0)
			m_source_combo.set_active(0);
	}

	update_status_label();
}

void SpectrumAnalyzerWindow::update_status_label()
{
	if (!m_audio_sampler) {
		m_status_label.set_text("");
		return;
	}

	const unsigned long reconnects = m_audio_sampler->ReconnectCount();
	char buffer[96];
	if (m_audio_sampler->IsOpen()) {
		if (reconnects > 0)
			snprintf(buffer, sizeof(buffer), "%s (reconnects: %lu)",
				m_audio_sampler->GetBackendName(), reconnects);
		else
			snprintf(buffer, sizeof(buffer), "%s", m_audio_sampler->GetBackendName());
	}
	else {
		snprintf(buffer, sizeof(buffer), "no audio input");
	}
	// Called once per frame: only touch the widget when the text actually
	// changes, otherwise GTK relayouts the bar 60 times a second for nothing.
	if (m_status_label.get_text() != Glib::ustring(buffer))
		m_status_label.set_text(buffer);
}

bool SpectrumAnalyzerWindow::on_motion_notify_event(GdkEventMotion* event)
{
	const Gtk::Allocation allocation = m_drawing_area.get_allocation();
	if (allocation.get_width() <= 0 || allocation.get_height() <= 0)
		return true;

	const float x = ((float)event->x / (float)allocation.get_width()) - 0.5f;
	const float y = -(((float)event->y / (float)allocation.get_height()) - 0.5f);

	// First motion after entering the canvas has no meaningful previous
	// position: start from "no movement" instead of a huge delta.
	const float delta_x = m_pointer_tracking ? (x - m_pointer_x) : 0.0f;
	const float delta_y = m_pointer_tracking ? (y - m_pointer_y) : 0.0f;

	for (size_t i = 0; i < m_filters.size(); i++)
		m_filters[i]->PointerMovement(x, y, delta_x, delta_y);

	m_pointer_x = x;
	m_pointer_y = y;
	m_pointer_tracking = true;
	return true;
}

bool SpectrumAnalyzerWindow::on_window_button_press_event(GdkEventButton* event)
{
	if((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		begin_move_drag(event->button, event->x_root, event->y_root, event->time);
		return true;	// handled
	}
	return false;	// not handled
}

bool SpectrumAnalyzerWindow::on_drawing_area_button_press_event(GdkEventButton* event)
{
	const Gtk::Allocation allocation = m_drawing_area.get_allocation();
	if (allocation.get_width() <= 0 || allocation.get_height() <= 0)
		return true;

	const float x = ((float)event->x / (float)allocation.get_width()) - 0.5f;
	const float y = -(((float)event->y / (float)allocation.get_height()) - 0.5f);

	if(event->type == GDK_BUTTON_PRESS) {
		m_pointer_tracking = false;		// delta restarts from this press
		for (size_t i = 0; i < m_filters.size(); i++) {
			if (m_filters[i]->PointerPress(event->button, x, y))
				break;
		}
		return true;
	}

	if(event->type == GDK_BUTTON_RELEASE) {
		for (size_t i = 0; i < m_filters.size(); i++)
			m_filters[i]->PointerRelease(event->button, x, y);
		m_pointer_tracking = false;
		return true;
	}

	return false;
}

bool SpectrumAnalyzerWindow::on_drawing_area_render(const Glib::RefPtr<Gdk::GLContext>&)
{
	draw();
	return true;
}

bool SpectrumAnalyzerWindow::should_render() const
{
	if (!get_visible())
		return false;

	Glib::RefPtr<Gdk::Window> gdk_window = const_cast<SpectrumAnalyzerWindow*>(this)->get_window();
	if (!gdk_window)
		return false;

	return (gdk_window->get_state() & GDK_WINDOW_STATE_ICONIFIED) == 0;
}

void SpectrumAnalyzerWindow::draw()
{
	float magnitudes[sa::kNumBars];
	if (m_audio_sampler)
		m_audio_sampler->CopyMagnitudes(magnitudes, sa::kNumBars);
	else
		std::memset(magnitudes, 0, sizeof(magnitudes));

	glClear(GL_COLOR_BUFFER_BIT);

	// Every primitive states the blend mode it needs, so the picture no longer
	// depends on what was drawn last (the filter boxes used to leak additive
	// blending into the bars of the next frame).
	glc::blend_alpha();
	render_samples(magnitudes, sa::kNumBars);
	render_grid(NUM_HORIZONTAL_LINES, NUM_VERTICAL_LINES);

	for (size_t i = 0; i < m_filters.size(); i++)
		m_filters[i]->Render();

	glc::blend_alpha();
	glc::check_errors("frame");
}

bool SpectrumAnalyzerWindow::update()
{
	if (!m_audio_sampler)
		return true;

	float magnitudes[sa::kNumBars];
	m_audio_sampler->CopyMagnitudes(magnitudes, sa::kNumBars);

	char address_buffer[ADDRESS_BUFFER_SIZE + 1];

	for (size_t i = 0; i < m_filters.size() && i < m_last_sent_activation.size(); i++) {
		Filter* filter = m_filters[i].get();

		// Update returns 'true' if activation value changed
		filter->Update(magnitudes, sa::kNumBars);

		const float value = filter->GetActivation();
		if (std::fabs(value - m_last_sent_activation[i]) < OSC_ACTIVATION_DEADBAND)
			continue;

		m_last_sent_activation[i] = value;
		snprintf(address_buffer, sizeof(address_buffer), "/spectrum-analyzer/%02d/%s",
			m_spectrum_analyzer_number, filter->GetName());
		send_float_packet(address_buffer, value);
	}

	update_status_label();
	return true;
}

void SpectrumAnalyzerWindow::trigger_redraw()
{
	m_drawing_area.trigger_redraw();
}

void SpectrumAnalyzerWindow::on_osc_destination_changed()
{
	if (!g_message_bus)
		return;

	char port[16];
	snprintf(port, sizeof(port), "%d", m_osc_port_spinbutton.get_value_as_int());
	if (!g_message_bus->set_destination(m_osc_host_entry.get_text().c_str(), port))
		m_osc_host_entry.set_tooltip_text("Invalid OSC destination");
	else
		m_osc_host_entry.set_tooltip_text("Destination IP / host for OSC messages (Enter to apply)");
}

void SpectrumAnalyzerWindow::on_osc_host_activate()
{
	on_osc_destination_changed();
}

bool SpectrumAnalyzerWindow::on_osc_host_focus_out(GdkEventFocus*)
{
	on_osc_destination_changed();
	return false;
}

bool SpectrumAnalyzerWindow::on_delete_event(GdkEventAny*)
{
	m_quit_dialog->show();
	return true;
}

void SpectrumAnalyzerWindow::on_quit_dialog_response(int response_id)
{
	if(response_id == Gtk::RESPONSE_OK) {
		g_time_to_quit = true;
	}
	m_quit_dialog->hide();
}

SpectrumAnalyzerWindow::~SpectrumAnalyzerWindow()
{
}
