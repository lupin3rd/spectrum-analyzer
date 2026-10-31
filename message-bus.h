// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#ifndef __MESSAGE_BUS_H__
#define __MESSAGE_BUS_H__

//
// liblo OpenSoundControl (http://liblo.sourceforge.net/)
//
#include <string>

#include <lo/lo.h>

#define DEFAULT_OSC_HOST ("localhost")
#define DEFAULT_OSC_PORT ("10007")

class MessageBus
{
public:
	MessageBus();
	virtual ~MessageBus();

	// Set the destination (IP/host and UDP port) of the outgoing OSC messages.
	// Returns false if the arguments are not usable; the previous destination
	// is kept in that case.
	//
	// liblo resolves the host lazily, on the first send.  Call this only when
	// the user commits a destination (Enter / focus out) rather than on every
	// keystroke: a hostname lookup blocks the UI thread.
	bool set_destination(const char* host, const char* port);

	const char* destination_host() const { return m_host.c_str(); }
	const char* destination_port() const { return m_port.c_str(); }

	// Number of messages liblo refused to send (bad host, unreachable, ...).
	unsigned long error_count() const { return m_errors; }

	void send_int(const char* address, int value);
	void send_float(const char* address, float value);

	// True when the string looks like something a URL/host parser can accept.
	static bool valid_host(const char* host);
	static bool valid_port(const char* port);

private:
	lo_address m_address;
	std::string m_host;
	std::string m_port;
	unsigned long m_errors;
};

#endif
