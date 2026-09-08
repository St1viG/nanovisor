#ifndef IRQPROTO_H
#define IRQPROTO_H

#include <stdint.h>

#include "hv_abi.h"

/*
	Phase C guest protocol.

	An image opts in simply by defining guest_writer_round() and/or
	guest_reader_round(). The vector-32 handler declares them weak, so an image
	that defines neither leaves both resolving to NULL and keeps the plain
	phase A interrupt behaviour - which is why hello.img still prints what it
	always did.
*/
void guest_writer_round(void);
void guest_reader_round(void);

/* The assigned mode, valid after the first interrupt. -1 until then. */
extern volatile int guest_mode;

/*
	Send one round: the count, then the bytes, then collect how many the
	hypervisor accepted (it discards anything past BUFFER_SIZE).
	A count of 0 is the end-of-stream sentinel - see design D6.
*/
uint32_t hv_buf_send(const char *src, uint32_t count);

/*
	Receive one round: the count, then that many bytes, storing at most max of
	them, then report how many were stored. Reporting fewer than the round
	carried stops this VM, which is what the spec asks for.
*/
uint32_t hv_buf_recv(char *dst, uint32_t max);

#endif /* IRQPROTO_H */
