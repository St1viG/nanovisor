#include "vm.h"
#include "output.h"
#include "hv_abi.h"
#include "shared_buf.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

int vm_init(struct vm *v, const struct vm_config *cfg)
{
	struct kvm_userspace_memory_region region;
	size_t mem_size = cfg->mem_size;

	memset(v, 0, sizeof(*v));
	v->kvm_fd = v->vm_fd = v->vcpu_fd = -1;
	v->mem = MAP_FAILED;
	v->run = MAP_FAILED;
	v->run_mmap_size = 0;
	v->mem_size = mem_size;
	v->cfg = *cfg;
	v->id = cfg->id;
	v->irq_pending = 0;

	/*
		Set here, not in vm_setup: every teardown path runs vm_destroy ->
		sb_vm_gone, and that has to know this VM's role even when the VM died
		before it was ever set up. A VM that fails to load its image is still
		counted in readers_total, and if it leaves without saying so the writer
		waits for an acknowledgement that can never come.
	*/
	v->role = (enum vm_role)cfg->role;
	v->irq_session_active = cfg->irq_session;

	v->kvm_fd = open("/dev/kvm", O_RDWR);
	if (v->kvm_fd < 0) {
		perror("open /dev/kvm");
		return -1;
	}

	int api = ioctl(v->kvm_fd, KVM_GET_API_VERSION, 0);
	if (api != KVM_API_VERSION) {
		printf("KVM API mismatch: kernel=%d headers=%d\n", api, KVM_API_VERSION);
		return -1;
	}

	v->vm_fd = ioctl(v->kvm_fd, KVM_CREATE_VM, 0);
	if (v->vm_fd < 0) {
		perror("KVM_CREATE_VM");
		return -1;
	}

	v->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v->mem == MAP_FAILED) {
		perror("mmap mem");
		return -1;
	}

	region.slot = 0;
	region.flags = 0;
	region.guest_phys_addr = 0;
	region.memory_size = v->mem_size;
	region.userspace_addr = (uintptr_t)v->mem;
	if (ioctl(v->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
		return -1;
	}

	v->vcpu_fd = ioctl(v->vm_fd, KVM_CREATE_VCPU, 0);
	if (v->vcpu_fd < 0) {
		perror("KVM_CREATE_VCPU");
		return -1;
	}

	v->run_mmap_size = ioctl(v->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (v->run_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
		return -1;
	}

	v->run = mmap(NULL, v->run_mmap_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, v->vcpu_fd, 0);
	if (v->run == MAP_FAILED) {
		perror("mmap kvm_run");
		return -1;
	}

	return 0;
}

void vm_destroy(struct vm *v)
{
	sb_vm_gone(v);
	fileio_vm_destroy(v);

	if (v->run && v->run != MAP_FAILED) {
		munmap(v->run, (size_t)v->run_mmap_size);
		v->run = MAP_FAILED;
	}

	if (v->mem && v->mem != MAP_FAILED) {
		munmap(v->mem, v->mem_size);
		v->mem = MAP_FAILED;
	}

	if (v->vcpu_fd >= 0) {
		close(v->vcpu_fd);
		v->vcpu_fd = -1;
	}

	if (v->vm_fd >= 0) {
		close(v->vm_fd);
		v->vm_fd = -1;
	}

	if (v->kvm_fd >= 0) {
		close(v->kvm_fd);
		v->kvm_fd = -1;
	}
}

static void setup_segments_64(struct kvm_sregs *sregs)
{
	struct kvm_segment code = {
		.base    = 0,
		.limit   = 0xffffffff,
		.present = 1,
		.type    = 11,
		.dpl     = 0,
		.db      = 0,
		.s       = 1,
		.l       = 1,
		.g       = 1,
	};
	struct kvm_segment data = code;
	data.type = 3;
	data.l    = 0;

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}

/*
	Identity map [0, mem_size) with 4 KB pages. mem_size/4096 PTEs are needed:
	512 / 1024 / 2048 for 2 / 4 / 8 MB, i.e. 1 / 2 / 4 page tables, which is
	exactly what fits in the reserved region below GUEST_START_ADDR.
*/
int setup_paging_4k(struct vm *v)
{
	const uint64_t flags = PDE64_PRESENT | PDE64_RW | PDE64_USER;
	uint64_t *pml4 = (void *)(v->mem + PML4_ADDR);
	uint64_t *pdpt = (void *)(v->mem + PDPT_ADDR);
	uint64_t *pd   = (void *)(v->mem + PD_ADDR);
	size_t n_pages = v->mem_size / PAGE_4K;
	size_t n_pts   = (n_pages + PTES_PER_TABLE - 1) / PTES_PER_TABLE;
	size_t i, j;

	/*
		The page tables live below the guest, and 2/4/8 MB need 1/2/4 of them,
		which fills the reserved region exactly. A larger --memory would need
		more tables than fit, and they would be silently overwritten when the
		image is loaded at GUEST_START_ADDR - the guest would then triple-fault
		on the first touch of an address whose table was clobbered, which is
		both nondeterministic and nowhere near the real cause. Refuse instead.

		To actually support more memory with 4 KB pages, raise
		GUEST_START_ADDR (and the origin in guest/guest.ld) to leave room.
	*/
	if (PT_BASE + n_pts * PAGE_4K > GUEST_START_ADDR) {
		fprintf(stderr,
			"error: %zu MB with 4 KB pages needs %zu page tables (%zu bytes), "
			"but only %d bytes are reserved below the guest at 0x%x\n",
			v->mem_size / (1024 * 1024), n_pts, n_pts * PAGE_4K,
			GUEST_START_ADDR - PT_BASE, GUEST_START_ADDR);
		return -1;
	}

	pml4[0] = flags | PDPT_ADDR;
	pdpt[0] = flags | PD_ADDR;

	for (i = 0; i < n_pts; i++) {
		uint64_t pt_addr = PT_BASE + i * PAGE_4K;
		uint64_t *pt = (void *)(v->mem + pt_addr);

		pd[i] = flags | pt_addr;

		for (j = 0; j < PTES_PER_TABLE; j++)
			pt[j] = ((i * PTES_PER_TABLE + j) * PAGE_4K) | flags;
	}

	return 0;
}

/*
	Identity map [0, mem_size) with 2 MB pages: PML4 + PDPT + PD only, with
	PDE64_PS set on 1 / 2 / 4 PD entries. The page tables themselves live
	inside the first 2 MB page they map.
*/
void setup_paging_2m(struct vm *v)
{
	const uint64_t flags = PDE64_PRESENT | PDE64_RW | PDE64_USER;
	uint64_t *pml4 = (void *)(v->mem + PML4_ADDR);
	uint64_t *pdpt = (void *)(v->mem + PDPT_ADDR);
	uint64_t *pd   = (void *)(v->mem + PD_ADDR);
	size_t n_pd = v->mem_size / PAGE_2M;
	size_t i;

	pml4[0] = flags | PDPT_ADDR;
	pdpt[0] = flags | PD_ADDR;

	for (i = 0; i < n_pd; i++)
		pd[i] = (i * PAGE_2M) | flags | PDE64_PS;
}

int setup_long_mode(struct vm *v, struct kvm_sregs *sregs)
{
	/* Entries left untouched below must read as not-present. */
	memset(v->mem, 0, GUEST_START_ADDR);

	switch (v->cfg.page_size) {
	case PAGE_SIZE_4K:
		if (setup_paging_4k(v) < 0)
			return -1;
		break;
	case PAGE_SIZE_2M:
		setup_paging_2m(v);
		break;
	default:
		fprintf(stderr, "unsupported page size %d KB\n", v->cfg.page_size);
		return -1;
	}

	sregs->cr3  = PML4_ADDR;
	sregs->cr4  = CR4_PAE;
	sregs->cr0  = CR0_PE | CR0_PG;
	sregs->efer = EFER_LME | EFER_LMA;

	setup_segments_64(sregs);

	return 0;
}

/*
	Spec part A: "if a VM crashes or an unexpected VM exit occurs, execution of
	that VM (that thread) must be terminated and the error code printed".
	Printing the bare number is not useful at a defense, so map it.
*/
const char *kvm_exit_name(uint32_t reason)
{
	switch (reason) {
	case KVM_EXIT_UNKNOWN:          return "KVM_EXIT_UNKNOWN";
	case KVM_EXIT_EXCEPTION:        return "KVM_EXIT_EXCEPTION";
	case KVM_EXIT_IO:               return "KVM_EXIT_IO";
	case KVM_EXIT_HYPERCALL:        return "KVM_EXIT_HYPERCALL";
	case KVM_EXIT_DEBUG:            return "KVM_EXIT_DEBUG";
	case KVM_EXIT_HLT:              return "KVM_EXIT_HLT";
	case KVM_EXIT_MMIO:             return "KVM_EXIT_MMIO";
	case KVM_EXIT_IRQ_WINDOW_OPEN:  return "KVM_EXIT_IRQ_WINDOW_OPEN";
	case KVM_EXIT_SHUTDOWN:         return "KVM_EXIT_SHUTDOWN";
	case KVM_EXIT_FAIL_ENTRY:       return "KVM_EXIT_FAIL_ENTRY";
	case KVM_EXIT_INTR:             return "KVM_EXIT_INTR";
	case KVM_EXIT_SET_TPR:          return "KVM_EXIT_SET_TPR";
	case KVM_EXIT_TPR_ACCESS:       return "KVM_EXIT_TPR_ACCESS";
	case KVM_EXIT_NMI:              return "KVM_EXIT_NMI";
	case KVM_EXIT_INTERNAL_ERROR:   return "KVM_EXIT_INTERNAL_ERROR";
	case KVM_EXIT_SYSTEM_EVENT:     return "KVM_EXIT_SYSTEM_EVENT";
	default:                        return "unknown exit reason";
	}
}

/*
	A triple fault surfaces as KVM_EXIT_SHUTDOWN with no further detail, so the
	register state is the only clue about where the guest actually died.
*/
static void dump_vcpu(struct vm *v)
{
	struct kvm_regs regs;
	struct kvm_sregs sregs;

	if (ioctl(v->vcpu_fd, KVM_GET_REGS, &regs) == 0)
		out_printf(v->id, "  rip=0x%llx rsp=0x%llx rbp=0x%llx rax=0x%llx rflags=0x%llx\n",
			   (unsigned long long)regs.rip, (unsigned long long)regs.rsp,
			   (unsigned long long)regs.rbp, (unsigned long long)regs.rax,
			   (unsigned long long)regs.rflags);
	else
		out_printf(v->id, "  KVM_GET_REGS failed\n");

	if (ioctl(v->vcpu_fd, KVM_GET_SREGS, &sregs) == 0)
		out_printf(v->id, "  cr0=0x%llx cr3=0x%llx cr4=0x%llx efer=0x%llx cs.sel=0x%x\n",
			   (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
			   (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer,
			   sregs.cs.selector);
	else
		out_printf(v->id, "  KVM_GET_SREGS failed\n");
}

/* Reports an exit this hypervisor does not handle, and ends this VM only. */
static void report_unexpected_exit(struct vm *v)
{
	uint32_t reason = v->run->exit_reason;

	out_printf(v->id, "unexpected exit: %s (%u)\n", kvm_exit_name(reason), reason);

	switch (reason) {
	case KVM_EXIT_FAIL_ENTRY:
		out_printf(v->id, "  hardware_entry_failure_reason=0x%llx\n",
			   (unsigned long long)v->run->fail_entry.hardware_entry_failure_reason);
		break;
	case KVM_EXIT_INTERNAL_ERROR:
		out_printf(v->id, "  suberror=%u\n", v->run->internal.suberror);
		break;
	default:
		break;
	}

	dump_vcpu(v);
}

int vm_setup(struct vm *v, const struct vm_config *cfg)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(v->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		return -1;
	}

	if (setup_long_mode(v, &sregs) < 0)
		return -1;

	if (ioctl(v->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		return -1;
	}

	if (load_guest_image(v, cfg->image, GUEST_START_ADDR) < 0) {
		printf("Failed to load guest image\n");
		return -1;
	}

	memset(&regs, 0, sizeof(regs));
	regs.rflags = 0x2;
	regs.rip    = GUEST_START_ADDR;   /* GVA == GPA; the image is linked at 0x8000 */
	regs.rsp    = v->mem_size;        /* stack grows down from the top of guest memory */

	if (ioctl(v->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		return -1;
	}

	if (fileio_vm_init(v) < 0)
		return -1;

	/*
		In a session the first interrupt assigns the mode and every later one
		is scheduled by the coordinator; outside one, keep the starter's three
		demonstration interrupts.
	*/
	v->irq_pending = cfg->irq_session ? 1 : IRQ_COUNT;

	return 0;
}

/*
	The vCPU dispatch loop. Returns 0 once the guest halts, -1 if the VM could
	not be run. Phase A.7 calls this from one thread per guest, so it must not
	touch any state outside *v.
*/
/*
	Port 0xE9 is the spec's serial port (part A). Any other port is a guest bug
	at this stage; phases B and C add 0x0278, 0x510 and 0x520 here.
*/
/*
	Port 0x510. The spec overloads it three ways and the operand width plus the
	VM's role is what tells them apart:

	  IN,  1 byte, first interrupt : the assigned mode
	  IN,  4 bytes, reader         : how many bytes this round carries
	  IN,  1 byte,  reader         : one byte of the round
	  OUT, 4 bytes, writer         : how many bytes are about to be sent
	  OUT, 1 byte,  writer         : one byte

	Direction is validated against the role, so a reader that tries to write to
	the buffer (or the reverse) is a reported error rather than silent
	corruption of somebody else's round.
*/
static int handle_buf_port(struct vm *v, char *base)
{
	int is_in = (v->run->io.direction == KVM_EXIT_IO_IN);
	uint32_t size = v->run->io.size;

	if (v->run->io.count != 1) {
		out_printf(v->id, "port 0x%x used with count %u, expected 1\n",
			   PORT_BUF, v->run->io.count);
		return -1;
	}

	/* The very first read is the mode assignment, whatever the role. */
	if (is_in && size == 1 && !v->mode_sent) {
		*(uint8_t *)(base + v->run->io.data_offset) =
			(v->role == ROLE_WRITER) ? HV_MODE_WRITE : HV_MODE_READ;
		v->mode_sent = 1;
		return 0;
	}

	if (v->role == ROLE_WRITER) {
		if (is_in) {
			out_printf(v->id, "writer read from port 0x%x\n", PORT_BUF);
			return -1;
		}

		if (size == 4) {
			sb_writer_begin(v, *(uint32_t *)(base + v->run->io.data_offset));
			return 0;
		}

		if (size == 1) {
			if (v->stream != STREAM_ACTIVE) {
				out_printf(v->id, "writer sent a byte before its count\n");
				return -1;
			}
			sb_writer_byte(v, *(uint8_t *)(base + v->run->io.data_offset));
			return 0;
		}
	} else {
		if (!is_in) {
			out_printf(v->id, "reader wrote to port 0x%x\n", PORT_BUF);
			return -1;
		}

		if (size == 4) {
			*(uint32_t *)(base + v->run->io.data_offset) = sb_reader_count(v);
			return 0;
		}

		if (size == 1) {
			if (v->stream != STREAM_ACTIVE) {
				out_printf(v->id, "reader took a byte before its count\n");
				return -1;
			}
			*(uint8_t *)(base + v->run->io.data_offset) = sb_reader_byte(v);
			return 0;
		}
	}

	out_printf(v->id, "port 0x%x used with size %u\n", PORT_BUF, size);

	return -1;
}

/*
	Port 0x520. The writer reads back how many bytes were accepted (the excess
	beyond BUFFER_SIZE is discarded); a reader reports how many it read, and is
	stopped if that is short of the round.
*/
static int handle_ack_port(struct vm *v, char *base)
{
	if (v->run->io.size != 4 || v->run->io.count != 1) {
		out_printf(v->id, "port 0x%x used with size %u count %u, expected 4/1\n",
			   PORT_ACK, v->run->io.size, v->run->io.count);
		return -1;
	}

	if (v->role == ROLE_WRITER) {
		if (v->run->io.direction != KVM_EXIT_IO_IN) {
			out_printf(v->id, "writer wrote to port 0x%x\n", PORT_ACK);
			return -1;
		}

		/*
			Publishing is only meaningful after a count has been sent on
			0x510. Without this the stale stream_expected of 0 would be read
			as the end-of-stream sentinel and silently kill the session.
		*/
		if (v->stream != STREAM_ACTIVE) {
			out_printf(v->id, "writer read port 0x%x without sending a count\n", PORT_ACK);
			return -1;
		}

		*(uint32_t *)(base + v->run->io.data_offset) = (uint32_t)sb_writer_publish(v);
		return 0;
	}

	if (v->run->io.direction != KVM_EXIT_IO_OUT) {
		out_printf(v->id, "reader read from port 0x%x\n", PORT_ACK);
		return -1;
	}

	if (sb_reader_ack(v, *(uint32_t *)(base + v->run->io.data_offset)) < 0) {
		out_printf(v->id, "read fewer bytes than the round carried; stopping this VM\n");
		return -1;
	}

	return 0;
}

static int handle_io(struct vm *v)
{
	char *base = (char *)v->run;

	if (v->run->io.port == SERIAL_PORT) {
		if (v->run->io.size != 1) {
			out_printf(v->id, "serial port 0x%x used with size %u, expected 1\n",
				   SERIAL_PORT, v->run->io.size);
			return -1;
		}

		if (v->run->io.direction == KVM_EXIT_IO_OUT) {
			out_char(v, *(base + v->run->io.data_offset));
			return 0;
		}

		/* IN: hand the guest one byte of the hypervisor's stdin. */
		*(unsigned char *)(base + v->run->io.data_offset) = in_byte();
		return 0;
	}

	if (v->run->io.port == PORT_FILE) {
		uint32_t *slot;

		/*
			Risk 10: data_offset is a byte offset into the kvm_run page, size is
			the operand width and count is the string-op repeat count. Assert
			loudly rather than mis-decode: the phase B ABI is 32 bits wide, once.
		*/
		if (v->run->io.size != 4 || v->run->io.count != 1) {
			out_printf(v->id, "file port 0x%x used with size %u count %u, expected 4/1\n",
				   PORT_FILE, v->run->io.size, v->run->io.count);
			return -1;
		}

		slot = (uint32_t *)(base + v->run->io.data_offset);

		if (v->run->io.direction == KVM_EXIT_IO_OUT)
			return hv_file_request(v, *slot);   /* OUT carries the request address */

		*slot = (uint32_t)v->last_ret;          /* IN collects the result */
		return 0;
	}

	if (v->irq_session_active && v->run->io.port == PORT_BUF)
		return handle_buf_port(v, base);

	if (v->irq_session_active && v->run->io.port == PORT_ACK)
		return handle_ack_port(v, base);

	out_printf(v->id, "unhandled IO %s on port 0x%x (size %u)\n",
	       v->run->io.direction == KVM_EXIT_IO_OUT ? "OUT" : "IN",
	       v->run->io.port, v->run->io.size);

	return -1;
}

int vm_run(struct vm *v)
{
	v->run->request_interrupt_window = (v->irq_pending > 0);

	for (;;) {
		if (ioctl(v->vcpu_fd, KVM_RUN, 0) == -1) {
			out_flush(v);
			out_printf(v->id, "KVM_RUN failed: %s\n", strerror(errno));
			return -1;
		}

		switch (v->run->exit_reason) {
		case KVM_EXIT_IO:
			if (handle_io(v) < 0) {
				out_flush(v);
				return -1;
			}
			break;
		case KVM_EXIT_IRQ_WINDOW_OPEN:
			if (v->irq_pending > 0) {
				if (inject_irq(v, IRQ_NUM) < 0)
					return -1;
				v->irq_pending--;
			} else {
				v->run->request_interrupt_window = 0;
			}
			break;
		case KVM_EXIT_HLT:
			/*
				Design D5. The spec wants "terminate on hlt" in phase A and
				"keep handling further interrupts" in phase C, which conflict:
				with no in-kernel irqchip a halted vCPU exits immediately. The
				hypervisor knows whether a session is live, so the guest needs
				no signalling - hlt means "idle, awaiting the next interrupt"
				during a session and "done" outside one.
			*/
			if (v->irq_session_active) {
				if (!sb_wait_turn(v)) {
					v->irq_pending = 1;
					v->run->request_interrupt_window = 1;
					break;
				}
				v->irq_session_active = 0;
			}
			out_flush(v);
			out_printf(v->id, "KVM_EXIT_HLT\n");
			return 0;
		default:
			out_flush(v);
			report_unexpected_exit(v);
			return -1;
		}
	}
}

int load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr)
{
	struct stat st;
	long fsz;

	FILE *f = fopen(image_path, "rb");
	if (!f) {
		perror("Failed to open guest image");
		return -1;
	}

	/*
		Check what this actually is before trusting its size. ftell on a
		directory reports LONG_MAX, which produced a nonsensical "does not fit"
		message, and an empty file loaded fine and then triple-faulted on the
		zeros at GUEST_START_ADDR - both diagnosing the wrong thing.
	*/
	if (fstat(fileno(f), &st) < 0) {
		perror("Failed to stat guest image");
		fclose(f);
		return -1;
	}

	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr, "guest image %s is not a regular file\n", image_path);
		fclose(f);
		return -1;
	}

	if (st.st_size == 0) {
		fprintf(stderr, "guest image %s is empty\n", image_path);
		fclose(f);
		return -1;
	}

	fsz = (long)st.st_size;

	/*
		Risk 7: the image is only the start of what the guest occupies - .bss
		follows it and the stack grows down from mem_size. Refuse an image that
		leaves no room between the two rather than triple-faulting on first touch.
	*/
	if (load_addr >= v->mem_size ||
	    (uint64_t)fsz + GUEST_MIN_SLACK > v->mem_size - load_addr) {
		fprintf(stderr, "guest image %s (%ld bytes) does not fit in %zu bytes of guest memory\n",
			image_path, fsz, v->mem_size);
		fclose(f);
		return -1;
	}

	if (fread((uint8_t *)v->mem + load_addr, 1, (size_t)fsz, f) != (size_t)fsz) {
		perror("Failed to read guest image");
		fclose(f);
		return -1;
	}
	fclose(f);

	return 0;
}

void *guest_ptr(struct vm *v, uint64_t gva, size_t len)
{
	/* Written to survive any gva/len the guest can produce, overflow included. */
	if (len > v->mem_size)
		return NULL;

	if (gva > v->mem_size - len)
		return NULL;

	return v->mem + gva;
}

const char *guest_str(struct vm *v, uint64_t gva, size_t max_len)
{
	const char *p;
	size_t avail, i;

	if (gva >= v->mem_size)
		return NULL;

	p = v->mem + gva;
	avail = v->mem_size - gva;
	if (avail > max_len)
		avail = max_len;

	for (i = 0; i < avail; i++) {
		if (p[i] == '\0')
			return p;
	}

	/* Unterminated within the bound: refuse rather than read past it. */
	return NULL;
}

int inject_irq(struct vm *v, unsigned int vector)
{
	struct kvm_interrupt irq = { .irq = vector };

	if (ioctl(v->vcpu_fd, KVM_INTERRUPT, &irq) < 0) {
		perror("KVM_INTERRUPT");
		return -1;
	}
	return 0;
}
