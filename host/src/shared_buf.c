#include "shared_buf.h"
#include "vm.h"
#include "output.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Risk 2: a hung VM is otherwise invisible. Report, then keep waiting. */
#define WATCHDOG_SECONDS 5

static struct shared_buf buf;
static int verbose_rounds;

/* Called with buf.m held. */
static void trace(struct vm *v, const char *what)
{
	if (verbose_rounds)
		out_printf(v->id, "trace: %s (round=%llu len=%u readers_pending=%d)\n",
			   what, (unsigned long long)buf.round, buf.len, buf.readers_pending);
}

void shared_buf_init(int readers_total, int verbose)
{
	memset(&buf, 0, sizeof(buf));
	verbose_rounds = verbose;
	pthread_mutex_init(&buf.m, NULL);
	pthread_cond_init(&buf.cv, NULL);
	buf.readers_total = readers_total;
	buf.readers_pending = 0;
}

void shared_buf_destroy(void)
{
	pthread_mutex_destroy(&buf.m);
	pthread_cond_destroy(&buf.cv);
}

static void dump_state(struct vm *v, const char *waiting_for)
{
	out_printf(v->id, "watchdog: %ds waiting for %s "
			  "(role=%s round=%llu last_round=%llu readers_total=%d "
			  "readers_pending=%d eof=%d over=%d)\n",
		   WATCHDOG_SECONDS, waiting_for,
		   v->role == ROLE_WRITER ? "writer" : "reader",
		   (unsigned long long)buf.round, (unsigned long long)v->last_round,
		   buf.readers_total, buf.readers_pending, buf.eof, buf.over);
}

/*
	Waits with a watchdog. Returns when pred() holds, reporting every
	WATCHDOG_SECONDS so a deadlock names itself instead of hanging silently.
	Called with buf.m held.
*/
static void wait_with_watchdog(struct vm *v, int (*pred)(struct vm *), const char *what)
{
	while (!pred(v)) {
		struct timespec deadline;

		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += WATCHDOG_SECONDS;

		if (pthread_cond_timedwait(&buf.cv, &buf.m, &deadline) == ETIMEDOUT && !pred(v))
			dump_state(v, what);
	}
}

static int writer_ready(struct vm *v)
{
	(void)v;
	return buf.over || buf.readers_pending == 0;
}

static int reader_ready(struct vm *v)
{
	return buf.over || buf.round > v->last_round;
}

int sb_wait_turn(struct vm *v)
{
	int over;

	pthread_mutex_lock(&buf.m);

	if (v->role == ROLE_WRITER) {
		wait_with_watchdog(v, writer_ready, "readers to finish the round");
		/*
			The sentinel round has been published and everyone has consumed it,
			so there is nothing left to write. Design D6.
		*/
		if (buf.eof && buf.readers_pending == 0)
			buf.over = 1;
	} else {
		wait_with_watchdog(v, reader_ready, "the writer to publish a round");
	}

	over = buf.over;
	if (over)
		pthread_cond_broadcast(&buf.cv);

	pthread_mutex_unlock(&buf.m);

	return over;
}

/* ----------------------------------------------------------- writer side */

int sb_writer_begin(struct vm *v, uint32_t count)
{
	pthread_mutex_lock(&buf.m);

	v->stream_expected = count;
	v->stream_index = 0;
	v->stream = STREAM_ACTIVE;
	buf.len = count > BUFFER_SIZE ? BUFFER_SIZE : count;

	pthread_mutex_unlock(&buf.m);

	return 0;
}

int sb_writer_byte(struct vm *v, uint8_t byte)
{
	pthread_mutex_lock(&buf.m);

	/*
		Spec: if more bytes are sent than BUFFER_SIZE, the hypervisor ignores
		the excess. They are still accepted off the port - refusing them would
		desynchronise the guest, which is mid-loop.
	*/
	if (v->stream_index < BUFFER_SIZE)
		buf.data[v->stream_index] = byte;

	v->stream_index++;

	pthread_mutex_unlock(&buf.m);

	return 0;
}

int32_t sb_writer_publish(struct vm *v)
{
	int32_t accepted;

	pthread_mutex_lock(&buf.m);

	accepted = (int32_t)buf.len;

	if (v->stream_expected == 0)
		buf.eof = 1;              /* design D6: count == 0 is end of stream */

	buf.round++;
	buf.readers_pending = buf.readers_total;
	trace(v, "writer published");

	v->stream = STREAM_EXPECT_COUNT;
	v->stream_index = 0;
	v->stream_expected = 0;

	pthread_cond_broadcast(&buf.cv);
	pthread_mutex_unlock(&buf.m);

	return accepted;
}

/* ----------------------------------------------------------- reader side */

uint32_t sb_reader_count(struct vm *v)
{
	uint32_t len;

	pthread_mutex_lock(&buf.m);

	v->last_round = buf.round;
	v->round_pending = 1;
	v->stream = STREAM_ACTIVE;
	v->stream_index = 0;
	v->stream_expected = buf.len;
	len = buf.len;
	trace(v, "reader took round");

	pthread_mutex_unlock(&buf.m);

	return len;
}

uint8_t sb_reader_byte(struct vm *v)
{
	uint8_t byte = 0;

	pthread_mutex_lock(&buf.m);

	if (v->stream_index < buf.len)
		byte = buf.data[v->stream_index];

	v->stream_index++;

	pthread_mutex_unlock(&buf.m);

	return byte;
}

int sb_reader_ack(struct vm *v, uint32_t n_read)
{
	int short_read;

	pthread_mutex_lock(&buf.m);

	short_read = (n_read < buf.len);

	v->stream = STREAM_EXPECT_COUNT;
	v->stream_index = 0;

	if (v->round_pending) {
		v->round_pending = 0;
		if (buf.readers_pending > 0)
			buf.readers_pending--;
	}

	trace(v, "reader acked");
	pthread_cond_broadcast(&buf.cv);
	pthread_mutex_unlock(&buf.m);

	/* Spec: if not all bytes were read, VM execution must be stopped. */
	return short_read ? -1 : 0;
}

/* --------------------------------------------------------------- teardown */

void sb_vm_gone(struct vm *v)
{
	if (!v->irq_session_active)
		return;

	pthread_mutex_lock(&buf.m);

	/*
		Risk 3: a reader that dies mid-round would otherwise leave
		readers_pending non-zero for ever and hang the writer. Teardown counts
		as "I am finished with this round".
	*/
	if (v->role == ROLE_READER) {
		/*
			A departing reader still owes an acknowledgement for the current
			round in two distinct cases: it took the round and died before
			acking (round_pending), or it was counted in readers_pending when
			the round was published and died before ever taking it
			(last_round < round). Only a reader that has already acked the
			current round owes nothing.

			Missing the second case deadlocks the writer whenever a reader dies
			between a publish and its own first read - which is a race, so it
			reproduced roughly one run in six.
		*/
		int owes_ack = v->round_pending || v->last_round < buf.round;

		if (owes_ack && buf.readers_pending > 0)
			buf.readers_pending--;
		if (buf.readers_total > 0)
			buf.readers_total--;
		if (buf.readers_pending > buf.readers_total)
			buf.readers_pending = buf.readers_total;
	} else if (v->role == ROLE_WRITER) {
		/* No writer left means no further rounds can ever be published. */
		buf.over = 1;
	}

	v->round_pending = 0;
	v->irq_session_active = 0;

	pthread_cond_broadcast(&buf.cv);
	pthread_mutex_unlock(&buf.m);
}

void sb_vm_never_started(int role)
{
	pthread_mutex_lock(&buf.m);

	if (role == ROLE_READER) {
		if (buf.readers_total > 0)
			buf.readers_total--;
		/* Drops any obligation this VM was carrying for the current round. */
		if (buf.readers_pending > buf.readers_total)
			buf.readers_pending = buf.readers_total;
	} else if (role == ROLE_WRITER) {
		buf.over = 1;
	}

	pthread_cond_broadcast(&buf.cv);
	pthread_mutex_unlock(&buf.m);
}
