#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "irqproto.h"

/*
	Phase C reader: consumes each round from the shared buffer and appends it
	to a local file, which the hypervisor keeps in this VM's own vm_<id>/.
*/

static char chunk[BUFFER_SIZE];
static int  dst_fd = -1;
static int  finished;

void guest_reader_round(void)
{
	uint32_t got;

	if (finished)
		return;

	got = hv_buf_recv(chunk, BUFFER_SIZE);

	if (got == 0) {
		finished = 1;
		if (dst_fd >= 0)
			close(dst_fd);
		print("reader: end of stream\n");
		return;
	}

	if (dst_fd >= 0)
		write(dst_fd, chunk, (int)got);
}

void guest_main(void)
{
	dst_fd = open("out.txt", O_RDWR | O_CREATE);

	if (dst_fd < 0)
		print("reader: cannot open out.txt\n");
	else
		print("reader: writing to out.txt\n");
}
