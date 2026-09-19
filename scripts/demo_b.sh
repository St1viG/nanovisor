#!/usr/bin/env bash
# Phase B demo: file round trip, error matrix, copy-on-write.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/demo_lib.sh"
build

rm -rf vm_0 vm_1

heading "B1  Round trip: open, write, lseek back, read, close"
step $HV -m 4 -p 2 -g $IMG/file_basic.img
step cat vm_0/out.txt
pause

heading "B2  Error matrix: every call that must fail"
rm -rf vm_0
step $HV -m 4 -p 2 -g $IMG/file_errors.img
pause

heading "B3  Copy-on-write on a shared file"
rm -rf vm_0 vm_1
printf 'shared file original contents\n' > shared.txt
cp shared.txt shared.txt.orig
step cat shared.txt
step $HV -m 4 -p 2 -g $IMG/file_shared.img $IMG/file_shared2.img -f shared.txt

echo
echo "The original must be untouched:"
step cmp shared.txt shared.txt.orig

echo
echo "Each VM has its own copy, and they differ from each other:"
step cat vm_0/shared.txt
step cat vm_1/shared.txt

echo
echo "vm_0 seeked to offset 7 before writing, so PATCHED lands at 7, not 0."
echo "That is what proves the file offset survived the copy-on-write fd swap."
rm -f shared.txt.orig
