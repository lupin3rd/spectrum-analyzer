#define WINDOW_TITLE ("Spectrum Analyzer %02d")

#include <gtkmm.h>
#include <epoxy/gl.h>

#include <cstdio>
#include <cstdlib>

#include "gl-compat.h"
#include "spectrum-analyzer.h"
#include "spectrum-analyzer-window.h"

#define MIN_DEVICE_NUMBER (1)
#define MAX_DEVICE_NUMBER (100)

#define WINDOW_MINIMUM_WIDTH (240)
#define MSG_CONFIRM_QUIT ("Confirm Quit?")

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
		m_filters(),
		m_spectrum_analyzer_number(1),
		m_is_fullscreen(false)
{
	m_quit_dialog = new Gtk::Dialog(MSG_CONFIRM_QUIT, *this);
	m_quit_dialog->add_button("_Cancel", Gtk::RESPONSE_CANCEL);
	m_quit_dialog->add_button("_Quit", Gtk::RESPONSE_OK);
	m_quit_dialog->set_default_response(Gtk::RESPONSE_CANCEL);
	m_quit_dialog->signal_response().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_quit_dialog_response));

	m_filters.push_back(new Filter(-0.375, 0.41, (char*)"Red", 1.0, 0.0, 0.0));
	m_filters.push_back(new Filter(-0.125, 0.41, (char*)"Green", 0.0, 1.0, 0.0));
	m_filters.push_back(new Filter( 0.125, 0.41, (char*)"Blue", 0.0, 0.0, 1.0));
	m_filters.push_back(new Filter( 0.375, 0.41, (char*)"Yellow", 1.0, 1.0, 0.0));

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
		Gtk::Label* source_label = new Gtk::Label("Source:");
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
		Gtk::Label* spacer = new Gtk::Label();
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

	//
	// OSC output bar, at the bottom of the window
	//
	m_osc_bar.set_spacing(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_top(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_bottom(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_start(STANDARD_WIDGET_SPACING);
	m_osc_bar.set_margin_end(STANDARD_WIDGET_SPACING);

		// OSC: destination IP / host
		Gtk::Label* osc_label = new Gtk::Label("OSC:");
		m_osc_bar.pack_start(*osc_label, false, false);

		m_osc_host_entry.set_text(DEFAULT_OSC_HOST);
		m_osc_host_entry.set_width_chars(8);
		m_osc_host_entry.set_tooltip_text("Destination IP / host for OSC messages");
		m_osc_bar.pack_start(m_osc_host_entry, false, false);

		// OSC: destination UDP port
		m_osc_port_spinbutton.set_range(1, 65535);
		m_osc_port_spinbutton.set_increments(1, 100);
		m_osc_port_spinbutton.set_value(atoi(DEFAULT_OSC_PORT));
		m_osc_port_spinbutton.set_tooltip_text("Destination UDP port for OSC messages");
		m_osc_bar.pack_start(m_osc_port_spinbutton, false, false);

		m_osc_host_entry.signal_changed().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_osc_destination_changed));
		m_osc_port_spinbutton.signal_value_changed().connect(sigc::mem_fun(*this, &SpectrumAnalyzerWindow::on_osc_destination_changed));

		// keep the controls left-aligned
		Gtk::Label* osc_spacer = new Gtk::Label();
		m_osc_bar.pack_start(*osc_spacer, true, true);

	m_window_vbox.pack_start(m_osc_bar, false, false);
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
	snprintf(buffer, 200, WINDOW_TITLE, m_spectrum_analyzer_number);
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

	if (!m_audio_sampler->Open(source)) {
		fprintf(stderr, "ERROR: could not open source '%s'; falling back to default\n", source ? source : "default");
		if (!m_audio_sampler->Open(NULL)) {
			fprintf(stderr, "ERROR: could not open the default source either\n");
		}
		// Reflect the fallback in the UI (row 0 is always "Default").
		if (m_source_combo.get_active_row_number() != 0)
			m_source_combo.set_active(0);
	}
}

bool SpectrumAnalyzerWindow::on_motion_notify_event(GdkEventMotion* event)
{
	static float last_x=0.0, last_y=0.0;

	Gtk::Allocation allocation = m_drawing_area.get_allocation();
	float x = ((float)event->x / (float)allocation.get_width()) - 0.5;
	float y = -(((float)event->y / (float)allocation.get_height()) - 0.5);

	vector<Filter*>::iterator itFilter = m_filters.begin();
	for(; itFilter < m_filters.end(); itFilter++) {
		Filter* filter = *itFilter;
		filter->PointerMovement(x, y, (x - last_x), (y - last_y));
	}

	last_x = x; last_y = y;
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
	Gtk::Allocation allocation = m_drawing_area.get_allocation();

	vector<Filter*>::iterator itFilter = m_filters.begin();
	if(event->type == GDK_BUTTON_PRESS) {
		for(; itFilter < m_filters.end(); itFilter++) {
			Filter* filter = *itFilter;
			if(filter->PointerPress(event->button, ((float)event->x / (float)allocation.get_width()) - 0.5, -(((float)event->y / (float)allocation.get_height()) - 0.5)))
				break;
		}
		return true;
	}
	else if(event->type == GDK_BUTTON_RELEASE) {
		for(; itFilter < m_filters.end(); itFilter++) {
			Filter* filter = *itFilter;
			filter->PointerRelease(event->button, ((float)event->x / (float)allocation.get_width()) - 0.5, -(((float)event->y / (float)allocation.get_height()) - 0.5));
		}
		return true;
	}
	else {
		return false;
	}
}

bool SpectrumAnalyzerWindow::on_drawing_area_render(const Glib::RefPtr<Gdk::GLContext>&)
{
	draw();
	return true;
}

void SpectrumAnalyzerWindow::draw()
{
	glClear(GL_COLOR_BUFFER_BIT);

	float magnitudes[NUM_BARS];
	m_audio_sampler->CopyMagnitudes(magnitudes, NUM_BARS);

	render_samples(magnitudes, NUM_BARS);
	render_grid(NUM_HORIZONTAL_LINES, NUM_VERTICAL_LINES);

	// Render boxes
	vector<Filter*>::iterator itFilter = m_filters.begin();
	for(; itFilter < m_filters.end(); itFilter++) {
		Filter* filter = *itFilter;
		filter->Render();
	}
}

#define ADDRESS_BUFFER_SIZE (200)

bool SpectrumAnalyzerWindow::update()
{
	char address_buffer[ADDRESS_BUFFER_SIZE+1];

	float magnitudes[NUM_BARS];
	m_audio_sampler->CopyMagnitudes(magnitudes, NUM_BARS);

	vector<Filter*>::iterator itFilter = m_filters.begin();
	for(; itFilter < m_filters.end(); itFilter++) {
		Filter* filter = *itFilter;

		// Update returns 'true' if activation value changed
		if(filter->Update(magnitudes, NUM_BARS)) {
			snprintf(address_buffer, ADDRESS_BUFFER_SIZE, "/spectrum-analyzer/%02d/%s", m_spectrum_analyzer_number, filter->GetName());
			send_float_packet(address_buffer, filter->GetActivation());
		}
	}
	return true;
}

void SpectrumAnalyzerWindow::trigger_redraw()
{
	m_drawing_area.trigger_redraw();
}

void SpectrumAnalyzerWindow::on_osc_destination_changed()
{
	if(!g_message_bus)
		return;

	char port[16];
	snprintf(port, sizeof(port), "%d", m_osc_port_spinbutton.get_value_as_int());
	g_message_bus->set_destination(m_osc_host_entry.get_text().c_str(), port);
}

bool SpectrumAnalyzerWindow::on_delete_event(GdkEventAny *event)
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
