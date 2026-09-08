#ifndef SHARED_BUF_H
#define SHARED_BUF_H

#include <stdint.h>
#include <pthread.h>

#include "hv_abi.h"

struct vm;

/*
	The hypervisor-controlled buffer every guest communicates through.

	One writer VM stages a round and publishes it; every reader consumes that
	round and acknowledges. The writer may not start round N+1 until all
	readers have acknowledged round N, which is the barrier the spec asks for.

	round is monotonic and each VM remembers the last one it saw, so a reader
	that finishes early cannot consume the wakeup meant for the next round -
	it simply finds round == last_round and waits again.
*/
struct shared_buf {
	pthread_mutex_t m;
	pthread_cond_t  cv;

	uint8_t  data[BUFFER_SIZE];
	uint32_t len;              /* bytes valid in the published round */
	uint64_t round;            /* incremented once per published round */

	int readers_total;         /* readers still alive */
	int readers_pending;       /* readers that have not acknowledged this round */
	int eof;                   /* the count == 0 sentinel round was published */
	int over;                  /* session finished; every VM may terminate */
};

void shared_buf_init(int readers_total);
void shared_buf_destroy(void);

/*
	Blocks this VM's thread until it should receive its next vector-32
	interrupt. Returns 1 when the session is over and the VM should halt for
	real (design D5). This is the only place a VM thread blocks.
*/
int sb_wait_turn(struct vm *v);

/* Writer side, all non-blocking: the barrier has already been passed. */
int     sb_writer_begin(struct vm *v, uint32_t count);
int     sb_writer_byte(struct vm *v, uint8_t byte);
int32_t sb_writer_publish(struct vm *v);

/* Reader side, likewise non-blocking. */
uint32_t sb_reader_count(struct vm *v);
uint8_t  sb_reader_byte(struct vm *v);
int      sb_reader_ack(struct vm *v, uint32_t n_read);

/* A VM is going away; keeps a dead reader from deadlocking the writer. */
void sb_vm_gone(struct vm *v);

#endif /* SHARED_BUF_H */
