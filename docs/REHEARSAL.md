# Live-modification rehearsal

At the defense you must "successfully perform a modification for the phase being
defended". These are the candidates from the roadmap. Each one below was
actually carried out against this tree, run, and reverted — the file and line
counts are measured, not estimated.

**Time yourself on each.** The counts tell you how big the edit is; they say
nothing about how long it takes you to remember *where* the edit goes, which is
the part worth practising.

| # | Modification | Files | Lines | Verified |
|---|---|---|---|---|
| 1 | Add `16` to the allowed memory sizes | 1 (+2 to make it work with 4 KB pages) | 2 | yes — **has a trap, read below** |
| 2 | Add a syscall end to end (`tell`) | 4 + a test image | 17 | yes |
| 3 | Change `BUFFER_SIZE` | 1 | 1 | yes |
| 4 | Move the CoW trigger to open-with-write-intent | 1 | 7 | yes |
| 5 | Add a `-v/--verbose` option | 4 | ~30 | already in the tree |
| 6 | Add a third role | 4–5 | ~60 | steps only |
| 7 | `rep outsb` instead of byte-at-a-time | 2 | ~25 | steps only |
| 8 | Swap the identity map for `KVM_TRANSLATE` | 1 | ~20 | steps only |

---

## 1. Add `16` to the allowed memory sizes

**The obvious edit,** `host/src/opts.c` in `parse_memory`:

```c
if (v != 2 && v != 4 && v != 8 && v != 16) {
```

and the message next to it. `-m 16 -p 2` then works immediately.

**The trap: `-m 16 -p 4` cannot work without a second change,** and before the
guard was added it failed *silently*. 4 KB paging needs one page table per 512
pages, so 16 MB needs 8 of them at `PT_BASE`, spanning `0x4000–0xBFFF`. The
guest image is loaded at `0x8000` and overwrites the last four. The guest then
triple-faults on the first touch of an address whose table was clobbered —
nowhere near the real cause, and dependent on the image's size.

`mem_probe` reports success at 16 MB regardless, because it touches the top of
memory and `.bss` but never the 8–10 MB range whose table the image overwrites.
Do not trust it here.

`setup_paging_4k` now refuses the setup outright:

```
error: 16 MB with 4 KB pages needs 8 page tables (32768 bytes), but only
16384 bytes are reserved below the guest at 0x8000
```

**To make it genuinely work**, raise the reserved region:

1. `host/inc/vm.h`: `GUEST_START_ADDR` `0x8000` → `0x10000` (room for 11 pages)
2. `guest/guest.ld`: `. = 0x8000;` → `. = 0x10000;`
3. `make && ./host/build/hypervisor -m 16 -p 4 -g guest/build/mem_probe.img`

Both must move together — that is the same coupling as task A.4.

## 2. Add a syscall end to end (`tell`)

Measured: **17 lines across 4 files.** Order matters; the `_Static_assert` in the
ABI header means a one-sided edit fails to compile rather than corrupting a
request.

1. `common/hv_abi.h` — add `HV_TELL = 6` to `enum hv_op`.
2. `guest/inc/syscall.h` — declare `int tell(int fd);`
3. `guest/lib/syscall.c` — `return hv_call(HV_TELL, (uint32_t)fd, 0, 0);`
4. `host/src/fileio.c` — add the operation and a dispatcher case:

```c
static int32_t hv_tell(struct vm *v, int32_t fd)
{
	struct guest_file *f = get_file(v, fd);

	return f ? (int32_t)f->off : -1;
}
```

```c
case HV_TELL:
	req->ret = hv_tell(v, (int32_t)req->arg0);
	break;
```

5. A test image in `guest/tests/`. Verified output: `tell` returns 0 after open,
   10 after writing 10 bytes, 3 after `lseek(fd, 3, SEEK_SET)`.

`unlink` is the same shape, plus `is_valid_name` and the `vm_<id>/` prefix — and
you must decide whether unlinking a shared name is refused (it should be).

## 3. Change `BUFFER_SIZE`

**One line**, `common/hv_abi.h`. Both sides pick it up because the guest sizes
its staging buffer from the same macro.

Verified at 16: a 148-byte input goes from 4 rounds to 11, the transfer stays
byte-exact, and `irq_flood` still reports `sent 32 bytes, hypervisor accepted
16` — it scales itself, since `FLOOD` is `BUFFER_SIZE * 2`.

Show it with `-v` so the extra rounds are visible in the trace.

## 4. Move the CoW trigger to open-with-write-intent

Measured: **7 lines in `host/src/fileio.c`.**

1. Forward-declare `cow_materialize` above `hv_open` (it is defined below).
2. At the end of `hv_open`, after the entry is populated:

```c
if (f->shared && can_write(f) && cow_materialize(v, f) < 0) {
	free_fd(f);
	return -1;
}
```

Leave the check in `hv_write`; it becomes a no-op because `cow_materialize` is
idempotent. Verified: same final contents, and the `cow:` line now appears
*before* the guest's first read instead of after it — which is the visible proof
the trigger moved.

Say why the spec's version is the better default: a guest that opens `O_RDWR`
and only ever reads pays for a full copy under this variant.

## 5. Add `-v/--verbose`

Already implemented, so use it as a worked example rather than a modification:
`opts.h` field, `usage()` line, `longopts` entry, optstring letter, `case`,
and threading the flag to where it is read. Adding `-q/--quiet` is the same
shape in about the same number of lines.

## 6. Add a third role

Not executed. The shape:

1. `enum vm_role` in `host/inc/vm.h` — add `ROLE_PEEKER`.
2. `common/hv_abi.h` — add `HV_MODE_PEEK 2`, since the mode byte is what tells
   the guest which path to run.
3. `host/src/main.c` — assign the new role.
4. `host/src/shared_buf.c` — decide whether the new role counts toward
   `readers_total`. **This is the whole question.** A peeker that does not
   acknowledge must *not* be counted, or the writer blocks forever; the reader
   accounting in `sb_vm_gone` has the same shape and is where the one real
   deadlock in this project lived.
5. `guest/lib/interrupts.c` — dispatch on the new mode.

Expect to be asked what happens if the third role dies mid-round.

## 7. `rep outsb` instead of byte-at-a-time

Not executed. Today every handler asserts `io.count == 1`. With a string
instruction the guest issues one `rep outsb` and KVM reports `io.count > 1` with
`count` items of `io.size` bytes laid out consecutively from `io.data_offset`.

1. `guest/lib/irqproto.c` — replace the byte loop with `rep outsb`/`rep insb`.
2. `host/src/vm.c` — in `handle_buf_port`, loop over `io.count` reading
   consecutive items rather than rejecting `count != 1`.

Mention the payoff: one vmexit per round instead of one per byte.

## 8. Swap the identity map for `KVM_TRANSLATE`

Not executed. Only `guest_ptr` changes:

```c
struct kvm_translation tr = { .linear_address = gva };

if (ioctl(v->vcpu_fd, KVM_TRANSLATE, &tr) < 0 || !tr.valid)
	return NULL;
if (tr.physical_address + len > v->mem_size)   /* bounds check still required */
	return NULL;
return v->mem + tr.physical_address;
```

Two points to make: the bounds check does not go away, and this costs one ioctl
per pointer argument. It also must be issued from the vCPU's owning thread —
which is true here because of D7, and would not have been had the vCPU been
created on the main thread.
