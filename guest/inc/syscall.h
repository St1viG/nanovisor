#ifndef SYSCALL_H
#define SYSCALL_H

#include "hv_abi.h"

/*
	The spec's own names, for guest code. They are defined here rather than in
	the shared ABI header because on the host side they would collide with
	<fcntl.h> and <stdio.h>, which use the same names for different values.
*/
#define O_RD     HV_O_RD
#define O_WR     HV_O_WR
#define O_RDWR   HV_O_RDWR
#define O_CREATE HV_O_CREATE

#define SEEK_SET HV_SEEK_SET
#define SEEK_END HV_SEEK_END

/* Signatures exactly as given in the assignment. */
int open(const char *path, int flags);
int close(int fd);
int read(int fd, char *buf, int count);
int write(int fd, const char *buf, int count);
int lseek(int fd, const int offset, int off_flag);

#endif /* SYSCALL_H */
