# nanovisor

A KVM-based hypervisor for the AOR2 course project. One process runs N guest
VMs, one POSIX thread each, in 64-bit long mode. Guests reach the outside world
through four I/O ports: a serial console, a file ABI, and a hypervisor-owned
shared buffer driven by injected interrupts.

The assignment is in [`docs/PROJECT_en.md`](docs/PROJECT_en.md); the
implementation plan and its progress ledger are in
[`docs/ROADMAP.md`](docs/ROADMAP.md).

## Build and run

```sh
make                 # builds host/build/hypervisor and every guest/build/*.img
make test            # phase 0 gate + phase A, B, C suites
make clean
```

```
usage: hypervisor -g <image> [image...] [options]

  -m, --memory <2|4|8>        guest memory size in MB (default 2)
  -p, --page   <4|2>          page size, 4 for 4KB or 2 for 2MB (default 4)
  -g, --guest  <img> [img...] guest images; one VM is launched per image
  -f, --file   <f> [f...]     files shared between VMs
  -i, --irq                   run the shared-buffer interrupt session
  -w, --writer <id>           VM that writes to the shared buffer (default 0)
  -v, --verbose               trace shared-buffer rounds
  -h, --help                  this message
```

```sh
./host/build/hypervisor --memory 4 --page 2 --guest guest/build/hello.img guest/build/hello.img
./host/build/hypervisor -m 4 -p 2 -g guest/build/file_shared.img -f shared.txt
./host/build/hypervisor -m 4 -p 2 -i -g guest/build/irq_writer.img \
    guest/build/irq_reader.img guest/build/irq_reader.img -f input.txt
```

`-i`, `-w` and `-v` are additions to the assignment's option set. **The phase C
session only starts with `-i`**, which is what keeps every phase A and B demo
running unchanged — without it a `hlt` still terminates the VM immediately (see
D5 below).

## Layout

```
common/hv_abi.h     the guest/host ABI, included verbatim by both sides
host/               the hypervisor
  inc/, src/          opts, vm, fileio, shared_buf, output
guest/
  inc/                headers
  lib/                linked into every image: start, interrupts, irqproto,
                      syscall, print, string
  tests/              one image per file: guest/build/<name>.img
scripts/            test suites and the defense demos
tests/expected/     golden output for the phase 0 regression gate
```

Each `guest/tests/<name>.c` provides `guest_main()` and becomes
`guest/build/<name>.img`. `lib/start.c` owns `_start`.

## Guest memory map

`[0, mem_size)` is identity mapped, so **GVA == GPA**. The region below the
guest is reserved for hypervisor-owned structures:

```
0x000000  unused
0x001000  PML4
0x002000  PDPT
0x003000  PD
0x004000  PT[0]  ┐  4 KB paging only. mem_size/4096 PTEs means
0x005000  PT[1]  │  512 / 1024 / 2048 entries for 2 / 4 / 8 MB,
0x006000  PT[2]  │  so 1 / 2 / 4 tables — exactly what fits below
0x007000  PT[3]  ┘  the guest.
0x008000  guest image (linked at this address), then .bss
   ...    free
mem_size  initial rsp; the stack grows down
```

With 2 MB pages only PML4+PDPT+PD are used, with `PDE64_PS` set on 1 / 2 / 4 PD
entries. The guest recovers `mem_size` from its initial `rsp`, rounded up to the
next 2 MB boundary — no extra port, nothing hardcoded.

## Ports

| Port | Dir | Width | Meaning |
|---|---|---|---|
| `0xE9` | OUT | 1 | serial output: one character to the console |
| `0xE9` | IN | 1 | serial input: one byte of the hypervisor's stdin, 0 at EOF |
| `0x0278` | OUT | 4 | address of a `struct hv_request` in guest memory |
| `0x0278` | IN | 4 | result of the request just submitted |
| `0x0510` | IN | 1 | *first interrupt only*: the assigned mode (0 read, 1 write) |
| `0x0510` | OUT | 4 | writer: how many bytes this round carries |
| `0x0510` | OUT | 1 | writer: one byte of the round |
| `0x0510` | IN | 4 | reader: how many bytes this round carries |
| `0x0510` | IN | 1 | reader: one byte of the round |
| `0x0520` | IN | 4 | writer: how many bytes the hypervisor accepted |
| `0x0520` | OUT | 4 | reader: how many bytes it actually read |

Operand width plus the VM's role disambiguates `0x510`; the direction is
validated against the role, so a reader writing to the buffer is a reported
error rather than silent corruption. Every handler asserts `io.size` and
`io.count` instead of mis-decoding.

## File ABI (phase B)

```c
struct hv_request {              /* identical layout on both sides */
    uint32_t op;                 /* HV_OPEN / HV_CLOSE / HV_READ / HV_WRITE / HV_LSEEK */
    uint32_t arg0, arg1, arg2;
    int32_t  ret;
};
```

The guest builds this on its own stack and hands over the address:

```c
outl(PORT_FILE, (uint32_t)(uintptr_t)&req);
return (int)inl(PORT_FILE);
```

Two vmexits per call. Pointer arguments (`path`, `buf`) are ordinary guest
addresses inside the struct and are translated the same way, so nothing needs
serializing. A 32-bit address suffices because `mem_size` never exceeds 8 MB;
the generalisation is two `outl`s.

The spec's flag values collide with POSIX — spec `O_RDWR` is 4 where POSIX says
2, spec `SEEK_SET` is 1 where POSIX says 0 — so the shared header uses
`HV_`-prefixed names and the host is forced to translate. `guest/inc/syscall.h`
re-exports the unprefixed spec names for guest code only.

Two decisions the spec leaves open: `O_RD|O_WR` is treated as `O_RDWR`, and
`O_CREATE` with no access flag is refused rather than guessing a mode.

## Shared buffer protocol (phase C)

```
writer                    hypervisor                   reader(s)
  |                            |                            |
  |  first IRQ 32 ------------>|<------------ first IRQ 32  |
  |  inb(0x510) -> 1           |           inb(0x510) -> 0  |
  |                            |                            |
  |  [blocked at hlt until readers_pending == 0]            |
  |  outl(0x510, count) ------>|                            |
  |  outb(0x510, byte) x count>|  (bytes past BUFFER_SIZE   |
  |                            |   accepted and discarded)  |
  |  inl(0x520) -> accepted -->|  round++                   |
  |                            |  readers_pending = N       |
  |                            |------------- IRQ 32 ------>|
  |                            |<---- inl(0x510) -> count   |
  |                            |<---- inb(0x510) x count    |
  |                            |<---- outl(0x520, n_read)   |
  |                            |  readers_pending--         |
  |  [woken when it hits 0]    |                            |
```

A monotonic `round` counter, with each VM remembering the last round it
consumed, stops a fast reader from eating the wakeup meant for the next round.
`count == 0` is the end-of-stream sentinel: readers see a zero-length round,
close their output and halt for real.

`BUFFER_SIZE` is 64, deliberately small so a short input still exercises the
multi-round path and the barrier.

## Design decisions

Each of these had a real alternative; the alternative is the interesting half.

**D1 — Identity map, guest at `0x8000`, page tables below it.**
`[0, mem_size)` is identity mapped, so `GVA == GPA` and translating a guest
pointer is `v->mem + gva` plus a bounds check. *Alternative:* the
`KVM_TRANSLATE` ioctl does GVA→GPA properly, but costs one ioctl per pointer
argument and still needs the bounds check. This decision is why phase B's
pointer handling is three lines.

**D2 — One `outl`, one `inl` per file call.** The request struct crosses as an
address, not a serialized message. *Alternative:* marshalling each argument
through the port, which needs a length protocol for `path` and `buf`.

**D3 — Explicit per-file offset, `pread`/`pwrite`, never the host fd cursor.**
`struct guest_file` carries its own `off`. This is what makes copy-on-write
correct: when the host fd is swapped from the shared original to the private
copy, the seek position is in our struct and survives untouched. *Alternative:*
`lseek` on the host fd, which resets on the swap and corrupts silently in an
offset-dependent way.

**D4 — Per-VM directory.** Local names resolve inside `vm_<id>/`; shared names
resolve to the registry from `-f`. The name validator rejects `/` and `..`, so
path traversal is not filtered — it is unrepresentable. Combined with the fd
table being a `struct vm` member, isolation is structural rather than enforced
by scattered checks.

**D5 — `hlt` means "idle, awaiting IRQ" while a session is live.** The spec
wants "terminate on `hlt`" in phase A and "keep handling interrupts" in phase C,
which conflict: with no in-kernel irqchip a halted vCPU exits immediately. The
hypervisor knows whether a session is live, so it reinterprets `hlt` and the
guest needs no signalling. *Alternative:* the guest busy-waits on `pause` with a
`volatile` flag set by the ISR — same coordinator, worse CPU behaviour.

**D6 — `count == 0` is the EOF sentinel.** The protocol as specified has no
termination condition, so readers would block forever once the source is
exhausted. This is a deliberate extension.

**D7 — The vCPU is created inside its own thread.** KVM expects vcpu ioctls from
the thread that issued `KVM_CREATE_VCPU`. Creating it on the main thread and
running it elsewhere is what makes `KVM_INTERRUPT` fail later. Relatedly:
`KVM_CREATE_IRQCHIP` and `KVM_CREATE_PIT2` are never called, because an
in-kernel irqchip would make `KVM_INTERRUPT` fail.

## Concurrency notes

- `sb_wait_turn()` is the only place a VM thread blocks, which keeps the port
  handlers free of lock ordering concerns.
- `request_interrupt_window` is only ever written by a VM's own thread while it
  is outside `KVM_RUN`, so there is no cross-thread race on the mmap'd
  `kvm_run`.
- A 5-second `pthread_cond_timedwait` watchdog names what each VM is waiting
  for. It found the one real deadlock in this project: a reader that died
  between a publish and its own first read left `readers_pending` permanently
  above zero. Teardown now counts as "I am finished with this round".
- Guest console bytes are buffered per VM and flushed a line at a time under one
  mutex, so N guests interleave by line rather than by character.

## Testing

```sh
make test                    # everything
./scripts/regress.sh         # phase 0: byte-identical to the original starter
./scripts/test_phase_a.sh    # 19 checks
./scripts/test_phase_b.sh    # 24 checks
./scripts/test_phase_c.sh    # 20 checks
./scripts/demo_a.sh          # the defense demos, narrated
```

The phase 0 gate diffs against `tests/expected/phase0.txt` and fails on any
compiler warning. The phase C suite is the one worth running repeatedly — it is
what caught the deadlock above. The phase C suite and `demo_c.sh` generate their
input files with `python3`.
