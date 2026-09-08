#include "guest.h"
#include "print.h"
#include "irqproto.h"

/*
	A reader that stores only 4 bytes of each round, so it reports fewer than
	the round carried and must be stopped by the hypervisor (spec part C). Kept
	as permanent coverage of that rule.
*/
void guest_reader_round(void)
{
	char small[4];

	print("reader: storing only 4 bytes\n");
	hv_buf_recv(small, 4);
	print("reader: should not get here\n");
}

void guest_main(void) { }
