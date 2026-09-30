#include "spectrum-analyzer.h"
#include "utils.h"
#include "filter.h"
#include "audio-sampler.h"
#include "message-bus.h"
#include "spectrum-analyzer-window.h"

#include <gtkmm.h>
#include <glibmm/main.h>
#include <glibmm/timer.h>
#include <glibmm/miscutils.h>

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace std;

#define APPLICATION_NAME ("Spectrum Analyzer")
#define APPLICATION_VERSION ("1.0.0")
#define APPLICATION_ID ("org.spectrum-analyzer.SpectrumAnalyzer")

#define CSS_FILE_PATH ("spectrum-analyzer.css")
#define PNG_ICON_FILE_PATH ("spectrum-analyzer-status-icon.png")
#define SVG_ICON_FILE_PATH ("spectrum-analyzer-icon.svg")

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

//
// Global helper functions
//
void send_float_packet(const char* address, float value)
{
	g_message_bus->send_float(address, value);
}

void send_int_packet(const char* address, int value)
{
	g_message_bus->send_int(address, value);
}

//
// Resolve an asset path relative to the directory that contains the
// executable, so the program works regardless of the current directory.
//
static std::string resource_path(const char* filename)
{
	char buf[4096];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		return Glib::path_get_dirname(buf) + "/" + filename;
	}
	return filename;
}

//
// Callbacks
//
bool on_tooltip_button_press_event(GdkEventButton* event)
{
	if(g_spectrum_analyzer_window->is_visible()) {
		g_spectrum_analyzer_window->hide();
	}
	else {
		g_spectrum_analyzer_window->present();
	}
	return true;
}

// The 60 fps timer only drives the UI.  Audio capture runs on its own thread
// inside AudioSampler, so the main loop is never blocked by a read.
static bool on_redraw_timer()
{
	if(g_time_to_quit) {
		g_app->quit();
		return false;
	}

	g_spectrum_analyzer_window->update();
	g_spectrum_analyzer_window->trigger_redraw();
	return true;
}

static void on_startup()
{
	//
	// Dark theme (GTK3 CSS, replaces the old GTK2 rc file).
	//
	Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create();
	if(css->load_from_path(resource_path(CSS_FILE_PATH))) {
		Gtk::StyleContext::add_provider_for_screen(
			Gdk::Screen::get_default(),
			css,
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
		);
	}
	else {
		g_warning("Could not load CSS file: %s", resource_path(CSS_FILE_PATH).c_str());
	}

	// Window icon.
	Gtk::Window::set_default_icon_from_file(resource_path(SVG_ICON_FILE_PATH));

	//
	// Message Bus
	//
	g_message_bus = new MessageBus();

	//
	// Audio Sampler
	//
	g_audio_sampler = new AudioSampler();
	const char* device = g_device.empty() ? NULL : g_device.c_str();
	if(!g_audio_sampler->Open(device)) {
		fprintf(stderr, "ERROR: could not open audio capture device '%s'.\n", device ? device : "default");
		fprintf(stderr, "       pass an ALSA capture device as an argument to override.\n");
		exit(1);
	}

	//
	// Main Window
	//
	g_spectrum_analyzer_window = new SpectrumAnalyzerWindow();
	g_spectrum_analyzer_window->set_audio_sampler(g_audio_sampler);
	g_app->add_window(*g_spectrum_analyzer_window);
	g_spectrum_analyzer_window->show_all();

	//
	// Status Icon
	//
	Glib::RefPtr<Gtk::StatusIcon> icon = Gtk::StatusIcon::create_from_file(resource_path(PNG_ICON_FILE_PATH));
	if(icon) {
		icon->set_tooltip_text(APPLICATION_NAME);
		icon->set_visible();
		icon->signal_button_press_event().connect(sigc::ptr_fun(&on_tooltip_button_press_event));
	}

	//
	// Main Loop: a 60 fps timer drives the redraw (audio runs on its own thread).
	//
	Glib::signal_timeout().connect(sigc::ptr_fun(&on_redraw_timer), (guint)(g_frame_time * 1000.0));
}

int main(int argc, char *argv[])
{
	// Print the version and exit.
	if(argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		printf("%s %s\n", APPLICATION_NAME, APPLICATION_VERSION);
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
	}
	delete g_audio_sampler;
	delete g_message_bus;

	return result;
}
