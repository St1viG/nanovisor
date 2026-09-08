#include "string.h"

#include <stdint.h>

/*
	Deliberately naive byte loops. -fno-tree-loop-distribute-patterns (see
	guest/Makefile) is what stops -O2 from recognising these loops and
	rewriting them into calls to the very functions being defined.
*/

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;

	while (n--)
		*d++ = (uint8_t)c;

	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	while (n--)
		*d++ = *s++;

	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if (d == s || n == 0)
		return dst;

	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}

	return dst;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		++p;

	return (size_t)(p - s);
}
