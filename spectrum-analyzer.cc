// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "spectrum-analyzer.h"
#include "audio-sampler.h"
#include "filter.h"
#include "message-bus.h"
#include "spectrum-analyzer-window.h"
#include "utils.h"
#include "version.h"

#include <gtkmm.h>
#include <glibmm/main.h>
#include <glibmm/timer.h>
#include <glibmm/miscutils.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#if !defined(__linux__)
#include <unistd.h>
#endif

#define APPLICATION_NAME ("Spectrum Analyzer")
#define APPLICATION_ID ("org.spectrum-analyzer.SpectrumAnalyzer")

#define CSS_FILE_PATH ("spectrum-analyzer.css")
#define PNG_ICON_FILE_PATH ("spectrum-analyzer-status-icon.png")
#define SVG_ICON_FILE_PATH ("spectrum-analyzer-icon.svg")

// How often to retry opening the capture device when it is not available at
// startup (no sound server yet, device not plugged in, ...).
#define AUDIO_RETRY_INTERVAL_MS (2000)

//
// Globals
//
MessageBus* g_message_bus = NULL;

SpectrumAnalyzerWindow* g_spectrum_analyzer_window = NULL;

bool g_time_to_quit = false;

static Glib::RefPtr<Gtk::Application> g_app;
static AudioSampler* g_audio_sampler = NULL;
static const double g_frame_time = 1.0 / 60.0;
static std::string g_device;

#if !defined(__APPLE__) && !defined(_WIN32)
// The tray icon is an X11/GTK3 facility: it does nothing under Wayland and does
// not exist on macOS.  Kept in a long-lived RefPtr because the local one used to
// be dropped as soon as on_startup() returned, which made the icon vanish.
static Glib::RefPtr<Gtk::StatusIcon> g_status_icon;
#endif

//
// Global helper functions
//
void send_float_packet(const char* address, float value)
{
	if (g_message_bus)
		g_message_bus->send_float(address, value);
}

void send_int_packet(const char* address, int value)
{
	if (g_message_bus)
		g_message_bus->send_int(address, value);
}

//
// Resolve an asset path relative to the directory that contains the executable,
// so the program works regardless of the current directory.
//
// This is the one genuinely platform-specific piece of path handling; it is
// separated here so a port only has to touch these ten lines (or, better,
// register the assets in a GResource).
//
static std::string executable_directory()
{
#if defined(__linux__)
	char buf[4096];
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		return Glib::path_get_dirname(buf);
	}
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(NULL, &size);
	if (size > 0) {
		std::vector<char> path(size + 1, '\0');
		if (_NSGetExecutablePath(&path[0], &size) == 0) {
			char resolved[4096];
			if (realpath(&path[0], resolved))
				return Glib::path_get_dirname(resolved);
			return Glib::path_get_dirname(&path[0]);
		}
	}
#elif defined(_WIN32)
	char path[MAX_PATH];
	const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (n > 0 && n < MAX_PATH) {
		std::string s(path, n);
		const std::string::size_type slash = s.find_last_of("\\/");
		if (slash != std::string::npos)
			return s.substr(0, slash);
	}
#endif
	return Glib::get_current_dir();
}

#ifndef SPECTRUM_ANALYZER_DATADIR
#define SPECTRUM_ANALYZER_DATADIR "/usr/local/share/spectrum-analyzer"
#endif

static std::string resource_path(const char* filename)
{
	// 1) Next to the executable: the portable layout (AppImage, .app bundle,
	//    a plain build tree) needs no installation at all.
	const std::string local = Glib::build_filename(executable_directory(), filename);
	if (Glib::file_test(local, Glib::FILE_TEST_EXISTS))
		return local;

	// 2) The installed data directory, for distro packages that put the binary
	//    in /usr/bin and the assets in /usr/share/spectrum-analyzer.
	const std::string installed = Glib::build_filename(SPECTRUM_ANALYZER_DATADIR, filename);
	if (Glib::file_test(installed, Glib::FILE_TEST_EXISTS))
		return installed;

	return local;
}

static bool on_tooltip_button_press_event(GdkEventButton*)
{
	if (!g_spectrum_analyzer_window)
		return false;

	if (g_spectrum_analyzer_window->is_visible())
		g_spectrum_analyzer_window->hide();
	else
		g_spectrum_analyzer_window->present();
	return true;
}

// Try to (re)open the capture device.  Returns true if a stream is running.
static bool open_audio_device()
{
	if (!g_audio_sampler)
		return false;

	if (g_audio_sampler->IsOpen())
		return true;

	if (g_audio_sampler->Open(g_device))
		return true;

	fprintf(stderr, "ERROR: could not open audio capture device '%s'.\n",
		g_device.empty() ? "default" : g_device.c_str());
	fprintf(stderr, "       pass a capture device name as an argument to override;"
		" retrying every %d ms.\n", (int)AUDIO_RETRY_INTERVAL_MS);
	return false;
}

// The device may simply not exist yet (PipeWire starting up after login, USB
// interface plugged in later).  Retry instead of aborting the whole program.
static bool on_audio_retry_timer()
{
	if (g_time_to_quit)
		return false;

	if (open_audio_device()) {
		fprintf(stderr, "Audio backend: %s\n", g_audio_sampler->GetBackendName());
		return false;	// stop the timer
	}
	return true;		// keep trying
}

// The 60 fps timer drives the UI.  Audio capture runs on its own thread inside
// AudioSampler, so the main loop is never blocked by a read.  OSC output keeps
// running even when the window is hidden or iconified; only the redraw is
// skipped, so a minimised analyzer costs no GPU time.
static bool on_redraw_timer()
{
	if(g_time_to_quit) {
		g_app->quit();
		return false;
	}

	if (!g_spectrum_analyzer_window)
		return true;

	// Reopen the capture stream if the sound server or the device went away.
	if (g_audio_sampler)
		g_audio_sampler->Poll();

	g_spectrum_analyzer_window->update();

	if (g_spectrum_analyzer_window->should_render())
		g_spectrum_analyzer_window->trigger_redraw();

	return true;
}

static void on_startup()
{
	//
	// Dark theme (GTK3 CSS, replaces the old GTK2 rc file).
	//
	Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create();
	const std::string css_path = resource_path(CSS_FILE_PATH);
	if(css->load_from_path(css_path)) {
		Gtk::StyleContext::add_provider_for_screen(
			Gdk::Screen::get_default(),
			css,
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
		);
	}
	else {
		g_warning("Could not load CSS file: %s", css_path.c_str());
	}

	// Window icon.
	Gtk::Window::set_default_icon_from_file(resource_path(SVG_ICON_FILE_PATH));

	//
	// Message Bus
	//
	g_message_bus = new MessageBus();

	//
	// Audio Sampler (the real open happens below, once the window exists, so a
	// missing device cannot prevent the UI from appearing)
	//
	g_audio_sampler = new AudioSampler();

	//
	// Main Window
	//
	g_spectrum_analyzer_window = new SpectrumAnalyzerWindow();
	g_spectrum_analyzer_window->set_audio_sampler(g_audio_sampler);
	g_app->add_window(*g_spectrum_analyzer_window);
	g_spectrum_analyzer_window->show_all();

	if (!open_audio_device()) {
		Glib::signal_timeout().connect(sigc::ptr_fun(&on_audio_retry_timer), AUDIO_RETRY_INTERVAL_MS);
	}

	//
	// Status Icon
	//
#if !defined(__APPLE__) && !defined(_WIN32)
	g_status_icon = Gtk::StatusIcon::create_from_file(resource_path(PNG_ICON_FILE_PATH));
	if(g_status_icon) {
		g_status_icon->set_tooltip_text(APPLICATION_NAME);
		g_status_icon->set_visible();
		g_status_icon->signal_button_press_event().connect(sigc::ptr_fun(&on_tooltip_button_press_event));
	}
#endif

	//
	// Main Loop: a 60 fps timer drives the redraw (audio runs on its own thread).
	//
	Glib::signal_timeout().connect(sigc::ptr_fun(&on_redraw_timer), (guint)(g_frame_time * 1000.0));
}

int main(int argc, char *argv[])
{
	// Print the version and exit.
	if(argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		printf("%s %s\n", APPLICATION_NAME, SPECTRUM_ANALYZER_VERSION);
		return 0;
	}

	// Testing helper: print the available capture sources and exit.
	if(argc > 1 && strcmp(argv[1], "--list-sources") == 0) {
		std::vector<AudioSourceInfo> sources = AudioSampler::EnumerateSources();
		for(size_t i = 0; i < sources.size(); i++) {
			printf("%-55s %s%s\n",
				sources[i].id.empty() ? "(default)" : sources[i].id.c_str(),
				sources[i].description.c_str(),
				sources[i].is_monitor ? " [monitor]" : "");
		}
		return 0;
	}

	if(argc > 1) {
		g_device = argv[1];

		// Consume the source argument so GApplication does not interpret it as
		// a file to open (which would emit a GIO critical warning).
		for(int i = 1; i < argc - 1; i++)
			argv[i] = argv[i + 1];
		argc--;
	}

	// NON_UNIQUE lets several analyzer instances run at the same time (as the
	// original Gtk::Main did), each with its own source and OSC number.
	g_app = Gtk::Application::create(argc, argv, APPLICATION_ID, Gio::APPLICATION_NON_UNIQUE);
	g_app->signal_startup().connect(sigc::ptr_fun(&on_startup));

	int result = g_app->run();

	if(g_spectrum_analyzer_window) {
		g_app->remove_window(*g_spectrum_analyzer_window);
		delete g_spectrum_analyzer_window;
		g_spectrum_analyzer_window = NULL;
	}
	delete g_audio_sampler;
	g_audio_sampler = NULL;
	delete g_message_bus;
	g_message_bus = NULL;

	return result;
}
