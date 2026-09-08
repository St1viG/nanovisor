# Setup and running: what does what

Everything here was checked against this tree on 2026-09-08. Commands are run
from the repository root unless a `cd` is shown.

## 1. Prerequisites and how to check them

| Need | Check | Expected | If not |
|---|---|---|---|
| KVM device | `ls -l /dev/kvm` | `crw-rw---- 1 root kvm ...` | see below |
| your user in group `kvm` | `id -nG` | the list contains `kvm` | `sudo usermod -aG kvm $USER`, then log out and in |
| CPU virtualization visible | `grep -o -m1 -w 'svm\|vmx' /proc/cpuinfo` | `svm` (AMD) or `vmx` (Intel) | enable in firmware; on WSL2 see below |
| KVM module loaded | `lsmod \| grep kvm` | `kvm_amd` or `kvm_intel`, plus `kvm` | `sudo modprobe kvm_amd` (or `kvm_intel`) |
| C toolchain | `gcc --version; ld --version; make --version` | any recent GCC, binutils, GNU make | `sudo apt install build-essential` |
| python3 | `python3 --version` | any 3.x | `sudo apt install python3`; only the phase C suite and demo use it |
| `timeout`, `cmp`, `diff`, `mktemp` | `command -v timeout cmp diff mktemp` | four paths | coreutils and diffutils, present on any Debian/Ubuntu |

**WSL2.** KVM inside WSL2 needs nested virtualization. The file
`C:\Users\<you>\.wslconfig` must contain

```
[wsl2]
nestedVirtualization=true
```

and after editing it run `wsl --shutdown` from Windows, then reopen the shell.
This machine has that set, `/dev/kvm` present, the user in `kvm`, and `kvm_amd`
loaded; if the exam machine is different, the table above is the checklist.

**Never** create an in-kernel irqchip when experimenting with KVM here
(`KVM_CREATE_IRQCHIP`, `KVM_CREATE_PIT2`): the project injects interrupts with
`KVM_INTERRUPT`, which fails once an irqchip exists (design D7 in `README.md`).

## 2. Build

```sh
make            # both trees: host/build/hypervisor and guest/build/*.img
make host       # host only
make guest      # guest only
make clean      # removes host/build, guest/build, vm_*/, input.txt, shared.txt*
```

What `make` produces:

- `host/build/hypervisor`, from every `host/src/*.c` with `-Wall -Wextra -O2 -g -pthread`
  and `-Iinc -I../common`. Header dependencies are tracked (`-MMD`), so editing a
  header rebuilds what includes it; no `make clean` needed.
- `guest/build/<name>.img` for every `guest/tests/<name>.c`: a raw binary linked
  with every `guest/lib/*.c` through `guest/guest.ld`, at the load address the
  host expects. Dropping a new `.c` into `guest/tests/` is all it takes to get a
  new image; the linker script is a prerequisite of every image, so editing it
  relinks them all.
- `guest/build/guest.img`, a copy of `hello.img`, kept because the assignment names
  `guest.img` as the canonical guest and the phase 0 gate runs it.
- `make -C guest debug` additionally produces `guest/build/<name>.elf`, the same
  link as ELF, for `readelf -S` or `objdump -d` when you need to see the layout.

Warnings are errors as far as the phase 0 gate is concerned: `scripts/regress.sh`
fails on any `warning:` in a clean build of either tree.

## 3. Running the hypervisor by hand

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

`-g` and `-f` take several operands in a row; the list ends at the next option.
`-i`, `-w` and `-v` are this project's additions; the assignment's own examples
have none of them, so say so before typing `-i` at the defense.

One example per phase:

```sh
# A: two VMs, one thread each
./host/build/hypervisor --memory 4 --page 2 --guest guest/build/hello.img guest/build/hello.img

# B: a guest that creates, writes, seeks and reads a file in its own vm_0/
./host/build/hypervisor -m 4 -p 2 -g guest/build/file_basic.img
cat vm_0/out.txt

# B: two guests sharing a file; each write triggers a private copy
printf 'shared file original contents\n' > shared.txt
./host/build/hypervisor -m 4 -p 2 -g guest/build/file_shared.img guest/build/file_shared2.img -f shared.txt
cat shared.txt vm_0/shared.txt vm_1/shared.txt

# C: one writer streams input.txt to two readers through the shared buffer
python3 -c "open('input.txt','w').write('X'*200)"
./host/build/hypervisor -m 4 -p 2 -i -g guest/build/irq_writer.img guest/build/irq_reader.img guest/build/irq_reader.img -f input.txt
cmp input.txt vm_1/out.txt && cmp input.txt vm_2/out.txt && echo identical
```

What you see and what it means:

- Every line of guest output is prefixed `[vm N]`. Lines are buffered per VM and
  flushed whole, so several VMs interleave by line, never mid-line.
- `[vm N] KVM_EXIT_HLT` is the normal end of a VM: the guest executed `hlt`.
- Outside an interrupt session every VM first prints `IRQ0 received!` three times.
  That is the starter's behaviour, kept on purpose so the phase 0 gate stays
  byte-identical; the phase A demos show it too.
- `[vm N] unexpected exit: <reason> (<code>)` followed by a register dump means
  that VM died; its thread ends and the others continue. `crash.img` produces
  `KVM_EXIT_SHUTDOWN (8)` deliberately.
- `[vm N] cow: shared.txt is now private` marks the first write to a shared file.
- `[vm N] watchdog: 5s waiting for ...` means a VM thread has been blocked for
  five seconds in the phase C barrier. It keeps waiting; the line names what it
  waits for and dumps the barrier state, which is how the one real deadlock in
  this project was found.
- Exit status is 0 when every VM halted normally, 1 when options were rejected or
  any VM died or could not be set up.
- Each VM gets a directory `vm_<id>/` in the current directory for its local
  files. It is left on disk for inspection; `make clean` removes them.
- Standard input is shared by all VM threads. Only guests that read port 0xE9
  consume it (`echo.img`), so `printf 'text' | ./host/build/hypervisor ...` is how
  you feed them.

## 4. The test suites

```sh
make test                 # everything, in order; same as ./scripts/run_tests.sh
./scripts/regress.sh      # phase 0 gate alone
./scripts/test_phase_a.sh # phase A alone
./scripts/test_phase_b.sh # phase B alone
./scripts/test_phase_c.sh # phase C alone
```

Every suite rebuilds both trees itself, changes to the repository root, prints
`  PASS  <what>` or `  FAIL  <what>` per check with the offending output under a
failure, ends with `phase X: N passed, M failed`, and exits 1 on any failure.
`run_tests.sh` runs the four in order and prints `ALL SUITES PASSED` or
`FAILED: <suite names>`. The whole thing takes about seven seconds. Side effects
are limited to gitignored files in the root: `vm_0/`, `vm_1/`, `vm_2/`,
`input.txt`, `shared.txt`, `shared.txt.orig`.

### `scripts/regress.sh`, the phase 0 gate

Cleans and rebuilds both trees, fails if the build log contains a warning, runs
`hypervisor -g guest/build/guest.img` under a 10-second timeout, and diffs the
output against `tests/expected/phase0.txt`: three `IRQ0 received!` lines,
`Hello, world!`, `KVM_EXIT_HLT`, each with the `[vm 0]` prefix. It proves the
refactoring never changed what the starter printed. Prints `PHASE 0 GATE PASS`
or `PHASE 0 GATE FAIL`.

### `scripts/test_phase_a.sh`, 19 checks

| Section | Image | Checks |
|---|---|---|
| 1. multiple VMs | `hello.img` twice | both halt; every output line carries a `[vm N]` prefix (3) |
| 2. parametric paging | `mem_probe.img` | all six `-m 2/4/8` by `-p 4/2` combinations report `mem_probe: ok` with the right size (6) |
| 3. option validation | `hello.img` | `-m 3`, `-p 8`, no `-g`, unknown option, missing argument, stray operand: each rejected with the expected message and a non-zero status (6) |
| 4. fault isolation | `crash.img` + `hello.img` | vm 0 reports `KVM_EXIT_SHUTDOWN (8)`, vm 1 still halts normally, exit status non-zero (3) |
| 5. serial input | `echo.img` | a line piped to stdin comes back on port 0xE9 (1) |

### `scripts/test_phase_b.sh`, 24 checks

| Section | Image | Checks |
|---|---|---|
| 1. round trip | `file_basic.img` | open returns a descriptor, read returns what was written, content matches, `SEEK_END` reports the size, `vm_0/out.txt` matches on the host (5) |
| 2. error matrix | `file_errors.img` | the guest labels each of its 17 probes `[ok]` or `[UNEXPECTED]`; zero unexpected, at least 16 probes (2) |
| 3. copy-on-write | `file_shared.img` + `file_shared2.img` with `-f shared.txt` | original untouched, one private copy per VM, copies differ, the patch landed at offset 7 (offset survived the fd swap), the second VM patched at its own offset, exactly two `cow:` lines (6) |
| 4. isolation | `file_basic.img` twice, `file_errors.img` | same file name lands in separate `vm_N/` directories; `..` and `/` in names are refused (2) |
| 5. two descriptors on one shared file | `cow_twice.img` | both descriptors' writes survive, original untouched (2) |
| 6. descriptor table limits | `fd_limit.img` | 16 opens succeed and 8 are refused, earlier descriptors keep working, a slot is reused after close, refused opens created no files (4) |
| 7. hostile guest input | `hostile.img` | seven malformed requests all return -1, the VM survives, the bad address is reported (3) |

### `scripts/test_phase_c.sh`, 20 checks

All runs use `-i`. `python3` generates `input.txt` for each scenario.

| Section | Images | Checks |
|---|---|---|
| 1. mode assignment | three `irq_probe.img` | exactly one writer and two readers, all three halt; `-w 2` moves the writing role (4) |
| 2. barrier ordering | `irq_writer.img` + two `irq_reader.img`, `-v` | the `-v` trace is replayed: the writer never publishes before both readers acked; 3 data rounds plus the sentinel (2) |
| 3. full spec scenario | same | both readers' `out.txt` are byte-exact copies of a 20-line input; all three see the sentinel; a sub-buffer input and an exactly-64-byte input also transfer (5) |
| 4. excess discarded | `irq_flood.img` + `irq_reader.img` | the writer is told 128 sent, 64 accepted (1) |
| 5. reader dies mid-session | writer + `irq_reader.img` + `crash.img` | the dead reader reports its exit, the survivor loses no data, the run finishes in under 5 s (3) |
| 6. earlier phases | `hello.img` without `-i` | still prints `IRQ0 received!` three times (1) |
| 7. short reader | writer + `irq_reader.img` + `irq_short.img` | the reader that stores 4 bytes per round is stopped; the compliant one is unaffected (2) |
| 8. VM that never starts | writer + a nonexistent image, and the reverse | neither direction hangs; both finish in under 8 s (2) |

Sections 5 and 8 assert on wall-clock time, and the barrier has a 5-second
watchdog. On a heavily loaded machine these can fail with nothing wrong; rerun
the suite before believing such a failure.

## 5. The defense demos

```sh
make demo-a      # or ./scripts/demo_a.sh
make demo-b
make demo-c
```

These are the commands to type at the defense, narrated. They assert nothing:
each step prints the command as you would type it, runs it, and prints its exit
status. Between groups, `pause` waits for Enter when a terminal is attached and
continues by itself when the output is piped, so `make demo-b | cat` runs a demo
unattended. All three source `scripts/demo_lib.sh`, which only provides
`heading`, `step`, `pause` and `build`.

- **demo_a**: A1 two VMs; A2 all six memory/page combinations with `mem_probe`;
  A3 four rejected command lines; A4 `crash.img` next to `hello.img`; A5 a line
  piped into `echo.img`.
- **demo_b**: B1 `file_basic` round trip and `cat vm_0/out.txt`; B2 the error
  matrix; B3 copy-on-write with `file_shared` and `file_shared2`, then `cmp` of the
  original against its backup and `cat` of the two private copies.
- **demo_c**: C1 mode assignment with three probes, then with `-w 2`; C2 the
  barrier with `-v`, so `readers_pending` is visible reaching 0 before every
  publish; C3 the spec scenario with a 20-line input and two `cmp`s; C4 excess
  discard with `irq_flood`; C5 a reader that crashes mid-session.

## 6. The guest images

| Image | What it does | Used by |
|---|---|---|
| `hello.img` | prints `Hello, world!` and halts; the phase 0 baseline | regress, A, C6 |
| `mem_probe.img` | touches the top of memory and `.bss`, prints the size it found | A2 |
| `crash.img` | executes `ud2` with no handler: triple fault, `KVM_EXIT_SHUTDOWN` | A4, C5 |
| `echo.img` | reads port 0xE9 until end of input, echoes each byte back | A5 |
| `file_basic.img` | open, write, seek back, read, close on `out.txt` | B1, B4 |
| `file_errors.img` | 17 calls that must fail, each printed with `[ok]` or `[UNEXPECTED]` | B2, B4 |
| `file_shared.img` | reads the shared file, seeks to offset 7, writes `PATCHED` | B3 |
| `file_shared2.img` | same, offset 0 and `SECOND`, so the two copies differ | B3 |
| `cow_twice.img` | two descriptors on one shared file, both writing | B5 |
| `fd_limit.img` | opens 24 files against a 16-entry table | B6 |
| `hostile.img` | seven malformed file requests that must be refused | B7 |
| `irq_probe.img` | prints the mode assigned by the first interrupt, then ends the session | C1 |
| `irq_writer.img` | streams `input.txt` through the buffer, 64 bytes per round | C2, C3, C5, C7, C8 |
| `irq_reader.img` | appends every round to its own `out.txt` | C2, C3, C4, C5, C7, C8 |
| `irq_flood.img` | sends 128 bytes in one round to show the excess being discarded | C4 |
| `irq_short.img` | a reader that stores only 4 bytes per round and must be stopped | C7 |

Each image is `guest/tests/<name>.c` providing `guest_main()`; `guest/lib/start.c`
owns `_start`, sets up the GDT and IDT, enables interrupts, calls `guest_main`,
then halts. Phase C images opt into the protocol by defining
`guest_writer_round()` or `guest_reader_round()`.

## 7. Reading failures

- A `FAIL` line is followed by the output that was checked, indented. Rerun the
  command by hand from the suite (they are plain `hypervisor` invocations) to see
  it live.
- `build failed` from a suite: run `make` yourself to see the compiler error; the
  suites hide build output.
- `PHASE 0 GATE FAIL` with `build produced warnings`: the gate treats warnings as
  failures; fix the warning.
- `Permission denied` opening `/dev/kvm`: group membership; see section 1.
- `KVM_INTERRUPT: ...` errors: something created an in-kernel irqchip, or a vCPU
  ioctl was issued from the wrong thread; neither happens in this tree.
- Phase C timing failures or `watchdog:` lines during the suite: machine load;
  rerun. A watchdog line that repeats every 5 seconds with the same state is a
  real hang, and the state it prints says which VM is waiting for what.
- Leftover `vm_*/` directories from a previous run never break a suite, since each
  section removes what it needs, but `make clean` gives a clean slate.

## 8. The examiners' base tests and the modifications

The base test programs in `docs/m` are not part of `make test`; how to build and
run them, and the two things to know first, are in [README.md](README.md). After
implementing a modification, rerun `make test` and the checks on its page:
[A-16MB.md](A-16MB.md), [B-SEEK_CUR-O_APPEND.md](B-SEEK_CUR-O_APPEND.md),
[C-VECTOR33-STOP.md](C-VECTOR33-STOP.md).
