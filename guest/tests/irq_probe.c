#include "guest.h"
#include "print.h"
#include "irqproto.h"

/*
	Phase C demo 1: report the mode the hypervisor assigned, nothing else.

	Both round functions are defined so the image opts into the protocol, and
	the writer ends the session immediately with the zero-length sentinel -
	otherwise a run of probes would never terminate.
*/

static void report(void)
{
	print("mode = ");
	print_dec((uint64_t)guest_mode);
	print(guest_mode == HV_MODE_WRITE ? " (writer)\n" : " (reader)\n");
}

void guest_writer_round(void)
{
	report();
	hv_buf_send(0, 0);
}

void guest_reader_round(void)
{
	char dummy;

	hv_buf_recv(&dummy, 0);
	report();
}

void guest_main(void)
{
	/* All the work happens in the interrupt handler. */
}
