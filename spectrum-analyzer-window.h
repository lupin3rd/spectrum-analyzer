#ifndef __SPECTRUM_ANALYZER_WINDOW_H__
#define __SPECTRUM_ANALYZER_WINDOW_H__

#include <gtkmm.h>

#include <string>
#include <vector>
using namespace std;

#include "my-drawing-area.h"
#include "audio-sampler.h"
#include "filter.h"

class SpectrumAnalyzerWindow : public Gtk::Window
{
public:
	SpectrumAnalyzerWindow();
	virtual ~SpectrumAnalyzerWindow();

	void set_audio_sampler(AudioSampler* pAudioSampler) { m_audio_sampler = pAudioSampler; };
	bool on_drawing_area_render(const Glib::RefPtr<Gdk::GLContext>& context);
	void trigger_redraw();
	bool update();

	bool on_motion_notify_event(GdkEventMotion* event);
	void on_spectrum_analyzer_number_spinbox_changed();
	void update_window_title();
	void on_fullscreen_button_clicked();

	bool on_window_button_press_event(GdkEventButton* event);

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
	Gtk::Dialog* m_quit_dialog;

	MyDrawingArea m_drawing_area;
	AudioSampler* m_audio_sampler;

	vector<Filter*> m_filters;

	gint m_spectrum_analyzer_number;

	bool on_drawing_area_button_press_event(GdkEventButton* event);
	bool on_window_state_event(GdkEventWindowState* event);
	void on_quit_dialog_response(int response_id);
	void populate_sources();
	void on_source_changed();
	void on_osc_destination_changed();
	void draw();

	bool on_delete_event(GdkEventAny *event);
	bool m_is_fullscreen;
};

#endif
