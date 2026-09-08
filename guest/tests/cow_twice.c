#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "string.h"

/* Two descriptors on the same shared file, each triggering copy-on-write. */
void guest_main(void)
{
	char buf[64];
	int a = open("shared.txt", O_RDWR);
	int b = open("shared.txt", O_RDWR);

	print("fd a = "); print_dec((uint64_t)a);
	print(", fd b = "); print_dec((uint64_t)b); print("\n");

	lseek(a, 0, SEEK_SET);
	write(a, "AAAA", 4);
	lseek(b, 10, SEEK_SET);
	write(b, "BBBB", 4);

	lseek(a, 0, SEEK_SET);
	memset(buf, 0, sizeof(buf));
	read(a, buf, 32);
	print("result: "); print(buf); print("\n");

	close(a);
	close(b);
}
