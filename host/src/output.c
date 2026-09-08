#include "output.h"
#include "vm.h"

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

/* One lock for stdout; held only for the duration of a single write. */
static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;

static void emit_locked(int id, const char *text, int len)
{
	pthread_mutex_lock(&out_lock);
	printf("[vm %d] %.*s", id, len, text);
	fflush(stdout);
	pthread_mutex_unlock(&out_lock);
}

void out_flush(struct vm *v)
{
	if (v->outlen == 0)
		return;

	/*
		A line that overflowed the buffer is emitted with a newline appended:
		better a split long line than a prefix appearing mid-line in another
		VM's output.
	*/
	if (v->outbuf[v->outlen - 1] != '\n')
		v->outbuf[v->outlen++] = '\n';

	emit_locked(v->id, v->outbuf, (int)v->outlen);
	v->outlen = 0;
}

void out_char(struct vm *v, char c)
{
	v->outbuf[v->outlen++] = c;

	if (c == '\n' || v->outlen >= sizeof(v->outbuf) - 1)
		out_flush(v);
}

void out_printf(int id, const char *fmt, ...)
{
	char line[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	if (n < 0)
		return;
	if (n > (int)sizeof(line) - 1)
		n = (int)sizeof(line) - 1;

	emit_locked(id, line, n);
}
