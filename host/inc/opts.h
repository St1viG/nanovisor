#ifndef OPTS_H
#define OPTS_H

#include <stddef.h>

/*
	Parsed command line.

	  -m, --memory <2|4|8>          guest memory size in MB
	  -p, --page   <4|2>            page size: 4 for 4 KB, 2 for 2 MB
	  -g, --guest  <img> [img...]   one VM is launched per image (required)
	  -f, --file   <f> [f...]       files shared between VMs (phase B)

	guests and files point into argv, so they must not be freed; only the
	arrays themselves are owned. free_options releases them.
*/
struct hv_options {
	int    mem_mb;    /* 2, 4 or 8 */
	int    page_kb;   /* 4 or 2048 */
	char **guests;
	int    n_guests;
	char **files;
	int    n_files;
	int    irq_session;   /* -i: run the phase C shared-buffer session */
	int    writer_id;     /* -w: which VM writes; the rest read */
};

int  parse_options(int argc, char **argv, struct hv_options *o);
void free_options(struct hv_options *o);
void usage(const char *prog);

#endif /* OPTS_H */
