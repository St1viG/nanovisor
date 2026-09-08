#ifndef IO_H
#define IO_H

#include <stdint.h>

/*
	Port I/O. Every access here traps to the hypervisor as KVM_EXIT_IO, so the
	operand width chosen on this side is exactly what the host sees in
	run->io.size — the two sides must agree.
*/

static inline void outb(uint16_t port, uint8_t value)
{
	asm volatile("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static inline void outw(uint16_t port, uint16_t value)
{
	asm volatile("outw %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static inline void outl(uint16_t port, uint32_t value)
{
	asm volatile("outl %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;

	asm volatile("inb %1,%0" : "=a" (value) : "Nd" (port) : "memory");
	return value;
}

static inline uint16_t inw(uint16_t port)
{
	uint16_t value;

	asm volatile("inw %1,%0" : "=a" (value) : "Nd" (port) : "memory");
	return value;
}

static inline uint32_t inl(uint16_t port)
{
	uint32_t value;

	asm volatile("inl %1,%0" : "=a" (value) : "Nd" (port) : "memory");
	return value;
}

/*
	CPU control. hlt is how a guest finishes (spec part A) and, while a phase C
	session is live, how it idles until the next injected interrupt (design
	D5). cli/sti bracket a section that must not be interrupted.
*/
static inline void cli(void)
{
	asm volatile("cli" ::: "memory");
}

static inline void sti(void)
{
	asm volatile("sti" ::: "memory");
}

static inline void hlt(void)
{
	asm volatile("hlt" ::: "memory");
}

#endif /* IO_H */
