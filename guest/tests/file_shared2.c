#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "string.h"

/*
	Second copy-on-write participant. Identical to file_shared.c except that it
	patches different text at a different offset, so the two VMs' private
	copies end up differing from each other as well as from the original.
*/

#define PATCH_OFFSET 0

void guest_main(void)
{
	char buf[128];
	int fd, n;

	fd = open("shared.txt", O_RDWR);
	if (fd < 0) {
		print("file_shared2: open failed\n");
		return;
	}

	if (lseek(fd, PATCH_OFFSET, SEEK_SET) != PATCH_OFFSET ||
	    write(fd, "SECOND", 6) != 6 ||
	    lseek(fd, 0, SEEK_SET) != 0) {
		print("file_shared2: failed\n");
		close(fd);
		return;
	}

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	(void)n;
	print("after write: ");
	print(buf);

	close(fd);
}
