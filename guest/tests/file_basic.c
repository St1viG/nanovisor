#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "string.h"

/* Phase B demo 1: open, write, seek back, read, close. */

static const char payload[] = "nanovisor phase B round trip\n";

static void report(const char *what, int rc)
{
	print(what);
	print(" -> ");
	if (rc < 0)
		print("-1\n");
	else {
		print_dec((uint64_t)rc);
		print("\n");
	}
}

void guest_main(void)
{
	char buf[64];
	int len = (int)strlen(payload);
	int fd, n;

	fd = open("out.txt", O_RDWR | O_CREATE);
	report("open(out.txt, O_RDWR|O_CREATE)", fd);
	if (fd < 0)
		return;

	report("write(payload)", write(fd, payload, len));
	report("lseek(0, SEEK_SET)", lseek(fd, 0, SEEK_SET));

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, len);
	report("read(back)", n);

	print("content: ");
	print(buf);

	report("lseek(0, SEEK_END)", lseek(fd, 0, SEEK_END));
	report("close", close(fd));
}
