#include "descriptors.h"
#include "interrupts.h"
#include "io.h"

static struct gdt_entry gdt[3];

/* Provided by guest.ld; delimit the NOBITS .bss region. */
extern char __bss_start[], __bss_end[];

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	struct dt_ptr p;
	char *b;

	/*
		.bss is NOBITS, so it is not part of the flat image the hypervisor loads.
		It currently reads as zero only because host-side guest memory comes from
		mmap(MAP_ANONYMOUS); zero it here so correctness does not depend on that.
		Must be the first statement: gdt and idt both live in .bss.
	*/
	for (b = __bss_start; b != __bss_end; ++b)
		*b = 0;

	gdt[0] = (struct gdt_entry){ 0 };
	gdt[1] = (struct gdt_entry){  /* 64-bit code, selector 0x08: P=1, DPL=0, S=1, type=0xA, L=1, G=1 */
		.limit_low   = 0xFFFF,
		.access      = 0x9A,
		.flags_limit = 0xAF,
	};
	gdt[2] = (struct gdt_entry){  /* 64-bit data, selector 0x10: P=1, DPL=0, S=1, type=0x2, D/B=1, G=1 */
		.limit_low   = 0xFFFF,
		.access      = 0x92,
		.flags_limit = 0xCF,
	};

	p.limit = sizeof(gdt) - 1;
	p.base  = (uint64_t)(uintptr_t)gdt;
	asm volatile("lgdt %0" : : "m"(p) : "memory");

	/* Reload CS to 0x08 and continue at label 1 */
	asm volatile(
		"pushq $0x08\n\t"
		"lea 1f(%%rip), %%rax\n\t"
		"pushq %%rax\n\t"
		"lretq\n\t"
		"1:\n\t"
		::: "rax", "memory"
	);

	/* Reload data segment selectors to 0x10 */
	asm volatile(
		"movl $0x10, %%eax\n\t"
		"movw %%ax, %%ds\n\t"
		"movw %%ax, %%es\n\t"
		"movw %%ax, %%ss\n\t"
		::: "eax", "memory"
	);

	init_idt();

	asm volatile("sti");

	const char *s;
	for (s = "Hello, world!\n"; *s; ++s)
		outb(0xE9, *s);

	for (;;)
		asm volatile("hlt");
}
