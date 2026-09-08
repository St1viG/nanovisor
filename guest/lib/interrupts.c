#include "descriptors.h"
#include "interrupts.h"
#include "irqproto.h"
#include "io.h"

static struct idt_entry idt[IDT_ENTRIES];

/*
	Declared weak and left undefined here: an image that does not implement the
	phase C protocol leaves these resolving to NULL, and the handler keeps its
	phase A behaviour. No per-image opt-in flag is needed.
*/
extern void guest_writer_round(void) __attribute__((weak));
extern void guest_reader_round(void) __attribute__((weak));

/*
	SSE instrukcije su zabranjene unutar __attribute__((interrupt)) handlera;
	sprečava ih -mgeneral-regs-only, koji se sada primenjuje na ceo guest
	(vidi guest/Makefile).
*/
static void __attribute__((interrupt))
irq32_handler(struct interrupt_frame *frame)
{
	(void)frame;

	if (!guest_writer_round && !guest_reader_round) {
		const char *s;

		for (s = "IRQ0 received!\n"; *s; ++s)
			outb(0xE9, *s);
		return;
	}

	/* Spec: the first interrupt assigns the operating mode, which is saved. */
	if (guest_mode < 0) {
		guest_mode = inb(PORT_BUF);
		return;
	}

	if (guest_mode == HV_MODE_WRITE) {
		if (guest_writer_round)
			guest_writer_round();
	} else {
		if (guest_reader_round)
			guest_reader_round();
	}
}

static void set_idt_gate(unsigned n, void (*handler)(struct interrupt_frame *))
{
	uint64_t addr = (uint64_t)(uintptr_t)handler;
	idt[n].offset_low  = addr & 0xFFFF;
	idt[n].selector    = 0x08;  /* 64-bit code segment */
	idt[n].ist         = 0;
	idt[n].type_attr   = 0x8E;  /* P=1, DPL=0, 64-bit interrupt gate */
	idt[n].offset_mid  = (addr >> 16) & 0xFFFF;
	idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
	idt[n].reserved    = 0;
}

void init_idt(void)
{
	struct dt_ptr p;

	set_idt_gate(32, irq32_handler);

	p.limit = sizeof(idt) - 1;
	p.base  = (uint64_t)(uintptr_t)idt;
	asm volatile("lidt %0" : : "m"(p) : "memory");
}
