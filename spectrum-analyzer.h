// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __SPECTRUM_ANALYZER_H__
#define __SPECTRUM_ANALYZER_H__

// Application-wide constants and the two globals the OSC senders need.

#define NUM_VERTICAL_LINES (4)
#define NUM_HORIZONTAL_LINES (8)

#define STANDARD_WIDGET_SPACING (4)

#include "message-bus.h"

extern MessageBus* g_message_bus;
extern bool g_time_to_quit;

void send_float_packet(const char* address, float value);
void send_int_packet(const char* address, int value);

#endif
