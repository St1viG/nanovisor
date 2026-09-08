#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "irqproto.h"

/*
	Phase C writer: streams a file through the hypervisor's shared buffer,
	BUFFER_SIZE bytes per round.

	guest_main only opens the source; the transfer happens inside the
	vector-32 handler, one round per interrupt, with the hypervisor deciding
	when the next round may start.
*/

static char chunk[BUFFER_SIZE];
static int  src_fd = -1;
static int  finished;

void guest_writer_round(void)
{
	int n;

	if (finished)
		return;

	n = (src_fd >= 0) ? read(src_fd, chunk, BUFFER_SIZE) : 0;
	if (n < 0)
		n = 0;

	hv_buf_send(chunk, (uint32_t)n);

	/* Design D6: a zero-length round is the end-of-stream sentinel. */
	if (n == 0) {
		finished = 1;
		if (src_fd >= 0)
			close(src_fd);
		print("writer: end of stream\n");
	}
}

void guest_main(void)
{
	src_fd = open("input.txt", O_RD);

	if (src_fd < 0)
		print("writer: cannot open input.txt, sending an empty stream\n");
	else
		print("writer: streaming input.txt\n");
}
