#ifndef HV_ABI_H
#define HV_ABI_H

#include <stdint.h>

/*
	Phase A: the serial console and the vector of the injected interrupt. Both
	sides used to hardcode these separately; a mismatch on the vector means the
	guest's handler silently never runs, so they live here with the rest.
*/
#define PORT_SERIAL 0xE9
#define IRQ_VECTOR  32

/*
	The guest/hypervisor file ABI (design D2), included verbatim by both sides.

	One request crosses the boundary as two vmexits on port 0x0278:

	    guest:  outl(PORT_FILE, (uint32_t)&req);   // hand over the address
	    guest:  ret = inl(PORT_FILE);              // collect the result
	    host:   OUT -> read the u32 -> that is a GPA, because GVA == GPA (D1)
	                -> bounds check -> (struct hv_request *)(v->mem + gpa)
	                -> dispatch -> store req->ret and stash it for the IN

	Pointer arguments (path, buf) are just more guest addresses inside the
	struct and are translated the same way, so nothing has to be serialized.
	They always fit in 32 bits because mem_size never exceeds 8 MB.

	Names are HV_-prefixed because the spec's own values collide with the
	host's <fcntl.h> and <stdio.h>: the spec's O_RDWR is 4 while POSIX O_RDWR
	is 2, and the spec's SEEK_SET is 1 while POSIX SEEK_SET is 0. The guest
	sees the unprefixed spec names via guest/inc/syscall.h; the host must
	translate and must never pass these through to open(2) or lseek(2).
*/

#define PORT_FILE 0x0278

/*
	Phase C: the shared buffer (spec part C).

	  PORT_BUF (0x510)  mode byte on the first interrupt, then the round's
	                    length (32-bit) followed by the bytes themselves
	  PORT_ACK (0x520)  a reader reports how many bytes it read; the writer
	                    reads back how many the hypervisor accepted

	BUFFER_SIZE lives here rather than in a host header because the guest has
	to size its own staging buffer to match: a reader must be able to consume
	an entire round, since failing to read every byte stops the VM.

	Deliberately small so that a short input still exercises the multi-round
	path and the barrier. A large value hides exactly the bugs worth finding.
*/
#define PORT_BUF 0x510
#define PORT_ACK 0x520

#define BUFFER_SIZE 64

#define HV_MODE_READ  0
#define HV_MODE_WRITE 1

enum hv_op {
	HV_OPEN  = 1,
	HV_CLOSE = 2,
	HV_READ  = 3,
	HV_WRITE = 4,
	HV_LSEEK = 5,
};

/* open() flags, spec part B. */
#define HV_O_RD     1
#define HV_O_WR     2
#define HV_O_RDWR   4
#define HV_O_CREATE 8

/* lseek() indicators, spec part B. */
#define HV_SEEK_SET 1
#define HV_SEEK_END 2

/*
	arg0/arg1/arg2 by operation:

	  HV_OPEN   path (guest addr), flags,            -
	  HV_CLOSE  fd,                -                 -
	  HV_READ   fd,                buf (guest addr), count
	  HV_WRITE  fd,                buf (guest addr), count
	  HV_LSEEK  fd,                offset,           off_flag
*/
struct hv_request {
	uint32_t op;
	uint32_t arg0;
	uint32_t arg1;
	uint32_t arg2;
	int32_t  ret;
};

/* A one-sided edit to the layout must fail the build, not corrupt a request. */
_Static_assert(sizeof(struct hv_request) == 20, "hv_request layout differs between guest and host");
_Static_assert(sizeof(struct hv_request) == 5 * 4, "hv_request must be five 32-bit fields");

#endif /* HV_ABI_H */
