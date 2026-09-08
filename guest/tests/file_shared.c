#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "string.h"

/*
	Phase B demo 3: copy-on-write.

	Reads the shared file, then seeks to a fixed offset and writes there. The
	seek before the write is deliberate: it is what proves the offset survives
	the moment the hypervisor swaps the host fd from the shared original to
	this VM's private copy (design D3).
*/

#define PATCH_OFFSET 7

void guest_main(void)
{
	char buf[128];
	int fd, n;

	fd = open("shared.txt", O_RDWR);
	if (fd < 0) {
		print("file_shared: open failed\n");
		return;
	}

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	print("read ");
	print_dec((uint64_t)(n < 0 ? 0 : n));
	print(" bytes: ");
	print(buf);

	if (lseek(fd, PATCH_OFFSET, SEEK_SET) != PATCH_OFFSET) {
		print("file_shared: lseek failed\n");
		close(fd);
		return;
	}

	/* The write lands at PATCH_OFFSET in this VM's own copy, not at 0. */
	if (write(fd, "PATCHED", 7) != 7) {
		print("file_shared: write failed\n");
		close(fd);
		return;
	}

	if (lseek(fd, 0, SEEK_SET) != 0) {
		print("file_shared: rewind failed\n");
		close(fd);
		return;
	}

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	print("after write: ");
	print(buf);

	close(fd);
}
