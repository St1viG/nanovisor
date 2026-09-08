#include "vm.h"

#include <stdio.h>

int main(int argc, char *argv[])
{
	struct vm v;
	struct vm_config cfg;

	if (argc != 2) {
		printf("The program requests an image to run: %s <guest-image>\n", argv[0]);
		return 1;
	}

	/* Phase A.1 replaces this with the parsed command line. */
	cfg.mem_size  = MEM_SIZE;
	cfg.page_size = PAGE_SIZE_4K;
	cfg.image     = argv[1];
	cfg.id        = 0;

	if (vm_init(&v, &cfg)) {
		printf("Failed to init the VM\n");
		vm_destroy(&v);
		return 1;
	}

	if (vm_setup(&v, &cfg)) {
		vm_destroy(&v);
		return 1;
	}

	if (vm_run(&v)) {
		vm_destroy(&v);
		return 1;
	}

	vm_destroy(&v);
	return 0;
}
