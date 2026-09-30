#ifndef __MESSAGE_BUS_H__
#define __MESSAGE_BUS_H__

//
// liblo OpenSoundControl (http://liblo.sourceforge.net/)
//
#include <lo/lo.h>

#define DEFAULT_OSC_HOST ("localhost")
#define DEFAULT_OSC_PORT ("10007")

class MessageBus
{
public:
	MessageBus();
	virtual ~MessageBus();

	// Set the destination (IP/host and UDP port) of the outgoing OSC messages.
	void set_destination(const char* host, const char* port);

	void send_int(const char* address, int value);
	void send_float(const char* address, float value);

private:
	lo_address m_address;
};

#endif
