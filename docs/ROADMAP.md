# nanovisor — Development Roadmap

Implementation roadmap for the AOR2 KVM hypervisor project. The assignment itself is in
[`PROJECT_en.md`](PROJECT_en.md) (Serbian original: [`PROJECT_sr.md`](PROJECT_sr.md)).

**How to use this document:** tick tasks as you complete them. Every task carries a point value out of
100. Progress % is a plain sum of completed task points — no nested arithmetic, no per-phase conversion.

---

## Progress ledger

Update as you go.

```
Engineering progress:  33 / 100
Assignment points:     15 / 45
Current phase:         B (File support)
```

| Milestone | Engineering % | Points secured |
|---|---|---|
| Phase 0 complete — safe to write new code | 8% | 0 |
| Phase A complete — assignment A defensible | 33% | 15 |
| Phase B complete — assignment B defensible | 67% | 30 |
| Phase C complete — assignment C defensible | 94% | 45 |
| Phase D complete — defense-ready | 100% | 45 |

The gap between the two columns is real and deliberate. Phase 0 buys zero points and is still the single
highest-value thing you can do first; see [Why Phase 0 exists](#why-phase-0-exists).

---

## Baseline (verified)

The starter is the course's `KVM_Zadatak_4` skeleton. It builds and runs today:

```sh
make -C host && make -C guest
./host/build/hypervisor guest/build/guest.img
# IRQ0 received!  ×3
# Hello, world!
# KVM_EXIT_HLT
```

Environment: `/dev/kvm` present, user in the `kvm` group, AMD SVM host, gcc 15.2.0.

What already works, and is worth *not* rewriting:

- Long mode entry: GDT/segment setup, `CR0`/`CR4`/`EFER`, `KVM_SET_SREGS` (`host/src/vm.c:107-157`).
- Raw guest image loading (`host/src/vm.c:159`).
- `KVM_EXIT_IO` OUT on port `0xE9`, `KVM_EXIT_HLT` (`host/src/main.c:68-97`).
- IRQ injection: `KVM_INTERRUPT` + `request_interrupt_window` + `KVM_EXIT_IRQ_WINDOW_OPEN`
  (`host/src/vm.c:197`, `host/src/main.c:58,75-85`). This is the part students most often lose days on,
  and it is handed to you working.
- A guest-side IDT with a functioning vector-32 handler (`guest/src/interrupts.c`).

What is missing or actively wrong:

- `setup_long_mode()` hardcodes 2 MB / 4 KB pages. It maps GVA `0..0xFFFF` → GPA `0x8000..0x17FFF`
  (16 pages) plus one stack page at `pt[511]`. **Total mapped guest VA space: 68 KB.**
- No option parsing at all — `argv[1]` is the image path.
- Single VM, single thread, no `pthread` usage despite `-lpthread` in the link line.
- Port `0xE9` handles OUT only; the spec requires IN as well.
- `guest/inc/io.h` has `outb` and nothing else.
- `guest/Makefile` links `$(wildcard src/*.c)` into **one** image. There is currently no way to produce
  two different guest images, which Phase A requires by definition.
- `guest/Makefile` lacks `-mno-red-zone` despite an ISR already being installed and firing.
- `guest.img` is 928 bytes but `.bss` alone is 1048 bytes (`idt[64]` = 1 KB + `gdt` = 24 B). `.bss` is
  **not in the image** — it is zeroed only by accident, via `MAP_ANONYMOUS` in `vm_init`.
- `guest.ld` matches `.rodata` but not `.rodata*`, has no origin, and discards nothing.
- No `.gitignore`; `host/build/` and `guest/build/` show as untracked.

---

## Design decisions

Fix these early; do not revisit. Each is a likely defense question — know the alternative you rejected.

### D1 — Identity map, guest at `0x8000`, page tables below it

Reserve `[0x0000, 0x8000)` for hypervisor-owned structures, identity-map `[0, mem_size)`, load the guest
at GPA `0x8000`, set `rip = 0x8000` and `rsp = mem_size`.

The 4 KB case fits the reserved region exactly:

```
0x1000  PML4    (1 page)
0x2000  PDPT    (1 page)
0x3000  PD      (1 page)
0x4000  PT[0]  ┐
0x5000  PT[1]  │  mem_size/4096 PTEs = 512 / 1024 / 2048 for 2 / 4 / 8 MB
0x6000  PT[2]  │  → 1 / 2 / 4 page tables
0x7000  PT[3]  ┘
0x8000  guest image, then .bss, then stack growing down from mem_size
```

The 2 MB case needs only PML4 + PDPT + PD (3 pages), with `PDE64_PS` set on 1 / 2 / 4 PD entries.

**Consequence: `GVA == GPA`,** so the entire Phase B pointer-translation problem collapses to
`v->mem + gva` plus a bounds check. This is the highest-leverage decision in the project.

*Alternative (keep as a defense answer):* the `KVM_TRANSLATE` ioctl (`0x85`, `struct kvm_translation`,
confirmed present in this kernel's headers) does GVA→GPA properly. It costs one ioctl per pointer
argument and still needs the bounds check.

Requires `. = 0x8000;` in `guest/guest.ld`. Under `OUTPUT_FORMAT(binary)` the emitted file's offset 0
corresponds to the lowest section VMA, so `load_guest_image(v, path, 0x8000)` stays correct.

### D2 — Phase B ABI: one `outl`, one `inl`

The guest builds a request struct on its own stack and hands over its address:

```c
struct hv_request {              /* identical layout on both sides */
    uint32_t op;                 /* HV_OPEN / HV_CLOSE / HV_READ / HV_WRITE / HV_LSEEK */
    uint32_t arg0, arg1, arg2;   /* fd / guest pointer / count / flags */
    int32_t  ret;
};
```

Guest: `outl(0x0278, (uint32_t)(uintptr_t)&req);` then `return (int)inl(0x0278);`

Host: on `KVM_EXIT_IO_OUT`, port `0x278`, `io.size == 4` → read the u32 at
`((char *)run) + run->io.data_offset` → that is the GPA → bounds-check → `(struct hv_request *)(v->mem + gpa)`
→ dispatch → store `req->ret` and stash it for the following IN.

Guest addresses always fit in 32 bits because `mem_size <= 8 MB`. This uses IN *and* OUT on `0x0278` as
the spec requires, costs exactly two vmexits per syscall, and needs no serialization of pointer
arguments — `path` and `buf` are just more guest addresses inside the struct, translated identically.

*Generalization to mention at defense:* two `outl`s for a full 64-bit address.

### D3 — Explicit per-file offset, `pread`/`pwrite`, never the host fd cursor

`struct guest_file` carries its own `long off`; all I/O goes through `pread`/`pwrite`. This makes copy-on-write's
hardest sub-problem — preserving the seek position when the underlying host fd is swapped from the shared
original to the VM-local copy — disappear entirely. The offset lives in your struct, not the kernel's.

### D4 — Per-VM directory for local files

`vm_<id>/` is created at VM startup. Local names resolve inside it; shared names resolve to the CWD and
open `O_RDONLY` until CoW materializes `vm_<id>/<name>`. Isolation falls out of the namespace rather than
being enforced by scattered checks. The filename validator (`[A-Za-z][A-Za-z0-9.]*`) rejects `/` and `..`,
so path traversal is structurally impossible — say that out loud at defense.

### D5 — `hlt` means "idle, awaiting IRQ" while an interrupt session is live

The spec's "terminate on `hlt`" (phase A) and "keep handling subsequent interrupts" (phase C) are in
direct conflict: with no in-kernel irqchip, a halted vCPU produces `KVM_EXIT_HLT` immediately. The
hypervisor knows the session state, so no guest-side signalling is needed:

```
KVM_EXIT_HLT:
    if (vm->irq_session_active)
        block on the per-VM condvar until the coordinator schedules this VM's next
        round, then set run->request_interrupt_window = 1 and resume;
        inject on the ensuing KVM_EXIT_IRQ_WINDOW_OPEN
    else
        terminate this VM          /* phase A semantics */
```

`irq_session_active` is cleared when the hypervisor delivers the `count == 0` sentinel round.
`request_interrupt_window` is only ever written by the VM's own thread while it is *outside* `KVM_RUN`,
so there is no cross-thread race on the mmap'd `kvm_run`.

*Alternative:* the guest busy-waits on `pause` with a `volatile` flag set by the ISR — same coordinator,
no reinterpretation of `hlt`, worse CPU behaviour.

### D6 — `count == 0` is the EOF sentinel

The protocol as specified has no termination condition, so readers block forever once the writer's source
file is exhausted. When the writer's guest gets 0 bytes from its file it sends `count = 0`; the hypervisor
broadcasts a zero-length round; readers read count 0, write 0 to `0x520`, return from the ISR, close their
output file and `hlt` — now a real terminate. This is a deliberate protocol extension; be ready to defend it.

### D7 — Create the vCPU inside its own thread

KVM expects vcpu ioctls to be issued from the thread that created the vCPU. `vm_init()` currently runs
`KVM_CREATE_VCPU` on the main thread. Move **all** of `vm_init` into `vm_thread`; the main thread only
parses options and spawns.

---

## Phase overview

| Phase | Name | Maps to | Weight | Done when |
|---|---|---|---|---|
| [0](#phase-0--foundations--hygiene-8-pts) | Foundations & hygiene | enabler | **8** | Refactored program reproduces starter output exactly |
| [A](#phase-a--basic-hypervisor-25-pts) | Basic hypervisor | Assignment A (15 pts) | **25** | `--memory 4 --page 2 --guest g1.img g2.img` works; bad options rejected |
| [B](#phase-b--file-support-34-pts) | File support | Assignment B (15 pts) | **34** | `open`/`close`/`read`/`write`/`lseek` + CoW on shared files |
| [C](#phase-c--interrupt-support-27-pts) | Interrupt support | Assignment C (15 pts) | **27** | Writer VM streams a file through the shared buffer to N readers |
| [D](#phase-d--defense-readiness-6-pts) | Defense readiness | Defense requirement | **6** | 3 demos per phase scripted, live modification rehearsed |

### Why the weights are not 15/15/15

The assignment's even point split is a *grading* weight, not a *work* weight. Using it for a progress bar
would put you at "66% done" with the hardest third of the work entirely ahead of you — exactly the failure
a tracker exists to prevent. Effort is genuinely uneven:

- **Phase A is the cheapest of the three.** The starter already does long mode, segment setup, image
  loading and the exit dispatch loop. What remains is parameterization, argv handling and a thread
  wrapper. ≈8 h.
- **Phase B is the most expensive.** It is the only phase requiring a new ABI designed from nothing, code
  on *both* sides of the boundary, a stateful per-VM resource table, a filesystem feature (CoW) with a
  non-obvious correctness condition, and an isolation property. ≈11 h.
- **Phase C is expensive but subsidized.** Interrupt injection and the ISR already work in the starter.
  What remains is concurrency design and resolving two underspecified points (D5, D6). ≈9 h.
- **Phase 0** is ≈3 h of pure risk reduction. Weighting it 0 would create an incentive to skip it, which
  is the worst decision available in this project.
- **Phase D** is ≈2 h and is directly graded by the defense format.

| Phase | Points-based | Effort-based | **Adopted** |
|---|---|---|---|
| 0 | 0 | 9 | **8** |
| A | 33.3 | 24 | **25** |
| B | 33.3 | 33 | **34** |
| C | 33.3 | 27 | **27** |
| D | 0 | 6 | **6** |
| | 100 | 100 | **100** |

A phase counts as complete only when its **verification commands pass**, not when its last task is written.

---

## Phase 0 — Foundations & hygiene (8 pts)

### Why Phase 0 exists

1. **`-mno-red-zone` is a correctness prerequisite, not hygiene.** The SysV AMD64 ABI reserves 128 bytes
   below `rsp` for leaf functions. Hardware interrupt delivery pushes SS:RSP:RFLAGS:CS:RIP *at* `rsp`,
   straight through the red zone. `irq0_handler` already exists and already fires — it survives today only
   because GCC happens not to use the red zone in the functions involved. The moment the ISR calls into a
   shim with local buffers (Phase C), you get silent nondeterministic stack corruption. Fix it before
   writing a single line of new guest code.
2. **The identity-map decision is irreversible-by-cost.** Every pointer crossing the guest→host boundary
   in B and C is translated by whatever rule you pick. Picking it after writing Phase B means rewriting
   Phase B.
3. **`main.c` cannot host N threads in its current shape.** The refactor is mechanical, and it has a
   known-good regression target. Doing it while the program is still single-VM and provably working is far
   cheaper than doing it simultaneously with the pthread work.

### Tasks

- [x] **0.1** — `.gitignore`: `build/`, `*.o`, `*.d`, `*.img`, `vm_*/`, generated test data — **0.5 pts**
      · `.gitignore` (new)
- [x] **0.2** — Guest compiler flags: `-mno-red-zone -fno-stack-protector -mgeneral-regs-only -fno-pie
      -fno-asynchronous-unwind-tables -fcf-protection=none -O2 -Wall -Wextra -fno-builtin
      -fno-tree-loop-distribute-patterns`;
      drop the now-redundant `target("general-regs-only")` attribute — **1.5 pts**
      · `guest/Makefile:19`, `guest/src/interrupts.c:11`
- [x] **0.3** — Linker script: `*(.rodata*)`, `*(.data*)`, `__bss_start`/`__bss_end`,
      `/DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }` — **1.5 pts** · `guest/guest.ld`
      <br>**Must land together with 0.2.** At `-O0` GCC emits `.rodata`; at `-O2` it emits
      `.rodata.str1.1`, which `*(.rodata)` does not match. Adding `-O2` alone orphans every string literal.
      <br>**`. = 0x8000;` deferred to A.4.** It cannot land in phase 0: `setup_long_mode` still maps GVA
      `0..0xFFFF` → GPA `0x8000..0x17FFF` and `rip` is still `0`, so a guest linked at `0x8000` would
      resolve to GPA `0x10000`, past the image. The origin flips together with the identity map.
- [x] **0.4** — Zero `.bss` at the top of `_start` using the new symbols; stop relying on `MAP_ANONYMOUS`
      — **0.5 pts** · `guest/src/main.c`
- [x] **0.5** — Freestanding runtime: `inb`/`outb`/`inw`/`outw`/`inl`/`outl`, plus
      `memset`/`memcpy`/`memmove`/`strlen` (GCC emits calls to these even under `-ffreestanding`)
      — **1.0 pts** · `guest/inc/io.h`, `guest/src/string.c` + `guest/inc/string.h` (new)
- [x] **0.6** — Host refactor: `struct vm_config { size_t mem_size; int page_size; const char *image; int id; }`,
      `vm_setup(struct vm *, const struct vm_config *)`, `int vm_run(struct vm *)`; move the dispatch loop
      out of `main` — **2.0 pts** · `host/inc/vm.h`, `host/src/vm.c`, `host/src/main.c`
- [x] **0.7** — Host Makefile: `-pthread` in CFLAGS (not just at link), `-g -O2`, `-D_GNU_SOURCE`
      — **0.5 pts** · `host/Makefile`
- [x] **0.8** — Regression gate — **0.5 pts**

### Verification

```sh
./scripts/regress.sh          # clean build of both trees, warning check, output diff
```

Output must be **byte-identical** to the pre-refactor run: three `IRQ0 received!` lines, `Hello, world!`,
`KVM_EXIT_HLT`. The reference copy is committed at `tests/expected/phase0.txt`. **Passing.**

---

## Phase A — Basic hypervisor (25 pts)

Covers `PROJECT_en.md` lines 42–50 and the "Option validation" section.

### Tasks

- [x] **A.1** — Option parser: `struct hv_options { int mem_mb; int page_kb; char **guests; int n_guests;
      char **files; int n_files; }`, `parse_options()`, and the variadic collector
      `collect_list(int argc, char **argv, int *idx, char ***out, int *n)` that consumes `argv[optind]`
      while it does not begin with `-` — **4 pts** · `host/inc/opts.h`, `host/src/opts.c` (new)
      <br>`getopt_long` gives one `optarg`; the spec's `-g a.img b.img` needs more. Consume the extra
      operands *inside* the `case 'g'`/`case 'f'` handler so getopt never scans or permutes them.
      Reused verbatim by `-f` in Phase B.
- [x] **A.2** — Validation: `-m ∈ {2,4,8}`, `-p ∈ {4,2}` (accept `4`/`4KB`/`2`/`2MB`), `n_guests ≥ 1`,
      unknown option, missing argument → message on stderr + `exit(1)` — **2 pts** · `host/src/opts.c`
      <br>Graded in *every* phase, not just A.
- [x] **A.3** — Parametric paging: replace `setup_long_mode` with `setup_paging_4k()` and
      `setup_paging_2m()`; identity-map `[0, mem_size)` per D1 — **5 pts** · `host/src/vm.c:128`, `host/inc/vm.h`
- [x] **A.4** — Relocate guest to `0x8000`: add `. = 0x8000;` to `guest.ld` (deferred here from 0.3),
      `rip = GUEST_START_ADDR`, `rsp = mem_size`; drop
      `GUEST_CODE_PAGES` and the `pt[511]` stack hack — **2 pts**
      · `host/src/vm.c`, `host/src/main.c:47-50`, `guest/guest.ld`
- [x] **A.5** — Port `0xE9` **IN**: host feeds a byte into `((char *)run) + run->io.data_offset`; guest
      gains `inb(0xE9)` — **2 pts** · `host/src/vm.c`, `guest/inc/io.h`
- [x] **A.6** — `struct vm` gains `int id` and `struct vm_config cfg`; `vm_init` mmaps `cfg.mem_size`
      — **1 pt** · `host/inc/vm.h`, `host/src/vm.c:11`
      <br>Landed early: `struct vm_config` arrived with 0.6 and `vm_init` began mmapping `cfg.mem_size`
      with A.3. No separate commit.
- [x] **A.7** — One pthread per guest: `void *vm_thread(void *)` performing the full
      `vm_init` → `vm_setup` → `vm_run` → `vm_destroy` cycle (per D7); `main` spawns `n_guests` threads and
      joins, aggregating statuses — **4 pts** · `host/src/main.c`
- [x] **A.8** — Exit-reason handling: `const char *kvm_exit_name(uint32_t)`; on an unexpected exit print
      `[vm N] unexpected exit: <name> (code)` plus a `KVM_GET_REGS`/`KVM_GET_SREGS` dump; return from that
      thread only, siblings unaffected — **3 pts** · `host/src/vm.c`
      <br>For `KVM_EXIT_FAIL_ENTRY` / `KVM_EXIT_INTERNAL_ERROR` also print
      `run->fail_entry.hardware_entry_failure_reason` / `run->internal.suberror`.
- [x] **A.9** — Output serialization: per-VM line buffer flushed under a global stdout mutex, `[vmN]`
      prefix — **1 pt** · `host/src/vm.c`, `host/src/main.c`
- [x] **A.10** — Guest build restructure: shared objects in `guest/lib/`, one image per
      `guest/tests/<name>.c` → `guest/build/<name>.img` — **1 pt** · `guest/Makefile`
      <br>`lib/start.c` owns `_start` and calls each image's `guest_main()`. It also records
      `guest_mem_top` from the initial `rsp` (the host sets `rsp = mem_size`), so a guest knows how much
      memory it has without a new port — `mem_probe` relies on this.

### Verification — 3 demos

Scripted end to end in `scripts/test_phase_a.sh` (19 checks). **Passing.**

**Test images:** `guest/tests/hello.c`, `guest/tests/mem_probe.c`, `guest/tests/crash.c`,
`guest/tests/echo.c`

1. **Multiple VMs.**
   ```sh
   ./host/build/hypervisor -m 4 -p 2 -g guest/build/hello.img guest/build/hello.img
   ```
   Two `[vm0]`/`[vm1]` outputs, interleaved but not garbled; two `KVM_EXIT_HLT`.
2. **Parametric paging.** `mem_probe.img` writes at `mem_size - 8` and near the image, then reports
   success. Run all six combinations:
   ```sh
   for m in 2 4 8; do for p in 4 2; do
     ./host/build/hypervisor -m $m -p $p -g guest/build/mem_probe.img
   done; done
   ```
3. **Option validation.** `-m 3` → `error: --memory must be 2, 4 or 8 (got 3)`, exit 1. `-p 8` likewise.
   `-g` with no files → error. Also covered: unknown option, missing argument, stray operand.
4. **Fault isolation.**
   ```sh
   ./host/build/hypervisor -m 4 -p 4 -g guest/build/crash.img guest/build/hello.img
   ```
   vm0 executes `ud2` (or touches an unmapped GVA), prints its exit reason and dies; vm1 finishes normally.

---

## Phase B — File support (34 pts)

Covers `PROJECT_en.md` lines 62–128.

### Tasks

- [ ] **B.1** — Shared ABI header: `struct hv_request`, op enum, `O_RD=1`/`O_WR=2`/`O_RDWR=4`/`O_CREATE=8`,
      `SEEK_SET=1`/`SEEK_END=2`, `PORT_FILE 0x0278`; included verbatim by both sides with a
      `_Static_assert` on `sizeof` so a one-sided edit fails at compile time — **2 pts**
      · `common/hv_abi.h` (new)
      <br>The flag values are **not** POSIX. Translate them; never pass them through to `open(2)`.
- [ ] **B.2** — Guest shim: `open`/`close`/`read`/`write`/`lseek` building `struct hv_request` on the
      stack, then `outl(PORT_FILE, addr)` + `inl(PORT_FILE)` — **4 pts**
      · `guest/lib/syscall.c`, `guest/inc/syscall.h` (new)
- [ ] **B.3** — Host translation: `void *guest_ptr(struct vm *v, uint64_t gva, size_t len)` returning NULL
      on `gva + len > mem_size` or on overflow — **2 pts** · `host/src/vm.c`
- [ ] **B.4** — Host dispatcher: `int hv_file_request(struct vm *, uint32_t req_gpa)` decoding the op and
      writing `req->ret`; wired into `vm_run`'s `KVM_EXIT_IO` case with a per-VM `last_ret` for the
      matching IN — **3 pts** · `host/inc/fileio.h`, `host/src/fileio.c` (new)
- [ ] **B.5** — Per-VM fd table: `struct guest_file { char name[64]; int hostfd; long off; int flags;
      int shared; int cow; char host_path[256]; }`, `table[MAX_OPEN_FILES]`, `alloc_fd`/`get_file`/`free_fd`.
      The table is a `struct vm` member, so isolation is structural — **3 pts**
      · `host/inc/fileio.h`, `host/inc/vm.h`
      <br>Guest fds are virtual indices. A guest handing you an fd it never opened must get `-1`.
- [ ] **B.6** — `int is_valid_name(const char *)`: first char `[A-Za-z]`, rest `[A-Za-z0-9.]`, non-empty,
      length-bounded — **1 pt** · `host/src/fileio.c`
- [ ] **B.7** — `hv_open`: validate name → resolve shared registry, else `vm_<id>/<name>` → map custom
      flags to host `O_*` → `off = 0` → return fd or `-1`; `mkdir vm_<id>` at VM start — **4 pts**
      · `host/src/fileio.c`, `host/src/vm.c`
      <br>Decide and document: `O_CREATE` without a write flag, and `O_RD|O_WR` vs `O_RDWR`.
- [ ] **B.8** — `hv_read`/`hv_write`/`hv_lseek`/`hv_close` via `pread`/`pwrite` + `f->off` (D3);
      `SEEK_END` ignores `offset` per spec; permission checks against the open flags; short reads returned
      honestly — **4 pts** · `host/src/fileio.c`
- [ ] **B.9** — Shared-file registry from `-f`: global read-only list, name→path resolution *before* the
      local directory, refuse `O_CREATE` on a shared name — **3 pts** · `host/src/opts.c`, `host/src/fileio.c`
- [ ] **B.10** — Copy-on-write: `int cow_materialize(struct vm *, struct guest_file *)` — copy the shared
      original to `vm_<id>/<name>`, reopen RW, **preserve `f->off`**, set `cow = 1`. Called from `hv_write`
      only, idempotent; the original must be provably untouched — **4 pts** · `host/src/fileio.c`
- [ ] **B.11** — Test images — **3 pts** · `guest/tests/file_basic.c`, `file_errors.c`, `file_shared.c`
- [ ] **B.12** — Teardown: close all fds in `vm_destroy`; leave `vm_<id>/` on disk for inspection
      — **1 pt** · `host/src/vm.c`, `host/src/fileio.c`

### Verification — 3 demos

1. **Round trip.** `file_basic.c`: `open("out.txt", O_RDWR|O_CREATE)` → `write` → `lseek(fd, 0, SEEK_SET)`
   → `read` back → echo to `0xE9` → `close`. Confirm host-side with `cat vm_0/out.txt`.
2. **Error matrix.** `file_errors.c`: `open("1bad.txt")` → -1 (starts with a digit); `open("a/b.txt")` → -1
   (contains `/`); `open("nope.txt", O_RD)` → -1 (missing, no `O_CREATE`); `read` on a closed fd → -1;
   `lseek` with an invalid flag → -1. Each result printed.
3. **Copy-on-write.**
   ```sh
   cp shared.txt shared.txt.orig
   ./host/build/hypervisor -m 4 -p 2 -g guest/build/file_shared.img guest/build/file_shared.img -f shared.txt
   cmp shared.txt shared.txt.orig          # original untouched
   cmp vm_0/shared.txt vm_1/shared.txt     # must DIFFER — per-VM copies
   ```
   Add a variant that `lseek`s to offset N and *then* writes, proving offset preservation across the CoW swap.

---

## Phase C — Interrupt support (27 pts)

Covers `PROJECT_en.md` lines 140–160.

### Tasks

- [ ] **C.1** — `#define BUFFER_SIZE` + `struct shared_buf { pthread_mutex_t m; pthread_cond_t cv;
      uint8_t data[BUFFER_SIZE]; uint32_t len; uint64_t round; int readers_total, readers_pending; int eof; }`
      plus init/destroy — **2 pts** · `host/inc/shared_buf.h`, `host/src/shared_buf.c` (new)
- [ ] **C.2** — Role assignment: writer = VM 0 (or an explicit option), readers = the rest; first IRQ 32
      injected at session start; `vm->role`, `vm->irq_session_active` — **2 pts**
      · `host/src/main.c`, `host/inc/vm.h`
- [ ] **C.3** — Guest ISR rewrite: `static volatile int mode = -1;` — first entry reads the mode via
      `inb(0x510)` and returns; subsequent entries dispatch to the reader or writer path — **3 pts**
      · `guest/src/interrupts.c`
- [ ] **C.4** — Guest writer path: `outl(0x510, count)`, then `count` × `outb(0x510, byte)`, then
      `accepted = inl(0x520)` — **2 pts** · `guest/lib/irqproto.c` (new)
- [ ] **C.5** — Guest reader path: `count = inl(0x510)`, then `count` × `inb(0x510)`, then
      `outl(0x520, n_read)`; halt the VM if `n_read != count`, per spec — **2 pts** · `guest/lib/irqproto.c`
- [ ] **C.6** — Host `0x510` per-VM state machine `{ EXPECT_COUNT, STREAMING }` + index; direction
      validated against `vm->role`; bytes past `BUFFER_SIZE` accepted from the guest and discarded (spec:
      "the hypervisor ignores the excess") — **3 pts** · `host/src/shared_buf.c`, `host/src/vm.c`
- [ ] **C.7** — Host `0x520`: the writer's IN returns the accepted count; a reader's OUT records bytes
      read and terminates that VM if `< len` — **2 pts** · `host/src/shared_buf.c`
- [ ] **C.8** — Barrier: the writer blocks until `readers_pending == 0`; each reader decrements and
      broadcasts; a monotonic `round` counter prevents a fast reader from consuming the next round's
      wakeup — **3 pts** · `host/src/shared_buf.c`
- [ ] **C.9** — Injection coordinator per D5, with a `pthread_cond_timedwait` watchdog (5 s) that dumps
      every VM's state on timeout — **3 pts** · `host/src/vm.c`
      <br>The watchdog alone will save hours; a hung VM is otherwise invisible.
- [ ] **C.10** — Termination: `count == 0` sentinel → zero-length round → readers finish →
      `irq_session_active = 0` everywhere → next `hlt` terminates. Plus reader-death cleanup: `vm_destroy`
      decrements `readers_total`/`readers_pending` under the mutex and broadcasts, so a crashed reader
      cannot deadlock the writer — **3 pts** · `host/src/shared_buf.c`, `host/src/vm.c`
- [ ] **C.11** — Test images — **2 pts** · `guest/tests/irq_writer.c`, `irq_reader.c`, `irq_probe.c`

### Verification — 3 demos

1. **Mode assignment.** Three `irq_probe.img` guests that only print their assigned mode → exactly one
   `1`, two `0`s.
2. **Single round, ordering proven.** Writer sends a known 16-byte string; two readers echo it to `0xE9`.
   Show the writer does not advance to round 2 before both readers report on `0x520`.
3. **Full spec scenario.**
   ```sh
   ./host/build/hypervisor -m 4 -p 2 \
     -g guest/build/irq_writer.img guest/build/irq_reader.img guest/build/irq_reader.img \
     -f input.txt
   cmp input.txt vm_1/out.txt && cmp input.txt vm_2/out.txt
   ```
   Use an `input.txt` **larger** than `BUFFER_SIZE` so multi-round transfer, the barrier and EOF are all
   exercised. Also run a smaller-than-buffer variant, and one where the writer deliberately sends
   `count > BUFFER_SIZE` to demonstrate excess-byte discard.

---

## Phase D — Defense readiness (6 pts)

- [ ] **D.1** — `scripts/run_tests.sh` (or `make test`) running all ~10 demos and diffing against expected
      output — **1.5 pts**
- [ ] **D.2** — `scripts/demo_a.sh`, `demo_b.sh`, `demo_c.sh` — the exact commands you will type at the
      defense, in order — **1.5 pts**
- [ ] **D.3** — `README.md`: memory map diagram, port/ABI table, protocol state machines, and a "design
      decisions and their alternatives" section covering D1–D7 — **1.5 pts**
- [ ] **D.4** — Live-modification rehearsal: time yourself on each candidate below — **1.5 pts**

### Candidate live modifications to rehearse

These are what examiners actually ask:

- Add a `-v/--verbose` option, or add `16` to the allowed memory sizes.
- Add a syscall end-to-end (`unlink`, or `tell`): ABI enum → guest shim → host dispatcher.
- Change `BUFFER_SIZE` and show the excess-discard path still works.
- Add a third role (a second writer, or a reader that only peeks).
- Move the CoW trigger from first-write to open-with-write-intent.
- Switch the shared-buffer transfer from byte-at-a-time to `rep outsb` (handle `run->io.count > 1`).
- Swap the identity map for `KVM_TRANSLATE`-based translation.

---

## Risk register

Ordered by expected time burn.

| # | Risk | Symptom | Mitigation |
|---|---|---|---|
| 1 | **Red-zone corruption.** `guest/Makefile` lacks `-mno-red-zone` and an ISR already exists. | Nondeterministic wrong locals; garbage return address → `#GP`/`#PF` → triple fault → `KVM_EXIT_SHUTDOWN` with a nonsensical `rip`. Only manifests when the IRQ lands in a leaf function, so it looks timing-dependent. | Task 0.2, **before any new guest code**. |
| 2 | **Phase C `hlt` deadlock / injection cadence.** Guest halts awaiting IRQ N+1; hypervisor waits for the guest. | Everything hangs after the first ISR; threads sit in `futex_wait` or blocked in `ioctl`. | D5 + task C.9: the coordinator owns cadence, not the guest. Add the `pthread_cond_timedwait` watchdog. |
| 3 | **Barrier deadlock when a reader dies mid-round.** | Writer hangs forever; one reader already printed an exit reason. | Task C.10: `vm_destroy` decrements both counters under the mutex and broadcasts. Teardown is an implicit "I finished this round". |
| 4 | **CoW offset loss.** Guest seeks to 500 and writes; the copy resets position to 0 or EOF. | Corrupted output, off-by-N data, silently wrong. | D3: `long off` + `pread`/`pwrite` exclusively. Never consult the host fd cursor. Phase B demo 3 covers it. |
| 5 | **Identity-map decision deferred.** Phase B written against `GVA = GPA - 0x8000`, then changed. | Every `guest_ptr` call site and the linker script change at once. | Lock D1 in A.3/A.4, before B.1. |
| 6 | **`KVM_INTERRUPT` vs in-kernel irqchip.** Calling `KVM_CREATE_IRQCHIP` (tempting when reading LAPIC docs) makes `KVM_INTERRUPT` fail. | `perror("KVM_INTERRUPT")` at `host/src/vm.c:202` fires immediately. | Never call `KVM_CREATE_IRQCHIP`/`KVM_CREATE_PIT2`. Comment the dependency at `inject_irq`. Create the vCPU in its own thread (D7). |
| 7 | **Guest stack/`.bss` outside the mapped region.** Only 68 KB of GVA is mapped today; a `BUFFER_SIZE` buffer walks off the end. | Immediate triple fault on first touch. | A.3 identity-maps all of `[0, mem_size)`. Assert in `load_guest_image` that image size + slack < `mem_size - 0x8000`. |
| 8 | **`getopt_long` permutation vs variadic lists** (`-g a b -f c d`). | The second image silently vanishes, or `-f` absorbs `-g`'s operands. | Consume `argv[optind]` inside the `case` handler before returning to `getopt_long`. If fragile, hand-roll a 60-line parser — this is a coursework CLI, not a product. |
| 9 | **`.rodata.str1.1` orphaned at `-O2`.** `guest.ld` matches `.rodata`, not `.rodata*`. | String literals land at a surprising address (possibly after `.bss`); guest prints garbage. | Tasks 0.2 and 0.3 **together**. Verify by linking to ELF first and inspecting with `readelf -S`, then `objcopy -O binary`. |
| 10 | **`run->io` mis-decoding.** `data_offset` is a byte offset into the `kvm_run` page; `size` is operand width; `count` is the string-op repeat count. | Reading 1 byte where 4 were written; losing bytes when `count > 1`. | Helpers `io_read_u32`/`io_write_u32`/`io_read_u8`/`io_write_u8`, each asserting `io.size` and `io.count`. Assert loudly rather than mis-decode silently. |
| 11 | **Interleaved `printf` from N threads.** | Guest output shuffled mid-line; demos look broken when they are correct. | Task A.9: per-VM line buffer, one mutex, `[vmN]` prefix. |
| 12 | **`-m 2 -p 2` boundary.** Exactly one 2 MB PD entry; `rsp = 0x200000` is one past the top. | First `push` faults — or appears to. | The first push writes to `rsp - 8 = 0x1FFFF8`, which *is* mapped. Correct, but assert it and include this combination in the six-way paging matrix. |

---

## Dependencies

### Hard blockers

```
0.2 (-mno-red-zone) ──► every subsequent guest task
0.2 + 0.3 (must land together: -O2 splits .rodata)
0.3 (linker origin) ──► A.4 ──► B.3 ──► all of Phase B
A.3 (identity map)  ──► B.3 (guest_ptr) ──► B.2, B.4, and all of C's port handlers
0.6 (vm_run split)  ──► A.7 (pthreads) ──► C.9 (per-VM condvar coordinator)
A.7 + D7 (vcpu created in its own thread) ──► C.9 (KVM_INTERRUPT from the owning thread)
B.1 (ABI header)    ──► B.2 and B.4 (the _Static_assert makes a one-sided edit fail to compile)
A.10 (multi-image build) ──► every demo in A, B and C
C.1 (shared_buf)    ──► C.6, C.7, C.8, C.10
```

### Safely deferrable

- **A.5 (port `0xE9` IN)** — required by the spec, trivially added, rarely exercised by a demo. Do it late
  in Phase A.
- **A.9 (output serialization)** — cosmetic until you run 3+ VMs. Do it just before the Phase A demos.
- **B.9 (shared registry)** — `-f` parsing comes free with A.1; the *resolution* logic can wait until B.7
  is stable.
- **B.10 (CoW)** — land B.7/B.8 first and prove local files work end to end; CoW is a delta on a working
  `hv_write`.
- **B.11 / C.11 (test images)** — write throwaway inline versions to unblock development; polish into the
  demo set during Phase D.
- **`BUFFER_SIZE` tuning** — start at 64 so the multi-round path is exercised on tiny inputs. A large
  initial value hides exactly the barrier bugs you most need to find.

### Ordering warning

Do not start C.3–C.5 (the guest-side ISR protocol) before C.6–C.7 (the host-side state machine) at least
compile and log. Debugging a guest-side protocol bug with no host-side visibility means debugging inside a
VM with no debugger. Always build the observable side first.

### Commit convention

One commit per task, message prefixed with the task ID (`0.2:`, `A.3:`, `B.10:`). At the defense you can
run `git log --oneline` and walk the examiner through the build order — which directly serves the
"be able to answer questions about implementation details" requirement.
