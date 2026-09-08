#include "print.h"
#include "io.h"

#define SERIAL_PORT 0xE9

void putch(char c)
{
	outb(SERIAL_PORT, (uint8_t)c);
}

void print(const char *s)
{
	while (*s)
		putch(*s++);
}

void print_dec(uint64_t v)
{
	char buf[21];
	int i = 0;

	if (v == 0) {
		putch('0');
		return;
	}

	while (v > 0) {
		buf[i++] = (char)('0' + (v % 10));
		v /= 10;
	}

	while (i > 0)
		putch(buf[--i]);
}

void print_hex(uint64_t v)
{
	static const char digits[] = "0123456789abcdef";
	int shift = 60;
	int leading = 1;

	print("0x");

	while (shift >= 0) {
		unsigned nib = (unsigned)((v >> shift) & 0xF);

		if (nib != 0 || !leading || shift == 0) {
			putch(digits[nib]);
			leading = 0;
		}
		shift -= 4;
	}
}
