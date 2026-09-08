#include "guest.h"
#include "print.h"
#include "irqproto.h"

/*
	Phase C: deliberately sends more than BUFFER_SIZE in one round, to show the
	hypervisor discarding the excess and reporting what it accepted.
*/

#define FLOOD (BUFFER_SIZE * 2)

static char chunk[FLOOD];
static int  sent;

void guest_writer_round(void)
{
	uint32_t accepted;
	uint32_t i;

	if (sent) {
		hv_buf_send(chunk, 0);   /* sentinel: end the session */
		return;
	}

	for (i = 0; i < FLOOD; i++)
		chunk[i] = (char)('a' + (i % 26));

	accepted = hv_buf_send(chunk, FLOOD);
	sent = 1;

	print("writer: sent ");
	print_dec(FLOOD);
	print(" bytes, hypervisor accepted ");
	print_dec((uint64_t)accepted);
	print(" (BUFFER_SIZE is ");
	print_dec(BUFFER_SIZE);
	print(")\n");
}

void guest_reader_round(void)
{
	char buf[BUFFER_SIZE];
	uint32_t got = hv_buf_recv(buf, BUFFER_SIZE);

	print("reader: received ");
	print_dec((uint64_t)got);
	print(" bytes\n");
}

void guest_main(void)
{
}
