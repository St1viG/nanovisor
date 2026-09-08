# A. Guests of 16 MB

**Task.** Add 16 MB to the allowed guest memory sizes and change the long-mode
setup so that size works.

**Why it is more than the validator.** Design D1 puts the page tables below the
guest image. With 4 KB pages, 16 MB is 4096 PTEs, which is 8 page tables, 32 KB
starting at `PT_BASE` (0x4000) and ending at 0xC000. The image is loaded at
`GUEST_START_ADDR` (0x8000), so the last four tables would be overwritten by the
image and the guest would triple-fault at the first touch of an address whose
table is gone. `setup_paging_4k` in `host/src/vm.c` refuses that layout on purpose:

```
error: 16 MB with 4 KB pages needs 8 page tables (32768 bytes), but only
16384 bytes are reserved below the guest at 0x8000
```

So the load address has to move, and it lives in two places that must change
together: the host header and the guest linker script. With 2 MB pages nothing
else is needed: `setup_paging_2m` sets `PDE64_PS` on 8 PD entries instead of 4.

## Steps

1. **Accept the value.** `host/src/opts.c`, `parse_memory`:

   ```c
   if (v != 2 && v != 4 && v != 8 && v != 16) {
   	fprintf(stderr, "error: --memory must be 2, 4, 8 or 16 (got '%s')\n", s);
   ```

   and the `usage()` line: `-m, --memory <2|4|8|16>`.

2. **Make room below the guest.** `host/inc/vm.h`:

   ```c
   #define GUEST_START_ADDR 0x10000
   ```

   Update the D1 comment above it: `0x4000 .. 0xB000  PT[0..7]`, image at
   `0x10000`. Twelve pages now sit between `PT_BASE` and the image, enough for
   24 MB, so the capacity check in `setup_paging_4k` passes for 16 MB.

3. **Link the guest at the same address.** `guest/guest.ld`:

   ```
   . = 0x10000;
   ```

   This is the coupling from task A.4: `load_guest_image` copies the raw binary to
   `GUEST_START_ADDR` and `vm_setup` points `rip` at it, so the image must be
   linked there. The guest itself needs no change: `_start` recovers `mem_size`
   from the initial `rsp` rounded up to 2 MB, and 16 MB is a multiple.

4. **Suites and demo.** `scripts/test_phase_a.sh`: the paging matrix loop becomes
   `for m in 2 4 8 16`, and the `check_err` line for `-m 3` quotes the new message
   `--memory must be 2, 4, 8 or 16 (got '3')`. `scripts/demo_a.sh`: same loop.

5. **Docs that quote the numbers.** The usage line, memory map and D1 heading in
   `README.md`; the "never exceeds 8 MB" remark in `common/hv_abi.h`. Guest
   addresses still fit in 32 bits, so the file ABI is untouched.

## Verify

```sh
make
./host/build/hypervisor -m 16 -p 4 -g guest/build/mem_probe.img
./host/build/hypervisor -m 16 -p 2 -g guest/build/mem_probe.img
./host/build/hypervisor -m 32 -g guest/build/hello.img   # must be refused
make test
```

Captured output, identical for `-p 4` and `-p 2`:

```
[vm 0] IRQ0 received!
[vm 0] IRQ0 received!
[vm 0] IRQ0 received!
[vm 0] mem_probe: mem_top = 0x1000000 (16 MB)
[vm 0] mem_probe: ok
[vm 0] KVM_EXIT_HLT
```

```
error: --memory must be 2, 4, 8 or 16 (got '32')
```

`make test`: the phase 0 gate stays byte-identical, phase A reports 21 checks (the
matrix gained two), B and C are unchanged. `mem_probe` touches the top of memory
and `.bss`, so it proves the new tables exist; it does not walk every 2 MB, which
is why the refusal in `setup_paging_4k` matters more than the probe.

## What to say

- Why 0x10000: 0xC000 is the minimum that fits eight tables; 0x10000 is the next
  round number and leaves twelve table slots, so 24 MB would still fit.
- Why the guest did not change: identity map, `rsp = mem_size`, and the linker
  script origin is the only guest-side constant tied to the host.
- What breaks if only one side moves: linked at 0x8000 but loaded at 0x10000,
  every absolute address in the image is off by 0x8000 and the first string
  access or indirect call faults. That is why steps 2 and 3 are one edit.
- 2 MB pages need only the validator: the PD has 512 entries and 16 MB uses 8.

**Measured:** 8 files, 21 lines changed. Code: 3 files, 8 lines. Patch:
[`patches/modA.diff`](patches/modA.diff).
