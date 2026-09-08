#ifndef GUEST_H
#define GUEST_H

#include <stdint.h>

/*
	Every image in tests/ provides guest_main(). lib/start.c owns _start: it
	zeroes .bss, installs the GDT and IDT, enables interrupts and then calls
	guest_main(). Returning from guest_main halts the VM.
*/
void guest_main(void);

/*
	Top of guest physical memory, i.e. the mem_size the hypervisor was started
	with. The host sets rsp to mem_size before the first instruction, so the
	guest can recover it from the initial stack pointer without a new port.
*/
extern uint64_t guest_mem_top;

#endif /* GUEST_H */
