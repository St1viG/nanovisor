#ifndef STRING_H
#define STRING_H

#include <stddef.h>

/*
	GCC emits calls to these even under -ffreestanding (structure assignment,
	array initialisation, loop-idiom recognition), so a freestanding image has
	to provide them itself.
*/

void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

#endif /* STRING_H */
