#include "syscall.h"
#include "io.h"

/*
	Design D2. The request is built on this function's own stack and its
	address is handed to the hypervisor; pointer arguments inside it are
	ordinary guest addresses, which the host translates the same way, so
	nothing needs serializing.

	Two vmexits per call: the OUT delivers the address, the IN collects the
	result. outl carries a "memory" clobber, so the struct is guaranteed to be
	in memory before the hypervisor is allowed to look at it.

	Guest addresses fit in 32 bits because mem_size never exceeds 8 MB. The
	generalisation to a 64-bit address is two outl's, which is the answer to
	give if asked.
*/
static int hv_call(uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	struct hv_request req;

	req.op   = op;
	req.arg0 = arg0;
	req.arg1 = arg1;
	req.arg2 = arg2;
	req.ret  = -1;

	outl(PORT_FILE, (uint32_t)(uintptr_t)&req);

	return (int)inl(PORT_FILE);
}

int open(const char *path, int flags)
{
	return hv_call(HV_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)flags, 0);
}

int close(int fd)
{
	return hv_call(HV_CLOSE, (uint32_t)fd, 0, 0);
}

int read(int fd, char *buf, int count)
{
	return hv_call(HV_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buf, (uint32_t)count);
}

int write(int fd, const char *buf, int count)
{
	return hv_call(HV_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buf, (uint32_t)count);
}

int lseek(int fd, const int offset, int off_flag)
{
	return hv_call(HV_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)off_flag);
}
