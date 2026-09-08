# Index: every document and file, and when to open it

Paths are relative to the repository root. Start with the reading order, then use
the tables to jump.

## Reading order for the defense

1. [`README.md`](../README.md): what the hypervisor is, how to build and run it,
   the memory map, the ports, the two protocols, design decisions D1 to D7.
2. [`docs/guide/RUNNING.md`](guide/RUNNING.md): setup checks, the build, every
   script and every guest image, how to read a failure.
3. [`docs/REHEARSAL.md`](REHEARSAL.md): eight rehearsed live modifications with
   measured sizes, including the 16 MB trap.
4. [`docs/guide/README.md`](guide/README.md): the three official modifications,
   one page each with a verified patch, plus the examiners' base tests.
5. [`docs/ROADMAP.md`](ROADMAP.md), the "Design decisions" and "Risk register"
   sections: the alternatives that were rejected and the bugs that actually
   happened. That is where defense questions come from.

## Documents

| File | What it is | Open it when |
|---|---|---|
| [`README.md`](../README.md) | project overview: usage, layout, memory map, ports table, file ABI, shared-buffer protocol, D1 to D7, concurrency notes, testing | you need the shape of anything, or a design decision in one paragraph |
| [`docs/PROJECT_en.md`](PROJECT_en.md) | the assignment, translated | checking what the spec literally requires |
| [`docs/PROJECT_sr.md`](PROJECT_sr.md) | the assignment, Serbian original | same, in the examiner's words |
| [`docs/AOR2_2026_Projekat.pdf`](AOR2_2026_Projekat.pdf) | the assignment as published | the PDF itself is asked for |
| [`docs/ROADMAP.md`](ROADMAP.md) | the implementation plan: progress ledger, baseline analysis of the starter, D1 to D7 with alternatives, task lists per phase with points, verification demos, risk register, dependency graph, commit convention | "why was it done this way", "what went wrong and how was it found", the task-ID commit prefixes |
| [`docs/REHEARSAL.md`](REHEARSAL.md) | eight candidate live modifications, four carried out and measured, four as steps | practising a modification; the 16 MB trap and the `tell` syscall walk-through |
| [`docs/guide/README.md`](guide/README.md) | index of the modification guide, the official sheet translated, how the pages were made, how to build and run the examiners' base tests | the night before, and when handed docs/m at the defense |
| [`docs/guide/RUNNING.md`](guide/RUNNING.md) | setup checks, build targets, running by hand, what each suite section checks, what each demo shows, the guest image table, reading failures | running anything, or explaining what a script does |
| [`docs/guide/A-16MB.md`](guide/A-16MB.md) | modification A: 16 MB guests | defending phase A |
| [`docs/guide/B-SEEK_CUR-O_APPEND.md`](guide/B-SEEK_CUR-O_APPEND.md) | modification B: `SEEK_CUR` and `O_APPEND` | defending phase B |
| [`docs/guide/C-VECTOR33-STOP.md`](guide/C-VECTOR33-STOP.md) | modification C: vector 33 stop on `k` | defending phase C |
| [`docs/guide/patches/`](guide/patches/) | `modA.diff`, `modB.diff`, `modC.diff`: the exact verified solutions | the answer key; `git apply --check` first |
| [`docs/m/AOR2_2026_Modifikacije.pdf`](m/AOR2_2026_Modifikacije.pdf) | the official modification sheet (Cyrillic) | the source for the guide's translation |
| [`docs/m/A/`](m/A/), [`docs/m/B/`](m/B/) | the examiners' guest programs for the unmodified project | running the base tests; see the guide README for the two caveats |
| [`docs/m/C/test.txt`](m/C/test.txt) | the phase C scenario in one paragraph | it is what `scripts/demo_c.sh` step C3 runs |
| [`tests/expected/phase0.txt`](../tests/expected/phase0.txt) | golden output for the phase 0 gate | `scripts/regress.sh` reports a diff |
| `docs/INDEX.md` | this file | |

## Source map

### Shared

| File | Owns |
|---|---|
| [`common/hv_abi.h`](../common/hv_abi.h) | everything both sides must agree on: `PORT_SERIAL`, `IRQ_VECTOR`, `PORT_FILE`, `PORT_BUF`, `PORT_ACK`, `BUFFER_SIZE`, the mode values, `enum hv_op`, the `HV_O_*` and `HV_SEEK_*` values, `struct hv_request` with its static asserts |

### Host (`host/`)

| Files | Module | Key functions |
|---|---|---|
| [`host/src/main.c`](../host/src/main.c) | startup, one thread per VM, teardown order | `main`, `vm_thread` |
| [`host/inc/opts.h`](../host/inc/opts.h), [`host/src/opts.c`](../host/src/opts.c) | command line, validation, usage | `parse_options`, `collect_list`, `parse_memory`, `parse_page` |
| [`host/inc/vm.h`](../host/inc/vm.h), [`host/src/vm.c`](../host/src/vm.c) | KVM setup, paging, image loading, the port-I/O dispatch, the run loop, guest pointer translation, interrupt injection | `vm_init`, `vm_setup`, `setup_paging_4k`, `setup_paging_2m`, `load_guest_image`, `handle_io`, `handle_buf_port`, `handle_ack_port`, `vm_run`, `guest_ptr`, `guest_str`, `inject_irq`, `io_u8`, `io_u32` |
| [`host/inc/fileio.h`](../host/inc/fileio.h), [`host/src/fileio.c`](../host/src/fileio.c) | phase B: name rules, per-VM descriptor table, the five operations, shared registry, copy-on-write | `is_valid_name`, `hv_file_request`, `hv_open`, `hv_read`, `hv_write`, `hv_lseek`, `cow_materialize` |
| [`host/inc/shared_buf.h`](../host/inc/shared_buf.h), [`host/src/shared_buf.c`](../host/src/shared_buf.c) | phase C: the one-writer, N-reader round barrier and its watchdog | `sb_wait_turn`, `sb_writer_*`, `sb_reader_*`, `sb_vm_gone` |
| [`host/inc/output.h`](../host/inc/output.h), [`host/src/output.c`](../host/src/output.c) | per-VM line buffering, the `[vm N]` prefix, shared stdin | `out_char`, `out_flush`, `out_printf`, `in_byte` |
| [`host/Makefile`](../host/Makefile) | builds `host/build/hypervisor` from every `src/*.c` | |

### Guest (`guest/`)

| Files | Owns |
|---|---|
| [`guest/guest.ld`](../guest/guest.ld) | raw-binary link at the load address the host expects; `.start` first, `.bss` bounds |
| [`guest/Makefile`](../guest/Makefile) | every `lib/*.c` into every image; `tests/<name>.c` becomes `build/<name>.img`; `make debug` for ELF copies |
| [`guest/lib/start.c`](../guest/lib/start.c) | `_start`: zero `.bss`, recover `mem_size` from `rsp`, GDT, IDT, `sti`, `guest_main`, `hlt` |
| [`guest/lib/interrupts.c`](../guest/lib/interrupts.c) | the IDT, `set_idt_gate`, the vector 32 handler that dispatches to the role hooks |
| [`guest/lib/irqproto.c`](../guest/lib/irqproto.c) | `hv_buf_send`, `hv_buf_recv`: the phase C byte protocol on ports 0x510 and 0x520 |
| [`guest/lib/syscall.c`](../guest/lib/syscall.c) | `hv_call` and the five file functions with the spec's signatures |
| [`guest/lib/print.c`](../guest/lib/print.c) | `putch`, `print`, `print_dec`, `print_hex`, `getch` on the serial port |
| [`guest/lib/string.c`](../guest/lib/string.c) | `memset`, `memcpy`, `memmove`, `strlen` for freestanding code |
| [`guest/inc/`](../guest/inc/) | one header per lib file, plus `io.h` (`inb`/`outb` family, `cli`/`sti`/`hlt`), `descriptors.h` (GDT/IDT entry layouts), `guest.h` (`guest_main`, `guest_mem_top`) |
| [`guest/tests/`](../guest/tests/) | sixteen images, one per file; the table in [`guide/RUNNING.md`](guide/RUNNING.md#6-the-guest-images) says what each does and which suite uses it |

### Scripts and top level

| File | Does |
|---|---|
| [`Makefile`](../Makefile) | `make` (both trees), `make test`, `make demo-a/b/c`, `make clean` |
| [`scripts/run_tests.sh`](../scripts/run_tests.sh) | runs the four suites in order; what `make test` calls |
| [`scripts/regress.sh`](../scripts/regress.sh) | phase 0 gate: clean build, no warnings, output identical to the golden file |
| [`scripts/test_phase_a.sh`](../scripts/test_phase_a.sh) | phase A suite, 19 checks |
| [`scripts/test_phase_b.sh`](../scripts/test_phase_b.sh) | phase B suite, 24 checks |
| [`scripts/test_phase_c.sh`](../scripts/test_phase_c.sh) | phase C suite, 20 checks |
| [`scripts/demo_a.sh`](../scripts/demo_a.sh), [`demo_b.sh`](../scripts/demo_b.sh), [`demo_c.sh`](../scripts/demo_c.sh) | the narrated defense demos, no assertions |
| [`scripts/demo_lib.sh`](../scripts/demo_lib.sh) | helpers sourced by the demos |
| [`.gitignore`](../.gitignore) | build output, `vm_*/`, generated test data, `scratch/` |

## Where to look when

| You want to | Open |
|---|---|
| run the tests or a demo | [`guide/RUNNING.md`](guide/RUNNING.md) sections 4 and 5 |
| add a command-line option | `host/inc/opts.h`, `host/src/opts.c`; `host/inc/vm.h` and `host/src/main.c` if the VM must see it; worked example: `-v` in [`REHEARSAL.md`](REHEARSAL.md) item 5 |
| add a file syscall | `common/hv_abi.h`, `guest/inc/syscall.h`, `guest/lib/syscall.c`, `host/src/fileio.c`; [`REHEARSAL.md`](REHEARSAL.md) item 2 measures it at 17 lines |
| add `SEEK_CUR` or `O_APPEND` | [`guide/B-SEEK_CUR-O_APPEND.md`](guide/B-SEEK_CUR-O_APPEND.md) |
| allow 16 MB guests | [`guide/A-16MB.md`](guide/A-16MB.md); the trap is in [`REHEARSAL.md`](REHEARSAL.md) item 1 |
| add a port | `handle_io` in `host/src/vm.c`; `guest/inc/io.h` already has the guest side |
| add an interrupt vector | `guest/lib/interrupts.c`; the host side of injection is the `KVM_EXIT_HLT` and `KVM_EXIT_IRQ_WINDOW_OPEN` cases in `host/src/vm.c`; full example: [`guide/C-VECTOR33-STOP.md`](guide/C-VECTOR33-STOP.md) |
| change the shared-buffer protocol | `host/src/shared_buf.c`, `guest/lib/irqproto.c`; the README's protocol diagram first |
| add a test image | drop `guest/tests/<name>.c` with a `guest_main`; nothing else |
| explain a design decision | [`README.md`](../README.md) "Design decisions", then the longer D1 to D7 in [`ROADMAP.md`](ROADMAP.md) |
| explain a bug that happened | [`ROADMAP.md`](ROADMAP.md) risk register (items 3 and 10 fired), and the `fix:` commits in `git log` |
| walk the examiner through the build order | `git log --oneline`; commits carry the roadmap task IDs |
| run the examiners' base tests | [`guide/README.md`](guide/README.md) last section |

## Generated and ignored

- `host/build/`, `guest/build/`: build output. `make clean` removes them.
- `vm_0/`, `vm_1/`, `vm_2/`: per-VM local files from any run, left for inspection.
- `input.txt`, `shared.txt`, `shared.txt.orig`: generated by the suites and demos.
- `scratch/`: your own workspace, never committed. `scratch/explanations/` is for
  concept write-ups; the `.bak` files there are snapshots from before the refactor,
  and `scratch/baseline.txt` is the starter's untouched output.
