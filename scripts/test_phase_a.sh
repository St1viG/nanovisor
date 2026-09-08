#!/usr/bin/env bash
#
# Phase A verification: the four demos from docs/ROADMAP.md.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

HV=host/build/hypervisor
IMG=guest/build
pass=0
fail=0

ok()   { echo "  PASS  $1"; pass=$((pass + 1)); }
bad()  { echo "  FAIL  $1"; [ $# -gt 1 ] && echo "        $2"; fail=$((fail + 1)); }

run() { timeout 20 "$HV" "$@" 2>&1; }

echo "==> building"
make -C host >/dev/null 2>&1 && make -C guest >/dev/null 2>&1 || { echo "build failed"; exit 1; }

echo
echo "1. multiple VMs"
out="$(run -m 4 -p 2 -g $IMG/hello.img $IMG/hello.img)"
[ "$(grep -c '^\[vm 0\] KVM_EXIT_HLT$' <<<"$out")" = 1 ] && ok "vm 0 halted" || bad "vm 0 halted" "$out"
[ "$(grep -c '^\[vm 1\] KVM_EXIT_HLT$' <<<"$out")" = 1 ] && ok "vm 1 halted" || bad "vm 1 halted" "$out"
if grep -qv '^\[vm [0-9]\+\] ' <<<"$out"; then
	bad "every line is prefixed (no garbled interleaving)" "$(grep -v '^\[vm [0-9]\+\] ' <<<"$out" | head -3)"
else
	ok "every line is prefixed (no garbled interleaving)"
fi

echo
echo "2. parametric paging, six combinations"
for m in 2 4 8; do
	for p in 4 2; do
		out="$(run -m $m -p $p -g $IMG/mem_probe.img)"
		if grep -q "mem_probe: ok" <<<"$out" && grep -q "($m MB)" <<<"$out"; then
			ok "-m $m -p $p"
		else
			bad "-m $m -p $p" "$out"
		fi
	done
done

echo
echo "3. option validation"
check_err() {
	local desc="$1" expect="$2"; shift 2
	local out rc
	out="$(run "$@")"; rc=$?
	if [ "$rc" -ne 0 ] && grep -qF -- "$expect" <<<"$out"; then
		ok "$desc"
	else
		bad "$desc (rc=$rc)" "$(head -1 <<<"$out")"
	fi
}
check_err "-m 3 rejected"          "--memory must be 2, 4 or 8 (got '3')"   -m 3 -g $IMG/hello.img
check_err "-p 8 rejected"          "--page must be 4 (4KB) or 2 (2MB)"    -p 8 -g $IMG/hello.img
check_err "missing --guest"        "at least one guest image is required" -m 4
check_err "unknown option"         "invalid option"                       -x
check_err "missing argument"       "option requires an argument"          -m
check_err "stray operand"          "unexpected operand"                   $IMG/hello.img

echo
echo "4. fault isolation"
out="$(run -m 4 -p 4 -g $IMG/crash.img $IMG/hello.img)"; rc=$?
grep -q '^\[vm 0\] unexpected exit: KVM_EXIT_SHUTDOWN (8)$' <<<"$out" \
	&& ok "vm 0 reports its exit reason" || bad "vm 0 reports its exit reason" "$out"
grep -q '^\[vm 1\] KVM_EXIT_HLT$' <<<"$out" \
	&& ok "vm 1 unaffected by its sibling's crash" || bad "vm 1 unaffected" "$out"
[ "$rc" -ne 0 ] && ok "non-zero exit status when a VM dies" || bad "non-zero exit status"

echo
echo "5. serial input on 0xE9"
out="$(echo "hello serial world" | timeout 20 "$HV" -g $IMG/echo.img 2>&1)"
grep -q '^\[vm 0\] hello serial world$' <<<"$out" \
	&& ok "guest echoed host stdin" || bad "guest echoed host stdin" "$out"

echo
echo "----------------------------------------"
echo "phase A: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
