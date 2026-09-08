#include "opts.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/*
	Per-thread state. The struct vm itself lives in an array owned by main so
	that later phases (the phase C coordinator) can reach a sibling VM, but it
	is only ever mutated by its own thread.
*/
struct vm_thread_arg {
	struct vm       *vm;
	struct vm_config cfg;
	int              rc;
};

/*
	Design D7: KVM expects vcpu ioctls from the thread that created the vCPU,
	so the whole lifecycle - including vm_init, which issues KVM_CREATE_VCPU -
	runs here rather than on the main thread.
*/
static void *vm_thread(void *arg)
{
	struct vm_thread_arg *a = arg;

	if (vm_init(a->vm, &a->cfg)) {
		fprintf(stderr, "[vm %d] failed to init the VM\n", a->cfg.id);
		vm_destroy(a->vm);
		a->rc = -1;
		return NULL;
	}

	a->rc = vm_setup(a->vm, &a->cfg);
	if (a->rc == 0)
		a->rc = vm_run(a->vm);

	vm_destroy(a->vm);

	return NULL;
}

int main(int argc, char *argv[])
{
	struct hv_options opts;
	struct vm_thread_arg *args = NULL;
	struct vm *vms = NULL;
	pthread_t *tids = NULL;
	int spawned = 0;
	int failures = 0;
	int rc, i;

	rc = parse_options(argc, argv, &opts);
	if (rc != 0) {
		free_options(&opts);
		return rc > 0 ? 0 : 1;   /* --help is not an error */
	}

	vms  = calloc((size_t)opts.n_guests, sizeof(*vms));
	args = calloc((size_t)opts.n_guests, sizeof(*args));
	tids = calloc((size_t)opts.n_guests, sizeof(*tids));
	if (!vms || !args || !tids) {
		perror("calloc");
		free(vms);
		free(args);
		free(tids);
		free_options(&opts);
		return 1;
	}

	for (i = 0; i < opts.n_guests; i++) {
		args[i].vm  = &vms[i];
		args[i].rc  = -1;
		args[i].cfg = (struct vm_config){
			.mem_size  = (size_t)opts.mem_mb * 1024u * 1024u,
			.page_size = opts.page_kb,
			.image     = opts.guests[i],
			.id        = i,
		};

		if (pthread_create(&tids[i], NULL, vm_thread, &args[i]) != 0) {
			fprintf(stderr, "[vm %d] pthread_create failed\n", i);
			failures++;
			break;
		}
		spawned++;
	}

	for (i = 0; i < spawned; i++) {
		pthread_join(tids[i], NULL);
		if (args[i].rc != 0)
			failures++;
	}

	free(vms);
	free(args);
	free(tids);
	free_options(&opts);

	return failures ? 1 : 0;
}
