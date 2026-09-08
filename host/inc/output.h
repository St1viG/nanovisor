#ifndef OUTPUT_H
#define OUTPUT_H

struct vm;

/*
	All hypervisor and guest output goes through here.

	Guest bytes arriving on port 0xE9 are accumulated in a per-VM line buffer
	and only handed to stdout a whole line at a time, under a global mutex, so
	N concurrent guests interleave by line instead of by character (risk 11).
*/
void out_char(struct vm *v, char c);
void out_flush(struct vm *v);
void out_printf(int id, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* OUTPUT_H */
