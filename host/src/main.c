#include "opts.h"
#include "fileio.h"
#include "shared_buf.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
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

	/*
		Shared files are resolved by basename, so a guest opens "a.txt"
		regardless of the path -f was given. Check them once here: a name the
		spec's rules reject, or a file that is not readable, can never be
		opened by any guest, and saying so now beats debugging a -1 later.
	*/
	for (i = 0; i < opts.n_files; i++) {
		const char *base = strrchr(opts.files[i], '/');
		struct stat st;
		int j;

		base = base ? base + 1 : opts.files[i];

		if (!is_valid_name(base)) {
			fprintf(stderr, "warning: shared file '%s' has a name no guest can open\n",
				opts.files[i]);
		} else if (stat(opts.files[i], &st) != 0) {
			fprintf(stderr, "warning: shared file '%s': %s\n",
				opts.files[i], strerror(errno));
		} else if (!S_ISREG(st.st_mode)) {
			fprintf(stderr, "warning: shared file '%s' is not a regular file\n",
				opts.files[i]);
		} else if (access(opts.files[i], R_OK) != 0) {
			fprintf(stderr, "warning: shared file '%s' is not readable: %s\n",
				opts.files[i], strerror(errno));
		}

		/*
			Guests open shared files by basename, so two -f paths sharing one
			basename means the second is unreachable - silently, which is a
			genuinely confusing thing to debug from inside a guest.
		*/
		for (j = 0; j < i; j++) {
			const char *prev = strrchr(opts.files[j], '/');

			prev = prev ? prev + 1 : opts.files[j];
			if (!strcmp(prev, base)) {
				fprintf(stderr,
					"warning: shared files '%s' and '%s' have the same name; "
					"guests can only reach the first\n",
					opts.files[j], opts.files[i]);
				break;
			}
		}
	}

	fileio_set_shared(opts.files, opts.n_files);

	if (opts.irq_session) {
		if (opts.n_guests < 2)
			fprintf(stderr, "warning: --irq with %d guest(s): no reader to receive rounds\n",
				opts.n_guests);
		shared_buf_init(opts.n_guests - 1, opts.verbose);
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

	/* Configure every VM first, so the roles are known even for any that
	   fail to spawn below. */
	for (i = 0; i < opts.n_guests; i++) {
		args[i].vm  = &vms[i];
		args[i].rc  = -1;
		args[i].cfg = (struct vm_config){
			.mem_size  = (size_t)opts.mem_mb * 1024u * 1024u,
			.page_size = opts.page_kb,
			.image     = opts.guests[i],
			.id        = i,
			.role      = opts.irq_session
					? (i == opts.writer_id ? ROLE_WRITER : ROLE_READER)
					: ROLE_NONE,
			.irq_session = opts.irq_session,
		};
	}

	for (i = 0; i < opts.n_guests; i++) {
		if (pthread_create(&tids[i], NULL, vm_thread, &args[i]) != 0) {
			fprintf(stderr, "[vm %d] pthread_create failed\n", i);
			failures++;
			break;
		}
		spawned++;
	}

	/*
		Anything past `spawned` never ran, but shared_buf counted it at init.
		Retract those obligations before joining, or the writer blocks on
		acknowledgements from threads that do not exist.
	*/
	if (opts.irq_session) {
		for (i = spawned; i < opts.n_guests; i++)
			sb_vm_never_started(args[i].cfg.role);
	}

	for (i = 0; i < spawned; i++) {
		pthread_join(tids[i], NULL);
		if (args[i].rc != 0)
			failures++;
	}

	if (opts.irq_session)
		shared_buf_destroy();

	free(vms);
	free(args);
	free(tids);
	free_options(&opts);

	return failures ? 1 : 0;
}
