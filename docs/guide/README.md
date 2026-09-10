# Defense modification guide

The course publishes one official modification per phase in
[`../mods/AOR2_2026_Modifikacije.pdf`](../mods/AOR2_2026_Modifikacije.pdf); you implement
only the one for the phase you defend. Each page here gives the task, where it lives
in this tree, the edits step by step, the exact solution as a patch, how to prove it
works with captured output, and what to say when asked why.

| Phase | Modification | Page | Code files | Code lines | Rehearsal budget |
|---|---|---|---|---|---|
| A | guests of 16 MB | [A-16MB.md](A-16MB.md) | 3 | ~8 | 10 min |
| B | `SEEK_CUR` = 3 and `O_APPEND` = 9 | [B-SEEK_CUR-O_APPEND.md](B-SEEK_CUR-O_APPEND.md) | 4 plus a test image | ~40 | 20 min |
| C | vector 33: `k` on the writer's stdin stops every VM | [C-VECTOR33-STOP.md](C-VECTOR33-STOP.md) | 8 | ~100 | 45 min |

Setup, the build, every script and every guest image are explained in
[RUNNING.md](RUNNING.md).

The budgets are estimates for someone who has read the page once. Time yourself;
what costs time is remembering *where* each edit goes, not typing it.

## How these pages were made

Every modification was implemented on a copy of this tree, `make test` was run,
the outputs quoted on each page were captured from those runs, and the resulting
diff was saved under [`patches/`](patches/). The patches are against the tree as of
2026-09-08 and rely on the readiness helpers (`io_u8`/`io_u32`, `PORT_SERIAL`,
`IRQ_VECTOR` in `common/hv_abi.h`). Check that one still applies with:

```sh
git apply --check docs/guide/patches/modA.diff
```

Treat a patch as the answer key, not as the thing you type at the defense: the
examiner wants to watch you find the places. The step lists are written for that.

## The official sheet, translated

> Implement the following modifications, one per phase; only the one for the phase
> being defended.
>
> **A. Basic hypervisor features.** Add support for a guest physical memory size of
> 16 MB. Change the long-mode support so that the new size can be supported.
>
> **B. File support.** Add an `lseek` indicator `SEEK_CUR` = 3: the cursor moves to
> `offset` added to the current position. Add an open flag `O_APPEND` = 9: the file
> is opened for writing only, and before each write the cursor is placed at the end
> of the file.
>
> **C. Interrupt support.** Add support for an input interrupt, vector 33. Extend the
> writer VM so that if the character `k` arrives on standard input (read with an IN
> instruction by the writer VM), the hypervisor stops every VM. Make sure the writer
> reads standard input after it has written to the shared buffer. Reader VMs, besides
> reading the shared buffer, stop in their vector 33 handler when a request for that
> interrupt arrives.

## Before the modification: the official base tests

`../mods/A` and `../mods/B` hold the examiners' guest programs for the unmodified project;
`../mods/C/test.txt` describes the phase C scenario, which is what `scripts/demo_c.sh`
runs (with `-i`, this project's own flag; say so before you type it). The programs
define their own `_start` and expect `open`/`close`/`read`/`write`/`lseek` from your
guest library, so they are linked without `lib/start.c`:

```sh
CFLAGS="-m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -fno-stack-protector \
  -mgeneral-regs-only -fno-asynchronous-unwind-tables -fcf-protection=none \
  -fno-builtin -fno-tree-loop-distribute-patterns -O2 -Iguest/inc -Icommon"
make                                   # builds guest/build/lib/*.o
gcc $CFLAGS -c -o /tmp/b1.o docs/mods/B/b1_create_write_read.c
ld -T guest/guest.ld /tmp/b1.o guest/build/lib/syscall.o guest/build/lib/string.o -o /tmp/b1.img
./host/build/hypervisor -m 2 -p 4 -g /tmp/b1.img
```

Two things to know before running them:

- `a2_echo.c` calls `gdt_boot()` and `halt_forever()` without defining them. Link an
  extra object that provides both (an empty `gdt_boot`, and `halt_forever` as a
  `hlt` loop) and compile it with `-include` of a header declaring them.
- `b1` and `b2` name their files with an underscore (`b1_data.txt`), which the
  spec's naming rule does not list. `is_valid_name` in `host/src/fileio.c` accepts
  `_` as a deliberate extension for exactly this reason; say so if asked, and
  note that `/` and `..` are still refused, so the isolation argument stands.

Measured against this tree: all of them pass.
