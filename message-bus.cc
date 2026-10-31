// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lupin3rd
#include "message-bus.h"

#include <cstdio>
#include <cstring>

namespace {

// Log at most this many liblo failures, then stay quiet: an unreachable
// destination would otherwise print 240 lines per second.
const unsigned long kMaxErrorLogs = 3;

} // anonymous namespace

MessageBus::MessageBus()
	: m_address(NULL),
		m_host(DEFAULT_OSC_HOST),
		m_port(DEFAULT_OSC_PORT),
		m_errors(0)
{
	// Explicit UDP: never let liblo pick a reliable transport by accident.
	m_address = lo_address_new_with_proto(LO_UDP, m_host.c_str(), m_port.c_str());
}

MessageBus::~MessageBus()
{
	if (m_address) {
		lo_address_free(m_address);
		m_address = NULL;
	}
}

bool MessageBus::valid_host(const char* host)
{
	if (!host || !host[0])
		return false;

	for (const char* p = host; *p; p++) {
		const unsigned char c = (unsigned char)*p;
		if (c <= ' ' || c == '/' || c == '@' || c == ',' || c == '[' || c == ']')
			return false;
	}
	return true;
}

bool MessageBus::valid_port(const char* port)
{
	if (!port || !port[0])
		return false;

	char* end = NULL;
	const long value = strtol(port, &end, 10);
	if (end == port || (end && *end != '\0'))
		return false;
	return value > 0 && value <= 65535;
}

bool MessageBus::set_destination(const char* host, const char* port)
{
	const char* use_host = (host && host[0]) ? host : DEFAULT_OSC_HOST;
	const char* use_port = (port && port[0]) ? port : DEFAULT_OSC_PORT;

	if (!valid_host(use_host) || !valid_port(use_port)) {
		fprintf(stderr, "OSC: ignoring invalid destination '%s:%s'\n",
			use_host ? use_host : "", use_port ? use_port : "");
		return false;
	}

	lo_address fresh = lo_address_new_with_proto(LO_UDP, use_host, use_port);
	if (!fresh) {
		fprintf(stderr, "OSC: could not create address for '%s:%s'\n", use_host, use_port);
		return false;
	}

	// Keep a short TTL: OSC is fire-and-forget, and a stray packet must not
	// linger in a network loop.
	lo_address_set_ttl(fresh, 1);

	if (m_address)
		lo_address_free(m_address);
	m_address = fresh;
	m_host = use_host;
	m_port = use_port;

	// A new destination deserves fresh error reporting.
	m_errors = 0;
	return true;
}

void MessageBus::send_int(const char* address, int value)
{
	if (!m_address || !address)
		return;

	if (lo_send(m_address, address, "i", value) < 0) {
		m_errors++;
		if (m_errors <= kMaxErrorLogs) {
			fprintf(stderr, "OSC: send to %s:%s failed (%s)\n",
				m_host.c_str(), m_port.c_str(),
				lo_address_errstr(m_address) ? lo_address_errstr(m_address) : "error");
		}
	}
}

void MessageBus::send_float(const char* address, float value)
{
	if (!m_address || !address)
		return;

	if (lo_send(m_address, address, "f", value) < 0) {
		m_errors++;
		if (m_errors <= kMaxErrorLogs) {
			fprintf(stderr, "OSC: send to %s:%s failed (%s)\n",
				m_host.c_str(), m_port.c_str(),
				lo_address_errstr(m_address) ? lo_address_errstr(m_address) : "error");
		}
	}
}
