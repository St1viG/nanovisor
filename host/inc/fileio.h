#ifndef FILEIO_H
#define FILEIO_H

#include <stdint.h>
#include <stddef.h>

struct vm;

#define MAX_OPEN_FILES 16
#define GUEST_NAME_MAX 64
#define HOST_PATH_MAX  256

/*
	One entry of a VM's file table. The table is a struct vm member, so a guest
	simply has no way to name another guest's descriptor - isolation is
	structural rather than a check that could be forgotten.

	off is ours, not the host fd's: every access goes through pread/pwrite
	(design D3). That is what makes copy-on-write's hardest case - preserving
	the seek position while the underlying host fd is swapped from the shared
	original to the VM-local copy - a non-problem.
*/
struct guest_file {
	int  used;
	char name[GUEST_NAME_MAX];       /* the name the guest opened */
	char host_path[HOST_PATH_MAX];   /* where it actually lives */
	int  hostfd;
	long off;
	int  flags;                      /* spec flags, as passed to open() */
	int  shared;                     /* came from the -f registry */
	int  cow;                        /* a VM-local copy has been materialized */
};

/* Registry of files shared between VMs, from -f/--file. Read-only after setup. */
void fileio_set_shared(char **paths, int n);

/* Spec: a name is letters, digits and dots, and must start with a letter. */
int is_valid_name(const char *name);

/* Creates vm_<id>/ ; called once per VM at startup. */
int  fileio_vm_init(struct vm *v);
void fileio_vm_destroy(struct vm *v);

/* Decodes one struct hv_request at req_gpa and stores its result. */
int hv_file_request(struct vm *v, uint32_t req_gpa);

#endif /* FILEIO_H */
