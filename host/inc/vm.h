#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>
#include <linux/kvm.h>

#define MEM_SIZE         (2u * 1024u * 1024u)
#define GUEST_START_ADDR 0x8000

/*
	Design D1 - guest physical memory layout.

	  0x0000            unused
	  0x1000            PML4
	  0x2000            PDPT
	  0x3000            PD
	  0x4000 .. 0x7000  PT[0..3]        (4 KB paging only; 4 PTs == 8 MB, the maximum)
	  0x8000            guest image, then .bss, then the stack growing down from mem_size

	[0, mem_size) is identity mapped, so GVA == GPA and translating a guest
	pointer in phases B and C is v->mem + gva plus a bounds check.
*/
#define PML4_ADDR 0x1000
#define PDPT_ADDR 0x2000
#define PD_ADDR   0x3000
#define PT_BASE   0x4000

/* Room reserved above the image for .bss and the descending stack. */
#define GUEST_MIN_SLACK 0x10000

#define PAGE_4K   0x1000
#define PAGE_2M   0x200000
#define PTES_PER_TABLE 512

#define SERIAL_PORT 0xE9

#define IRQ_NUM   32
#define IRQ_COUNT 3

#define PAGE_SIZE_4K 4      /* --page values, in KB */
#define PAGE_SIZE_2M 2048

#define PDE64_PRESENT (1u << 0)
#define PDE64_RW      (1u << 1)
#define PDE64_USER    (1u << 2)
#define PDE64_PS      (1u << 7)

#define CR0_PE   (1u << 0)
#define CR0_PG   (1u << 31)
#define CR4_PAE  (1u << 5)
#define EFER_LME (1u << 8)
#define EFER_LMA (1u << 10)

/* Everything one guest needs to be brought up. Phase A fills this from the
   command line; phase 0 hardcodes a single VM. */
struct vm_config {
	size_t      mem_size;
	int         page_size;   /* KB: PAGE_SIZE_4K or PAGE_SIZE_2M */
	const char *image;
	int         id;
};

struct vm {
	int kvm_fd;
	int vm_fd;
	int vcpu_fd;
	char *mem;
	size_t mem_size;
	struct kvm_run *run;
	int run_mmap_size;

	int id;
	struct vm_config cfg;
	int irq_pending;

	/* Per-VM line buffer; see output.h. */
	char   outbuf[256];
	size_t outlen;
};

int  vm_init(struct vm *v, const struct vm_config *cfg);
int  vm_setup(struct vm *v, const struct vm_config *cfg);
int  vm_run(struct vm *v);
void vm_destroy(struct vm *v);

void setup_paging_4k(struct vm *v);
void setup_paging_2m(struct vm *v);
int  setup_long_mode(struct vm *v, struct kvm_sregs *sregs);
int  load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr);
int  inject_irq(struct vm *v, unsigned int vector);

const char *kvm_exit_name(uint32_t reason);

#endif /* VM_H */
