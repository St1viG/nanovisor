#include "guest.h"
#include "print.h"
#include "syscall.h"
#include "io.h"
#include "hv_abi.h"

/*
	Hostile guest, kept as permanent coverage of the request validation: every
	value below is one a correct guest would never send. Each must be refused
	with -1 and the VM must keep running; the final "survived" line is the proof.
*/

static int raw_call(uint32_t addr)
{
	outl(PORT_FILE, addr);
	return (int)inl(PORT_FILE);
}

void guest_main(void)
{
	struct hv_request req;
	char buf[16];
	int fd;

	print("1. request struct past end of memory: ");
	print_dec((uint64_t)(uint32_t)raw_call(0xFFFFFFF0u));
	print("\n");

	print("2. request struct straddling the very top: ");
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)guest_mem_top - 4));
	print("\n");

	fd = open("h.txt", O_RDWR | O_CREATE);
	write(fd, "0123456789", 10);

	print("3. read into a buffer past end of memory: ");
	req.op = HV_READ; req.arg0 = (uint32_t)fd;
	req.arg1 = 0xFFFFFF00u; req.arg2 = 16; req.ret = 0;
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)(uintptr_t)&req));
	print("\n");

	print("4. read with a count that overflows the buffer: ");
	req.op = HV_READ; req.arg0 = (uint32_t)fd;
	req.arg1 = (uint32_t)(uintptr_t)buf; req.arg2 = 0xFFFFFFF0u; req.ret = 0;
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)(uintptr_t)&req));
	print("\n");

	print("5. write from a buffer past end of memory: ");
	req.op = HV_WRITE; req.arg0 = (uint32_t)fd;
	req.arg1 = (uint32_t)guest_mem_top - 4; req.arg2 = 64; req.ret = 0;
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)(uintptr_t)&req));
	print("\n");

	print("6. open with a path that has no terminator in range: ");
	req.op = HV_OPEN; req.arg0 = (uint32_t)guest_mem_top - 2;
	req.arg1 = O_RD; req.arg2 = 0; req.ret = 0;
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)(uintptr_t)&req));
	print("\n");

	print("7. unknown opcode: ");
	req.op = 999; req.arg0 = 0; req.arg1 = 0; req.arg2 = 0; req.ret = 0;
	print_dec((uint64_t)(uint32_t)raw_call((uint32_t)(uintptr_t)&req));
	print("\n");

	close(fd);
	print("survived\n");
}
