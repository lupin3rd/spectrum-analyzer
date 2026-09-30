#include "message-bus.h"

#include <stdio.h>

MessageBus::MessageBus()
	: m_address(NULL)
{
	m_address = lo_address_new(DEFAULT_OSC_HOST, DEFAULT_OSC_PORT);
}

MessageBus::~MessageBus()
{
	if (m_address) {
		lo_address_free(m_address);
		m_address = NULL;
	}
}

void MessageBus::set_destination(const char* host, const char* port)
{
	if (m_address) {
		lo_address_free(m_address);
		m_address = NULL;
	}

	m_address = lo_address_new(
		(host && host[0]) ? host : DEFAULT_OSC_HOST,
		(port && port[0]) ? port : DEFAULT_OSC_PORT);
}

void MessageBus::send_int(const char* address, int value)
{
	if (m_address)
		lo_send(m_address, address, "i", value);
}

void MessageBus::send_float(const char* address, float value)
{
	if (m_address)
		lo_send(m_address, address, "f", value);
}
