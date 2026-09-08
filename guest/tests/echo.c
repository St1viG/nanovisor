#include "guest.h"
#include "print.h"

/*
	Serial input demo (A.5): reads bytes from port 0xE9 until the host reports
	end of input, echoing each one back out on the same port.
*/
void guest_main(void)
{
	char c;

	print("echo: reading from port 0xE9\n");

	while ((c = getch()) != 0)
		putch(c);

	print("\necho: end of input\n");
}
