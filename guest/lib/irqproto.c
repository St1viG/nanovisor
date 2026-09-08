#include "irqproto.h"
#include "io.h"

volatile int guest_mode = -1;

uint32_t hv_buf_send(const char *src, uint32_t count)
{
	uint32_t i;

	outl(PORT_BUF, count);

	for (i = 0; i < count; i++)
		outb(PORT_BUF, (uint8_t)src[i]);

	return inl(PORT_ACK);
}

uint32_t hv_buf_recv(char *dst, uint32_t max)
{
	uint32_t count = inl(PORT_BUF);
	uint32_t stored = 0;
	uint32_t i;

	/*
		Every byte of the round is taken off the port even when there is
		nowhere to put it: stopping early would leave the guest and the
		hypervisor disagreeing about how much of the round is left.
	*/
	for (i = 0; i < count; i++) {
		uint8_t byte = inb(PORT_BUF);

		if (i < max) {
			dst[i] = (char)byte;
			stored++;
		}
	}

	outl(PORT_ACK, stored);

	return stored;
}
