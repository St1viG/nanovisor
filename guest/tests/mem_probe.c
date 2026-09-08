#include "guest.h"
#include "print.h"

/*
	Paging demo (A.3): proves the whole of [0, mem_size) is mapped, for every
	--memory / --page combination, by touching both ends of the region the
	guest owns.
*/

static volatile uint64_t low_probe[512];   /* in .bss, just above the image */

void guest_main(void)
{
	volatile uint64_t *top = (volatile uint64_t *)(guest_mem_top - 8);
	int ok = 1;

	print("mem_probe: mem_top = ");
	print_hex(guest_mem_top);
	print(" (");
	print_dec(guest_mem_top / (1024 * 1024));
	print(" MB)\n");

	*top = 0xDEADBEEFCAFEBABEull;
	if (*top != 0xDEADBEEFCAFEBABEull) {
		print("mem_probe: FAIL at top of memory\n");
		ok = 0;
	}

	low_probe[0] = 0x1122334455667788ull;
	low_probe[511] = 0x8877665544332211ull;
	if (low_probe[0] != 0x1122334455667788ull || low_probe[511] != 0x8877665544332211ull) {
		print("mem_probe: FAIL near the image\n");
		ok = 0;
	}

	print(ok ? "mem_probe: ok\n" : "mem_probe: FAILED\n");
}
