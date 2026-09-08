#ifndef PRINT_H
#define PRINT_H

#include <stdint.h>

/* Serial output on port 0xE9 (spec part A). */
void putch(char c);
void print(const char *s);
void print_dec(uint64_t v);
void print_hex(uint64_t v);

/* Serial input on port 0xE9; returns 0 when the host has nothing left. */
char getch(void);

#endif /* PRINT_H */
