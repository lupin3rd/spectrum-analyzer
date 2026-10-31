// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __SPECTRUM_ANALYZER_WINDOW_H__
#define __SPECTRUM_ANALYZER_WINDOW_H__

#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

#include "audio-sampler.h"
#include "filter.h"
#include "my-drawing-area.h"
#include "spectrum-dsp.h"

class SpectrumAnalyzerWindow : public Gtk::Window
{
public:
	SpectrumAnalyzerWindow();
	virtual ~SpectrumAnalyzerWindow();

	void set_audio_sampler(AudioSampler* pAudioSampler) { m_audio_sampler = pAudioSampler; }
	bool on_drawing_area_render(const Glib::RefPtr<Gdk::GLContext>& context);
	void trigger_redraw();
	bool update();

	bool on_motion_notify_event(GdkEventMotion* event);
	void on_spectrum_analyzer_number_spinbox_changed();
	void update_window_title();
	void on_fullscreen_button_clicked();

	bool on_window_button_press_event(GdkEventButton* event);

	// True when the window is on screen and not iconified: only then is
	// redrawing worth any CPU.  OSC keeps running either way.
	bool should_render() const;

private:
	// Columns for the input-source combo box.
	class SourceColumns : public Gtk::TreeModel::ColumnRecord
	{
	public:
		SourceColumns()
		{
			add(m_id);
			add(m_description);
		}

		Gtk::TreeModelColumn<Glib::ustring> m_id;
		Gtk::TreeModelColumn<Glib::ustring> m_description;
	};

	Gtk::Box m_window_vbox;
	Gtk::Box m_toolbar;
	Gtk::Box m_osc_bar;
	Gtk::SpinButton m_spectrum_analyzer_number_spinbutton;
	Gtk::ComboBox m_source_combo;
	SourceColumns m_source_columns;
	Glib::RefPtr<Gtk::ListStore> m_source_model;
	Gtk::Entry m_osc_host_entry;
	Gtk::SpinButton m_osc_port_spinbutton;
	Gtk::Button m_fullscreen_button;
	Gtk::Label m_status_label;
	std::unique_ptr<Gtk::Dialog> m_quit_dialog;

	MyDrawingArea m_drawing_area;
	AudioSampler* m_audio_sampler;

	std::vector<std::unique_ptr<Filter> > m_filters;
	// Activation last actually sent per filter, so the OSC stream is not
	// saturated with sub-pixel changes.
	std::vector<float> m_last_sent_activation;

	gint m_spectrum_analyzer_number;

	// Pointer state for the drag deltas.  Members, not function statics: two
	// windows in one process (and a re-entry into the widget) would otherwise
	// share them and jump.
	bool m_pointer_tracking;
	float m_pointer_x;
	float m_pointer_y;

	bool on_drawing_area_button_press_event(GdkEventButton* event);
	bool on_window_state_event(GdkEventWindowState* event);
	void on_quit_dialog_response(int response_id);
	void populate_sources();
	void on_source_changed();
	void on_osc_destination_changed();
	void on_osc_host_activate();
	bool on_osc_host_focus_out(GdkEventFocus* event);
	void update_status_label();
	void draw();

	bool on_delete_event(GdkEventAny* event);
	bool m_is_fullscreen;
};

#endif
