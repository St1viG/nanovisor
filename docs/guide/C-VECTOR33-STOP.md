# C. Vector 33: `k` on the writer's stdin stops every VM

**Task.** Add an input interrupt with vector 33. After each round written to the
shared buffer, the writer VM reads one byte of standard input with IN. If it is
`k`, the hypervisor stops every VM. Reader VMs stop in their vector 33 handler.

**Where it lives.** The phase C coordinator is `host/src/shared_buf.c`: the only
place a VM thread blocks is `sb_wait_turn`, which the run loop in `host/src/vm.c`
calls on every `hlt` during a session and which decides whether the next
interrupt is due (design D5). The writer's stdin read is an IN on the serial port,
which lands in `handle_io`, so the host sees the byte on its way to the guest.
Interrupt delivery is `irq_pending` plus `request_interrupt_window`, and each
thread injects into its own vCPU (design D7). Guest handlers are registered in
`guest/lib/interrupts.c`; images opt into protocol roles with weak hooks.

**Design in one paragraph.** A `stopped` flag in the shared buffer. The host sets it
when the writer's IN returns `k` and broadcasts the condvar. `sb_wait_turn` grows a
third answer, "stopped", beside "next round" and "session over". A reader that
still has a published round to consume takes that first, so every reader ends
with exactly the rounds the writer sent. On "stopped", the run loop marks the
session inactive, arms vector 33 instead of 32 for the next injection, and
requests the window. The guest's vector 33 handler calls the image's `guest_stop`
hook (close the file, say so) and returns; the main loop's next `hlt` terminates
the VM, because the session is inactive. Nothing new blocks and no thread touches
another thread's vCPU.

## Steps

1. **ABI.** `common/hv_abi.h`, under `IRQ_VECTOR`:

   ```c
   #define IRQ_VECTOR_STOP 33
   #define STOP_CHAR       'k'
   ```

2. **Per-VM state.** `host/inc/vm.h`, `struct vm`: `int stop_pending;` (the next
   injection is vector 33).

3. **Coordinator state and API.** `host/inc/shared_buf.h`: `int stopped;` in
   `struct shared_buf`, return codes for `sb_wait_turn`, and one new function:

   ```c
   #define SB_ROUND   0
   #define SB_OVER    1
   #define SB_STOPPED 2
   int  sb_wait_turn(struct vm *v);
   void sb_stop_all(struct vm *v);
   ```

4. **Wake everyone.** `host/src/shared_buf.c`: both ready predicates also return
   true on `buf.stopped`, and `sb_wait_turn` returns a code instead of `over`:

   ```c
   if (v->role == ROLE_WRITER) {
   	wait_with_watchdog(v, writer_ready, "readers to finish the round");
   	if (buf.eof && buf.readers_pending == 0)
   		buf.over = 1;
   	rc = buf.stopped ? SB_STOPPED : (buf.over ? SB_OVER : SB_ROUND);
   } else {
   	wait_with_watchdog(v, reader_ready, "the writer to publish a round");
   	if (buf.over)
   		rc = SB_OVER;
   	else if (buf.round > v->last_round)
   		rc = SB_ROUND;          /* consume the published round first */
   	else
   		rc = SB_STOPPED;
   }
   if (rc != SB_ROUND)
   	pthread_cond_broadcast(&buf.cv);
   ```

   and the new function:

   ```c
   void sb_stop_all(struct vm *v)
   {
   	pthread_mutex_lock(&buf.m);
   	if (!buf.stopped) {
   		buf.stopped = 1;
   		out_printf(v->id, "stop: '%c' on stdin, stopping every VM\n", STOP_CHAR);
   	}
   	pthread_cond_broadcast(&buf.cv);
   	pthread_mutex_unlock(&buf.m);
   }
   ```

   Add `stopped=%d` to `dump_state` so the watchdog shows it.

5. **See the byte.** `host/src/vm.c`, the serial IN branch of `handle_io`, right
   after the byte is stored in the slot:

   ```c
   if (v->irq_session_active && v->role == ROLE_WRITER && *io_u8(v) == STOP_CHAR)
   	sb_stop_all(v);
   ```

6. **Arm vector 33.** `host/src/vm.c`, the `KVM_EXIT_HLT` case:

   ```c
   int rc = sb_wait_turn(v);

   if (rc == SB_STOPPED) {
   	v->irq_session_active = 0;   /* the halt after the handler terminates */
   	v->stop_pending = 1;
   }
   if (rc != SB_OVER) {
   	v->irq_pending = 1;
   	v->run->request_interrupt_window = 1;
   	break;
   }
   v->irq_session_active = 0;
   ```

   and in `KVM_EXIT_IRQ_WINDOW_OPEN`:

   ```c
   inject_irq(v, v->stop_pending ? IRQ_VECTOR_STOP : IRQ_VECTOR)
   ```

7. **Guest handler.** `guest/lib/interrupts.c`: declare
   `extern void guest_stop(void) __attribute__((weak));`, add the handler and
   register it:

   ```c
   static void __attribute__((interrupt))
   irq33_handler(struct interrupt_frame *frame)
   {
   	const char *s;

   	(void)frame;
   	if (guest_stop) {
   		guest_stop();
   		return;
   	}
   	for (s = "stopped by vector 33\n"; *s; ++s)
   		outb(PORT_SERIAL, *s);
   }
   ```

   ```c
   set_idt_gate(IRQ_VECTOR_STOP, irq33_handler);
   ```

   Declare the hook in `guest/inc/irqproto.h` next to the round hooks.

8. **The writer polls stdin after each round.** `guest/tests/irq_writer.c`, after
   `hv_buf_send`:

   ```c
   if (n > 0 && getch() == STOP_CHAR)
   	print("writer: stop requested from stdin\n");
   ```

   Only after a data round; the sentinel round ends the session anyway. The guest
   does nothing else: the host already acted when it served the IN.

9. **Both images say goodbye.** A `guest_stop` in `irq_writer.c` and
   `irq_reader.c` closes the file and prints `writer: stopped by vector 33` or
   `reader: stopped by vector 33`.

10. **stdin in the scripts.** The writer now reads stdin every round, so no scripted
    run of `irq_writer.img` may inherit the terminal: `</dev/null` on the `run`
    helper in `scripts/test_phase_c.sh` and on the three writer steps in
    `scripts/demo_c.sh`. Then add section 9 of the suite and step C6 of the demo
    from the patch.

## Verify

```sh
make
python3 -c "open('input.txt','w').write('X'*200)"    # 4 rounds of 64
printf 'xxk' | ./host/build/hypervisor -m 4 -p 2 -i \
    -g guest/build/irq_writer.img guest/build/irq_reader.img guest/build/irq_reader.img -f input.txt
wc -c vm_1/out.txt vm_2/out.txt
make test
```

Captured output:

```
[vm 2] reader: writing to out.txt
[vm 0] writer: streaming input.txt
[vm 1] reader: writing to out.txt
[vm 0] stop: 'k' on stdin, stopping every VM
[vm 0] writer: stop requested from stdin
[vm 0] writer: stopped by vector 33
[vm 0] KVM_EXIT_HLT
[vm 1] reader: stopped by vector 33
[vm 1] KVM_EXIT_HLT
[vm 2] reader: stopped by vector 33
[vm 2] KVM_EXIT_HLT
192 vm_1/out.txt
192 vm_2/out.txt
```

Three rounds were published before the third stdin byte, so both readers hold
192 bytes, every run. With `printf 'k'` the stop comes after round one and the
files hold 64 bytes. With `</dev/null` the stream completes as before and
`cmp input.txt vm_1/out.txt` is silent. `make test` reports 24 phase C checks; the
suite stayed green over five consecutive runs.

Interactively: run without the pipe and press `k` then Enter after any round.
If you take longer than 5 seconds, the readers' watchdog prints a state dump
every 5 seconds; that is the existing deadlock detector doing its job, not a
fault.

## What to say

- Why the writer thread does not inject into the readers: `KVM_INTERRUPT` must be
  issued by the thread that created the vCPU (design D7). The writer only sets a
  flag and broadcasts; every thread injects into its own vCPU when its window
  opens. That is the same mechanism as a normal round.
- Why a pending round is consumed before stopping: otherwise the outcome depends
  on scheduling and a slow reader loses the last round. With the rule, the demo is
  deterministic.
- Why the halt after the handler terminates: the run loop already reinterprets
  `hlt` by session state (design D5); clearing the session flag before injecting
  33 is enough.
- What if a reader has died: `sb_vm_gone` accounting is unchanged, and
  `writer_ready` no longer waits on readers once stopped, so nothing can block.
- Why only the writer reads stdin: the sheet says so, and stdin is shared by all
  VM threads; one reader keeps the byte stream unambiguous.

**Measured:** 11 files, 157 insertions, 17 deletions. Code: 8 files, about 100
lines; the rest is the suite section, the demo step and the stdin redirections.
Patch: [`patches/modC.diff`](patches/modC.diff).
