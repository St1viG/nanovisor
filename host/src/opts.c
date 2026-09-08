#include "opts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>

#define DEFAULT_MEM_MB  2
#define DEFAULT_PAGE_KB 4

void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s -g <image> [image...] [options]\n"
		"\n"
		"  -m, --memory <2|4|8>        guest memory size in MB (default %d)\n"
		"  -p, --page   <4|2>          page size, 4 for 4KB or 2 for 2MB (default %d)\n"
		"  -g, --guest  <img> [img...] guest images; one VM is launched per image\n"
		"  -f, --file   <f> [f...]     files shared between VMs\n"
		"  -i, --irq                   run the shared-buffer interrupt session\n"
		"  -w, --writer <id>           VM that writes to the shared buffer (default 0)\n"
		"  -v, --verbose               trace shared-buffer rounds\n"
		"  -h, --help                  this message\n",
		prog, DEFAULT_MEM_MB, DEFAULT_PAGE_KB);
}

static int add_item(char ***out, int *n, char *item)
{
	char **grown = realloc(*out, (size_t)(*n + 1) * sizeof(*grown));

	if (!grown) {
		perror("realloc");
		return -1;
	}

	grown[*n] = item;
	*out = grown;
	(*n)++;

	return 0;
}

/*
	getopt_long hands back a single optarg, but the spec's -g and -f take a
	whole list. The extra operands are consumed here, inside the option's own
	case, before getopt_long is called again - so it never scans or permutes
	them. An operand starting with '-' ends the list, which means a file whose
	name starts with a dash cannot be passed; the spec's own naming rules
	forbid such names anyway.
*/
static int collect_list(int argc, char **argv, char ***out, int *n)
{
	if (add_item(out, n, optarg) < 0)
		return -1;

	while (optind < argc && argv[optind][0] != '-') {
		if (add_item(out, n, argv[optind]) < 0)
			return -1;
		optind++;
	}

	return 0;
}

static int parse_memory(const char *s, int *out)
{
	char *end;
	long v = strtol(s, &end, 10);

	if (*s == '\0' || *end != '\0') {
		fprintf(stderr, "error: --memory expects a number (got '%s')\n", s);
		return -1;
	}

	if (v != 2 && v != 4 && v != 8) {
		fprintf(stderr, "error: --memory must be 2, 4 or 8 (got '%s')\n", s);
		return -1;
	}

	*out = (int)v;
	return 0;
}

static int parse_page(const char *s, int *out)
{
	if (!strcasecmp(s, "4") || !strcasecmp(s, "4k") || !strcasecmp(s, "4kb")) {
		*out = 4;
		return 0;
	}

	if (!strcasecmp(s, "2") || !strcasecmp(s, "2m") || !strcasecmp(s, "2mb")) {
		*out = 2048;
		return 0;
	}

	fprintf(stderr, "error: --page must be 4 (4KB) or 2 (2MB) (got '%s')\n", s);
	return -1;
}

int parse_options(int argc, char **argv, struct hv_options *o)
{
	static const struct option longopts[] = {
		{ "memory", required_argument, NULL, 'm' },
		{ "page",   required_argument, NULL, 'p' },
		{ "guest",  required_argument, NULL, 'g' },
		{ "file",   required_argument, NULL, 'f' },
		{ "irq",    no_argument,       NULL, 'i' },
		{ "writer", required_argument, NULL, 'w' },
		{ "verbose", no_argument,      NULL, 'v' },
		{ "help",   no_argument,       NULL, 'h' },
		{ NULL,     0,                 NULL, 0   },
	};
	int c;

	memset(o, 0, sizeof(*o));
	o->mem_mb  = DEFAULT_MEM_MB;
	o->page_kb = DEFAULT_PAGE_KB;

	/* The leading '+' disables GNU permutation: operands are ours to consume
	   in collect_list, so getopt_long must never reorder them. */
	while ((c = getopt_long(argc, argv, "+m:p:g:f:iw:vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (parse_memory(optarg, &o->mem_mb) < 0)
				return -1;
			break;
		case 'p':
			if (parse_page(optarg, &o->page_kb) < 0)
				return -1;
			break;
		case 'g':
			if (collect_list(argc, argv, &o->guests, &o->n_guests) < 0)
				return -1;
			break;
		case 'f':
			if (collect_list(argc, argv, &o->files, &o->n_files) < 0)
				return -1;
			break;
		case 'i':
			o->irq_session = 1;
			break;
		case 'v':
			o->verbose = 1;
			break;
		case 'w': {
			char *end;
			long id = strtol(optarg, &end, 10);

			if (*optarg == '\0' || *end != '\0' || id < 0) {
				fprintf(stderr, "error: --writer expects a VM index (got '%s')\n", optarg);
				return -1;
			}
			o->writer_id = (int)id;
			break;
		}
		case 'h':
			usage(argv[0]);
			return 1;
		default:
			/* getopt_long has already described the problem on stderr. */
			usage(argv[0]);
			return -1;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "error: unexpected operand '%s'\n", argv[optind]);
		usage(argv[0]);
		return -1;
	}

	if (o->n_guests < 1) {
		fprintf(stderr, "error: at least one guest image is required (--guest)\n");
		usage(argv[0]);
		return -1;
	}

	if (o->writer_id >= o->n_guests) {
		fprintf(stderr, "error: --writer %d but only %d guest(s) were given\n",
			o->writer_id, o->n_guests);
		usage(argv[0]);
		return -1;
	}

	if (o->writer_id != 0 && !o->irq_session)
		fprintf(stderr, "warning: --writer has no effect without --irq\n");

	return 0;
}

void free_options(struct hv_options *o)
{
	free(o->guests);
	free(o->files);
	o->guests = NULL;
	o->files = NULL;
	o->n_guests = o->n_files = 0;
}
