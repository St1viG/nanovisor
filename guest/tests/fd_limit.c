#include "guest.h"
#include "print.h"
#include "syscall.h"

/* Opens more files than the per-VM table holds; the overflow must fail
   cleanly and the earlier descriptors must keep working. */
void guest_main(void)
{
	char name[8] = "fx.txt";
	int fds[24];
	int i, opened = 0, failed = 0;

	for (i = 0; i < 24; i++) {
		name[1] = (char)('a' + i);
		fds[i] = open(name, O_RDWR | O_CREATE);
		if (fds[i] < 0)
			failed++;
		else
			opened++;
	}

	print("opened "); print_dec((uint64_t)opened);
	print(", refused "); print_dec((uint64_t)failed); print("\n");

	print("write to the first fd still works: ");
	print_dec((uint64_t)write(fds[0], "ok", 2)); print("\n");

	for (i = 0; i < 24; i++)
		if (fds[i] >= 0)
			close(fds[i]);

	name[1] = 'z';
	print("after closing, a new open succeeds: ");
	print_dec((uint64_t)open(name, O_RDWR | O_CREATE)); print("\n");
}
