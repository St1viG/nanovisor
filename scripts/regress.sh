#!/usr/bin/env bash
#
# Phase 0 regression gate.
#
# Phase 0 is a pure refactor: compiler flags, linker script, .bss zeroing and
# the vm_setup/vm_run split must not change one byte of what the program
# prints. tests/expected/phase0.txt is the output captured from the original
# starter, before any of it was touched.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

EXPECTED="tests/expected/phase0.txt"
IMG="guest/build/guest.img"
BIN="host/build/hypervisor"

fail=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

echo "==> clean build"
make -C host clean >/dev/null 2>&1
make -C guest clean >/dev/null 2>&1
if ! { make -C host && make -C guest; } >"$log" 2>&1; then
	echo "FAIL: build error"
	cat "$log"
	exit 1
fi

if grep -qiE 'warning:' "$log"; then
	echo "FAIL: build produced warnings"
	grep -iE 'warning:' "$log"
	fail=1
else
	echo "     no warnings"
fi

echo "==> running $BIN -g $IMG"
actual="$(timeout 10 "$BIN" -g "$IMG" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
	echo "FAIL: hypervisor exited with status $rc"
	fail=1
fi

if diff -u "$EXPECTED" <(printf '%s\n' "$actual"); then
	echo "     output matches $EXPECTED"
else
	echo "FAIL: output differs from $EXPECTED"
	fail=1
fi

if [ "$fail" -eq 0 ]; then
	echo
	echo "PHASE 0 GATE PASS"
	exit 0
fi

echo
echo "PHASE 0 GATE FAIL"
exit 1
