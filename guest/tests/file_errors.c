#include "guest.h"
#include "print.h"
#include "syscall.h"

/* Phase B demo 2: every way a call is supposed to fail. */

static void expect(const char *what, int got, int want)
{
	print(what);
	print(" -> ");
	if (got < 0) {
		print("-1");
	} else {
		print_dec((uint64_t)got);
	}
	print(got == want ? "  [ok]\n" : "  [UNEXPECTED]\n");
}

void guest_main(void)
{
	char buf[16];
	int fd;

	/* Name rules: must start with a letter, then letters, digits and dots. */
	expect("open(1bad.txt)      name starts with a digit", open("1bad.txt", O_RDWR | O_CREATE), -1);
	expect("open(a/b.txt)       name contains a slash   ", open("a/b.txt", O_RDWR | O_CREATE), -1);
	expect("open(..)            traversal attempt       ", open("..", O_RD), -1);
	expect("open(bad_name.txt)  underscore not allowed  ", open("bad_name.txt", O_RDWR | O_CREATE), -1);

	/* Missing file without O_CREATE. */
	expect("open(nope.txt, O_RD)   no such file         ", open("nope.txt", O_RD), -1);

	/* O_CREATE with no access flag is refused by decision, see fileio.c. */
	expect("open(x.txt, O_CREATE)  no access flag       ", open("x.txt", O_CREATE), -1);

	/* Descriptors the guest never owned, and ones it has closed. */
	expect("read(fd=9)          never opened            ", read(9, buf, sizeof(buf)), -1);
	expect("close(fd=-1)        invalid descriptor      ", close(-1), -1);

	fd = open("tmp.txt", O_RDWR | O_CREATE);
	expect("open(tmp.txt)       valid                   ", fd, 0);
	expect("close(tmp.txt)      valid                   ", close(fd), 0);
	expect("read(closed fd)     use after close         ", read(fd, buf, sizeof(buf)), -1);

	/* Permission is checked against the flags the file was opened with. */
	fd = open("tmp.txt", O_RD);
	expect("open(tmp.txt, O_RD) reopen read-only        ", fd, 0);
	expect("write(read-only fd) not permitted           ", write(fd, "x", 1), -1);
	expect("lseek(fd, 0, 3)     invalid indicator       ", lseek(fd, 0, 3), -1);
	expect("lseek(fd, -5, SEEK_SET) negative offset     ", lseek(fd, -5, SEEK_SET), -1);
	expect("close(tmp.txt)      valid                   ", close(fd), 0);
}
