#!/usr/bin/env bash
#
# Phase B verification: the three demos from docs/ROADMAP.md.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

HV=host/build/hypervisor
IMG=guest/build
pass=0
fail=0

ok()  { echo "  PASS  $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL  $1"; [ $# -gt 1 ] && echo "        $2"; fail=$((fail + 1)); }

run() { timeout 20 "$HV" "$@" 2>&1; }

echo "==> building"
make -C host >/dev/null 2>&1 && make -C guest >/dev/null 2>&1 || { echo "build failed"; exit 1; }

ORIGINAL="shared file original contents"

echo
echo "1. round trip"
rm -rf vm_0
out="$(run -m 4 -p 2 -g $IMG/file_basic.img)"
grep -q '^\[vm 0\] open(out.txt, O_RDWR|O_CREATE) -> 0$' <<<"$out" \
	&& ok "open returned a descriptor" || bad "open returned a descriptor" "$out"
grep -q '^\[vm 0\] read(back) -> 29$' <<<"$out" \
	&& ok "read returned what was written" || bad "read returned what was written" "$out"
grep -q '^\[vm 0\] content: nanovisor phase B round trip$' <<<"$out" \
	&& ok "content survived the round trip" || bad "content survived the round trip" "$out"
grep -q '^\[vm 0\] lseek(0, SEEK_END) -> 29$' <<<"$out" \
	&& ok "SEEK_END reports the file size" || bad "SEEK_END reports the file size" "$out"
[ "$(cat vm_0/out.txt 2>/dev/null)" = "nanovisor phase B round trip" ] \
	&& ok "host-side vm_0/out.txt matches" || bad "host-side vm_0/out.txt matches"

echo
echo "2. error matrix"
rm -rf vm_0
out="$(run -m 4 -p 2 -g $IMG/file_errors.img)"
n_ok="$(grep -c '\[ok\]$' <<<"$out")"
n_bad="$(grep -c 'UNEXPECTED' <<<"$out")"
[ "$n_bad" -eq 0 ] && ok "all $n_ok error cases behaved as specified" \
	|| bad "$n_bad unexpected results" "$(grep UNEXPECTED <<<"$out")"
[ "$n_ok" -ge 16 ] && ok "error matrix covers $n_ok cases" || bad "only $n_ok cases ran"

echo
echo "3. copy-on-write"
rm -rf vm_0 vm_1
printf '%s\n' "$ORIGINAL" > shared.txt
cp shared.txt shared.txt.orig
out="$(run -m 4 -p 2 -g $IMG/file_shared.img $IMG/file_shared2.img -f shared.txt)"

cmp -s shared.txt shared.txt.orig \
	&& ok "shared original untouched" || bad "shared original was modified"
[ -f vm_0/shared.txt ] && [ -f vm_1/shared.txt ] \
	&& ok "each VM materialized its own copy" || bad "per-VM copies missing"
! cmp -s vm_0/shared.txt vm_1/shared.txt \
	&& ok "the two copies differ" || bad "the two copies are identical"
[ "$(cat vm_0/shared.txt)" = "shared PATCHEDiginal contents" ] \
	&& ok "offset preserved across the CoW fd swap (patch at 7, not 0)" \
	|| bad "offset lost across CoW" "$(cat vm_0/shared.txt)"
[ "$(cat vm_1/shared.txt)" = "SECOND file original contents" ] \
	&& ok "second VM patched at its own offset" || bad "second VM patch wrong" "$(cat vm_1/shared.txt)"
[ "$(grep -c 'cow: shared.txt is now private' <<<"$out")" = 2 ] \
	&& ok "CoW triggered once per VM" || bad "CoW trigger count wrong" "$out"

echo
echo "4. isolation"
rm -rf vm_0 vm_1
run -m 4 -p 2 -g $IMG/file_basic.img $IMG/file_basic.img >/dev/null
[ -f vm_0/out.txt ] && [ -f vm_1/out.txt ] \
	&& ok "same name, separate per-VM files" || bad "per-VM directories missing"
out="$(run -m 4 -p 2 -g $IMG/file_errors.img)"
grep -q 'traversal attempt        -> -1' <<<"$out" \
	&& ok "path traversal rejected by the name rules" || bad "traversal not rejected"

echo
echo "5. two descriptors on one shared file"
rm -rf vm_0
printf '%s\n' "$ORIGINAL" > shared.txt
cp shared.txt shared.txt.orig
run -m 4 -p 2 -g $IMG/cow_twice.img -f shared.txt >/dev/null
# Each fd triggers CoW independently; the second must not discard the first's
# write by re-copying the original over the private file.
[ "$(cat vm_0/shared.txt 2>/dev/null)" = "AAAAed filBBBBiginal contents" ] \
	&& ok "both descriptors' writes survived copy-on-write" \
	|| bad "a write was lost across the second CoW" "$(cat vm_0/shared.txt 2>/dev/null)"
cmp -s shared.txt shared.txt.orig && ok "original still untouched" || bad "original modified"

echo
echo "6. hostile guest input"
rm -rf vm_0
out="$(run -m 4 -p 2 -g $IMG/hostile.img)"
grep -q 'survived' <<<"$out" \
	&& ok "hypervisor survived every malformed request" || bad "hypervisor did not survive" "$out"
# -1 arrives at the guest as 4294967295; all seven probes must be rejected.
[ "$(grep -c ': 4294967295$' <<<"$out")" = 7 ] \
	&& ok "all 7 out-of-range/malformed requests returned -1" \
	|| bad "some malformed request was accepted" "$out"
grep -q 'file request at out-of-range address' <<<"$out" \
	&& ok "out-of-range request address reported" || bad "no report for a bad request address"

rm -f shared.txt.orig

echo
echo "----------------------------------------"
echo "phase B: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
