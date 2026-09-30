CXX ?= g++
PKGS = gtkmm-3.0 fftw3 liblo alsa epoxy
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++11 -Wall -MMD -MP $(shell pkg-config --cflags $(PKGS))
LDLIBS = $(shell pkg-config --libs $(PKGS)) -lm

# PulseAudio backend (works on both PulseAudio and PipeWire via pipewire-pulse).
# Enabled automatically when the development headers are installed.
HAVE_PULSE := $(shell pkg-config --exists libpulse-simple libpulse && echo yes || echo no)
ifeq ($(HAVE_PULSE),yes)
CXXFLAGS += -DHAVE_PULSE $(shell pkg-config --cflags libpulse-simple libpulse)
LDLIBS += $(shell pkg-config --libs libpulse-simple libpulse)
endif

SOURCES := $(wildcard *.cc)
OBJECTS := $(SOURCES:.cc=.o)

all: spectrum-analyzer

spectrum-analyzer: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Make rebuild a .o whenever one of the headers it includes changes.
-include $(OBJECTS:.o=.d)

clean:
	rm -f spectrum-analyzer $(OBJECTS) $(OBJECTS:.o=.d)

.PHONY: all clean
