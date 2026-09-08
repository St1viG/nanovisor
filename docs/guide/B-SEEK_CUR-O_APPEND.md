# B. `SEEK_CUR` and `O_APPEND`

**Task.** Add the `lseek` indicator `SEEK_CUR` = 3 (offset relative to the current
position) and the open flag `O_APPEND` = 9 (write-only; before each write the
cursor is placed at the end of the file).

**Where it lives.** Every file operation goes through `host/src/fileio.c`, the
constants through `common/hv_abi.h`, and the guest sees the spec names via
`guest/inc/syscall.h`. `struct guest_file` keeps its own `off` and all I/O is
`pread`/`pwrite` (design D3), so both features are a matter of what `off` is set
to.

**The one design call: 9 is not a bit.** The spec's flags are 1, 2, 4, 8 and combine
by OR. 9 as bits is `O_RD | O_CREATE`, so `O_APPEND` cannot be detected with `&`.
The only clean decode is an exact match: the whole flags word equals 9. From then
on it is an ordinary write-only open with one extra property. Consequences to
state out loud: `O_APPEND` cannot be combined with anything, `O_RD | O_CREATE` is
no longer expressible (no existing test used it), and append does not create a
missing file, the same as plain `O_WR`.

## Steps

1. **ABI.** `common/hv_abi.h`, next to the existing flags and indicators:

   ```c
   #define HV_O_APPEND 9   /* exact match only: as bits, 9 is O_RD|O_CREATE */
   #define HV_SEEK_CUR 3
   ```

2. **Guest names.** `guest/inc/syscall.h`:

   ```c
   #define O_APPEND HV_O_APPEND
   #define SEEK_CUR HV_SEEK_CUR
   ```

3. **Remember the mode.** `host/inc/fileio.h`, in `struct guest_file`:

   ```c
   int  append;   /* every write goes to the current end */
   ```

   `free_fd` zeroes the whole entry, so nothing else needs resetting.

4. **Decode it once, in `hv_open`.** Right after the name check and before
   `to_posix_flags`:

   ```c
   append = (flags == HV_O_APPEND);
   if (append)
   	flags = HV_O_WR;
   ```

   and where the entry is filled in: `f->append = append;`. With `flags` now
   `HV_O_WR`, `to_posix_flags`, `can_write`, the shared-file rules and the
   `O_CREATE` refusal all behave as for any write-only open.

5. **Append at write time, in `hv_write`.** After the copy-on-write step and before
   `pwrite`:

   ```c
   if (f->append) {
   	struct stat st;

   	if (fstat(f->hostfd, &st) < 0)
   		return -1;
   	f->off = (long)st.st_size;
   }
   ```

   After the CoW step on purpose: for a shared file it is the private copy whose
   size is measured. The `f->off += n` afterwards is unchanged.

6. **`SEEK_CUR`, in `hv_lseek`.** One more case before `default`:

   ```c
   case HV_SEEK_CUR:
   	if (f->off + offset < 0)
   		return -1;
   	f->off += offset;
   	break;
   ```

7. **A test image.** `guest/tests/file_append.c` (in the patch) writes `abc`, seeks
   with `SEEK_CUR` forward and past the start, reopens with `O_APPEND`, seeks to 0,
   writes `def` and reads back `abcdef`; then tries `O_APPEND` on a missing file
   and on a shared file. The build picks it up automatically.

8. **The error matrix used 3 as its invalid indicator.** `guest/tests/file_errors.c`
   probes `lseek(fd, 0, 3)` expecting -1. That value is now `SEEK_CUR`, so change
   the probe to 7 or the phase B suite reports one unexpected result. Add section 8
   from the patch to `scripts/test_phase_b.sh` if you want the feature in `make test`.

## Verify

```sh
make
printf 'ORIGINAL' > shared.txt
./host/build/hypervisor -m 4 -p 2 -g guest/build/file_append.img -f shared.txt
cat shared.txt; echo; cat vm_0/shared.txt; echo; cat vm_0/log.txt
make test
```

Captured output:

```
[vm 0] IRQ0 received!
[vm 0] IRQ0 received!
[vm 0] IRQ0 received!
[vm 0] lseek(1, SEEK_SET) -> 1
[vm 0] lseek(1, SEEK_CUR) -> 2
[vm 0] lseek(-5, SEEK_CUR) -> -1
[vm 0] open(log.txt, O_APPEND) -> 0
[vm 0] lseek(0, SEEK_SET) -> 0
[vm 0] write(def) -> 3
[vm 0] read on an append fd -> -1
[vm 0] content: abcdef
[vm 0] open(missing.txt, O_APPEND) -> -1
[vm 0] open(shared.txt, O_APPEND) -> 0
[vm 0] cow: shared.txt is now private
[vm 0] write(+more) -> 5
[vm 0] KVM_EXIT_HLT
```

Host `shared.txt` still reads `ORIGINAL`; `vm_0/shared.txt` reads `ORIGINAL+more`;
`vm_0/log.txt` reads `abcdef`. `make test` reports 30 phase B checks with the new
section, all passing.

## What to say

- Why not open the host fd with POSIX `O_APPEND`: on Linux, `pwrite` on an
  `O_APPEND` descriptor ignores its offset and appends anyway, so it would also
  work, but design D3 keeps the position in `struct guest_file` and never trusts
  the kernel's. Computing the end at write time keeps that rule intact and makes
  the ordering against copy-on-write explicit.
- What `lseek` means on an append descriptor: it moves `off`, and the next write
  moves it back to the end. Same as POSIX.
- Why `SEEK_CUR` is three lines: the offset is ours, so "current position" is a
  field, not a syscall.
- Why `lseek(fd, -5, SEEK_CUR)` fails: positions before the start are refused,
  consistent with the existing negative `SEEK_SET` rule.

**Measured:** 7 files, 129 insertions, 2 deletions. Code: 4 files, about 40 lines;
the rest is the test image, the suite section and the error-matrix probe. Patch:
[`patches/modB.diff`](patches/modB.diff).
