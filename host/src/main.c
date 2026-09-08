#include "opts.h"
#include "vm.h"

#include <stdio.h>

/* Brings up one guest and runs it to completion. A.7 moves this onto a thread. */
static int run_guest(const struct vm_config *cfg)
{
	struct vm v;
	int rc;

	if (vm_init(&v, cfg)) {
		fprintf(stderr, "[vm %d] failed to init the VM\n", cfg->id);
		vm_destroy(&v);
		return -1;
	}

	rc = vm_setup(&v, cfg);
	if (rc == 0)
		rc = vm_run(&v);

	vm_destroy(&v);

	return rc;
}

int main(int argc, char *argv[])
{
	struct hv_options opts;
	int failures = 0;
	int rc, i;

	rc = parse_options(argc, argv, &opts);
	if (rc != 0) {
		free_options(&opts);
		return rc > 0 ? 0 : 1;   /* --help is not an error */
	}

	for (i = 0; i < opts.n_guests; i++) {
		struct vm_config cfg = {
			.mem_size  = (size_t)opts.mem_mb * 1024u * 1024u,
			.page_size = opts.page_kb,
			.image     = opts.guests[i],
			.id        = i,
		};

		if (run_guest(&cfg) != 0)
			failures++;
	}

	free_options(&opts);

	return failures ? 1 : 0;
}
