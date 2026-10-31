APP       := spectrum-analyzer

CXX       ?= g++
CXXFLAGS  ?= -O2
CXXFLAGS  += -std=c++11 -Wall -Wextra -MMD -MP -pthread
LDLIBS    += -lm -pthread

# ---------------------------------------------------------------------------
# Dependencies
#
# Grouped by what they are for, so a port can swap one group at a time:
#   UI / windowing : gtkmm-3.0, epoxy  (GLArea + the glc shim)
#   DSP            : fftw3            (only spectrum-dsp.cc uses it)
#   OSC            : liblo            (only message-bus.cc uses it)
#   Capture        : alsa, libpulse   (only audio-sampler.cc uses them)
# ---------------------------------------------------------------------------
PKGS_UI      := gtkmm-3.0 epoxy
PKGS_DSP     := fftw3
PKGS_OSC     := liblo
PKGS         := $(PKGS_UI) $(PKGS_DSP) $(PKGS_OSC)

ifeq ($(shell uname -s),Linux)
PKGS_CAPTURE := alsa
else
PKGS_CAPTURE :=
endif

CXXFLAGS += $(shell pkg-config --cflags $(PKGS) $(PKGS_CAPTURE))
LDLIBS   += $(shell pkg-config --libs $(PKGS) $(PKGS_CAPTURE))

# PulseAudio backend (works on both PulseAudio and PipeWire via pipewire-pulse).
# Enabled automatically when the development headers are installed.
HAVE_PULSE := $(shell pkg-config --exists libpulse-simple libpulse && echo yes || echo no)
ifeq ($(HAVE_PULSE),yes)
CXXFLAGS += -DHAVE_PULSE $(shell pkg-config --cflags libpulse-simple libpulse)
LDLIBS   += $(shell pkg-config --libs libpulse-simple libpulse)
endif

# Sanitizer build: `make asan` builds ./spectrum-analyzer-asan with ASan+UBSan.
ifeq ($(SANITIZE),1)
CXXFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
APP      := $(APP)-asan
endif

PREFIX  ?= /usr/local
DATADIR := $(PREFIX)/share/spectrum-analyzer
CXXFLAGS += -DSPECTRUM_ANALYZER_DATADIR='"$(DATADIR)"'

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
SOURCES := $(filter-out tests/%,$(wildcard *.cc))
OBJECTS := $(SOURCES:.cc=.o)
DEPS    := $(OBJECTS:.o=.d)

all: $(APP)

$(APP): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# The DSP core has no GUI, no audio and no OSC dependency: it is the only part
# of the program that can be exercised headlessly, so it gets a real test suite.
TEST_BIN := tests/dsp-test
TEST_SRC := tests/dsp-test.cc spectrum-dsp.cc

$(TEST_BIN): $(TEST_SRC) spectrum-dsp.h math-utils.h
	$(CXX) -std=c++11 -Wall -Wextra -O2 -I. -o $@ $(TEST_SRC) \
		$(shell pkg-config --cflags fftw3) $(shell pkg-config --libs fftw3) -lm

test: $(TEST_BIN)
	./$(TEST_BIN)

# Address/UB sanitizer build: same sources, one binary, catches the lifetime and
# out-of-bounds mistakes that a short manual run would not.
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
	rm -f $(APP) $(APP)-asan $(OBJECTS) $(DEPS) $(TEST_BIN)

.PHONY: all test asan install clean

-include $(DEPS)
