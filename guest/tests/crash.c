#include "guest.h"
#include "print.h"

/*
	Fault isolation demo (A.8): ud2 raises #UD, and with no IDT entry for
	vector 6 that escalates to a triple fault, which surfaces on the host as
	KVM_EXIT_SHUTDOWN. The sibling VMs must be unaffected.
*/
void guest_main(void)
{
	print("crash: about to execute ud2\n");
	asm volatile("ud2");
	print("crash: unreachable\n");
}
