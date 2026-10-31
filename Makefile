# Spectrum Analyzer
#
#   make          GTK 3 front end   -> ./spectrum-analyzer
#   make sdl      SDL 3 front end   -> ./spectrum-analyzer-sdl
#   make test     headless test suites (no display, no audio device)
#   make asan     address/UB sanitizer build
#   make appimage Linux AppImage of the SDL front end (needs network once)
#   make install  install under $(PREFIX) (default /usr/local)

APP       := spectrum-analyzer
SDL_BIN   := spectrum-analyzer-sdl

CXX       ?= g++
CXXFLAGS  ?= -O2
CXXFLAGS  += -std=c++11 -Wall -Wextra -MMD -MP -pthread
LDFLAGS   += -pthread
LDLIBS_BASE := -lm -pthread

# Fully static link. Needed on Windows to ship a single .exe instead of an .exe
# plus a folder of DLLs; not usable on Linux (glibc + dlopen).
ifeq ($(STATIC),1)
LDFLAGS     += -static -static-libgcc -static-libstdc++
PKG_STATIC  := --static
endif

# ---------------------------------------------------------------------------
# Dependencies
#
# Grouped by what they are for, so a port can swap one group at a time:
#   UI / windowing : gtkmm-3.0, epoxy  (GTK front end only)
#   DSP            : fftw3            (only spectrum-dsp.cc uses it)
#   OSC            : liblo            (only message-bus.cc uses it)
#   Capture        : miniaudio        (vendored in vendor/, no link dependency:
#                                      it loads PulseAudio/ALSA/WASAPI/
#                                      CoreAudio at runtime with dlopen)
# ---------------------------------------------------------------------------
PKGS_UI  := gtkmm-3.0 epoxy
PKGS_DSP := fftw3
PKGS_OSC := liblo
PKGS     := $(PKGS_UI) $(PKGS_DSP) $(PKGS_OSC)

# Use whatever is installed instead of failing: on Windows and macOS only the
# SDL front end is built and gtkmm does not exist there.
PKGS_FOUND   := $(foreach p,$(PKGS),$(shell pkg-config --exists $(p) 2>/dev/null && echo $(p)))
PKGS_MISSING := $(filter-out $(PKGS_FOUND),$(PKGS))
GTK_FOUND    := $(filter gtkmm-3.0,$(PKGS_FOUND))

CXXFLAGS += $(shell pkg-config --cflags $(PKGS_FOUND)) -Ivendor
LDLIBS   += $(shell pkg-config $(PKG_STATIC) --libs $(PKGS_FOUND))

ifeq ($(shell uname -s),Linux)
LDLIBS_BASE += -ldl
endif

LDLIBS += $(LDLIBS_BASE)

PREFIX  ?= /usr/local
DATADIR := $(PREFIX)/share/spectrum-analyzer
CXXFLAGS += -DSPECTRUM_ANALYZER_DATADIR='"$(DATADIR)"'

# Sanitizer build: `make asan` builds ./spectrum-analyzer-asan with ASan+UBSan.
ifeq ($(SANITIZE),1)
CXXFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
APP      := $(APP)-asan
endif

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
# app-sdl.cc belongs to the SDL front end (`make sdl`) and must not be pulled
# into the GTK build, which does not depend on SDL3 at all.
SOURCES  := $(filter-out app-sdl.cc,$(filter-out tests/%,$(wildcard *.cc)))
OBJECTS  := $(SOURCES:.cc=.o)
# miniaudio's implementation lives in its own translation unit.
AUDIO_OBJ := vendor/miniaudio-impl.o
DEPS      := $(OBJECTS:.o=.d) $(AUDIO_OBJ:.o=.d)

all: $(APP)

$(APP): $(OBJECTS) $(AUDIO_OBJ)
	$(if $(GTK_FOUND),,$(error gtkmm-3.0 not found: install libgtkmm-3.0-dev, or build the portable front end with 'make sdl'))
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(AUDIO_OBJ) $(LDLIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# SDL3 front end: the same core modules plus app-sdl.cc, no GTK anywhere.
# ---------------------------------------------------------------------------
SDL_SRC  := app-sdl.cc filter.cc
SDL_OBJ  := spectrum-dsp.o audio-sampler.o message-bus.o $(AUDIO_OBJ)
SDL_PKGS := sdl3 $(PKGS_DSP) $(PKGS_OSC)

$(SDL_BIN): $(SDL_SRC) $(SDL_OBJ) spectrum-dsp.h audio-sampler.h message-bus.h
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags sdl3) $(LDFLAGS) -o $@ \
		$(SDL_SRC) $(SDL_OBJ) \
		$(shell pkg-config $(PKG_STATIC) --libs $(SDL_PKGS)) $(LDLIBS_BASE)

sdl: $(SDL_BIN)

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------
appimage: $(SDL_BIN)
	./packaging/build-appimage.sh

# ---------------------------------------------------------------------------
# Tests.  The analysis core and the filter boxes are pure: no GUI, no audio
# device, no display, so they run anywhere.
# ---------------------------------------------------------------------------
TEST_BIN        := tests/dsp-test
TEST_SRC        := tests/dsp-test.cc spectrum-dsp.cc
FILTER_TEST_BIN := tests/filter-test

$(TEST_BIN): $(TEST_SRC) spectrum-dsp.h
	$(CXX) -std=c++11 -Wall -Wextra -O2 -I. -o $@ $(TEST_SRC) \
		$(shell pkg-config --cflags fftw3) $(shell pkg-config --libs fftw3) -lm

$(FILTER_TEST_BIN): tests/filter-test.cc filter.cc filter.h math-utils.h
	$(CXX) -std=c++11 -Wall -Wextra -O2 -I. -o $@ tests/filter-test.cc filter.cc -lm

test: $(TEST_BIN) $(FILTER_TEST_BIN)
	./$(TEST_BIN)
	./$(FILTER_TEST_BIN)

# ---------------------------------------------------------------------------
# Sanitizer build: same sources, one extra binary, catches lifetime and
# out-of-bounds mistakes that a short manual run would not.
# ---------------------------------------------------------------------------
asan:
	$(MAKE) clean
	$(MAKE) SANITIZE=1

install: $(APP)
	install -Dm755 $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 spectrum-analyzer.css $(DESTDIR)$(DATADIR)/spectrum-analyzer.css
	install -Dm644 spectrum-analyzer-icon.svg $(DESTDIR)$(DATADIR)/spectrum-analyzer-icon.svg
	install -Dm644 spectrum-analyzer-status-icon.png $(DESTDIR)$(DATADIR)/spectrum-analyzer-status-icon.png
	install -Dm644 packaging/spectrum-analyzer.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/spectrum-analyzer.desktop
	install -Dm644 spectrum-analyzer-icon.svg \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/spectrum-analyzer.svg

clean:
	rm -f $(APP) $(APP)-asan $(SDL_BIN) $(OBJECTS) $(DEPS) $(TEST_BIN) $(FILTER_TEST_BIN)

.PHONY: all sdl appimage test asan install clean

-include $(DEPS)
