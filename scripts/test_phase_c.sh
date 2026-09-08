#!/usr/bin/env bash
#
# Phase C verification: the three demos from docs/ROADMAP.md, plus the
# failure modes from the risk register.
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

run() { timeout 30 "$HV" "$@" 2>&1; }

echo "==> building"
make -C host >/dev/null 2>&1 && make -C guest >/dev/null 2>&1 || { echo "build failed"; exit 1; }

echo
echo "1. mode assignment"
out="$(run -m 4 -p 2 -i -g $IMG/irq_probe.img $IMG/irq_probe.img $IMG/irq_probe.img)"
[ "$(grep -c 'mode = 1 (writer)' <<<"$out")" = 1 ] \
	&& ok "exactly one writer" || bad "exactly one writer" "$out"
[ "$(grep -c 'mode = 0 (reader)' <<<"$out")" = 2 ] \
	&& ok "exactly two readers" || bad "exactly two readers" "$out"
[ "$(grep -c 'KVM_EXIT_HLT' <<<"$out")" = 3 ] \
	&& ok "all three VMs terminated" || bad "all three VMs terminated" "$out"

out="$(run -m 4 -p 2 -i -w 2 -g $IMG/irq_probe.img $IMG/irq_probe.img $IMG/irq_probe.img)"
grep -q '^\[vm 2\] mode = 1 (writer)$' <<<"$out" \
	&& ok "--writer 2 moves the writing role" || bad "--writer selects the writer" "$out"

echo
echo "2. barrier ordering"
rm -rf vm_0 vm_1 vm_2
python3 -c "open('input.txt','w').write('A'*64 + 'B'*64 + 'C'*20)"
out="$(run -m 4 -p 2 -i -v -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/irq_reader.img -f input.txt)"
trace="$(grep -o 'trace: [a-z ]*' <<<"$out" | sed 's/trace: //')"
# Every "writer published" must be preceded by readers_pending having reached 0.
violations=0
pending=0
while read -r line; do
	case "$line" in
		"writer published") [ "$pending" -ne 0 ] && violations=$((violations+1)); pending=2 ;;
		"reader acked")     pending=$((pending-1)) ;;
	esac
done <<<"$trace"
[ "$violations" -eq 0 ] \
	&& ok "writer never advanced before both readers acked" \
	|| bad "$violations barrier violations"
[ "$(grep -c 'writer published' <<<"$out")" = 4 ] \
	&& ok "3 data rounds + 1 sentinel round" || bad "round count" "$(grep -c 'writer published' <<<"$out")"

echo
echo "3. full spec scenario"
rm -rf vm_0 vm_1 vm_2
python3 -c "
lines = ['line %03d: the quick brown fox jumps over the lazy dog' % i for i in range(1, 21)]
open('input.txt','w').write('\n'.join(lines) + '\n')"
out="$(run -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/irq_reader.img -f input.txt)"
cmp -s input.txt vm_1/out.txt && ok "vm_1 received a byte-exact copy" || bad "vm_1 copy differs"
cmp -s input.txt vm_2/out.txt && ok "vm_2 received a byte-exact copy" || bad "vm_2 copy differs"
[ "$(grep -c 'end of stream' <<<"$out")" = 3 ] \
	&& ok "writer and both readers saw the sentinel" || bad "sentinel not seen by all" "$out"

# Smaller than one buffer.
rm -rf vm_0 vm_1
python3 -c "open('input.txt','w').write('short input\n')"
run -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img -f input.txt >/dev/null
cmp -s input.txt vm_1/out.txt && ok "single sub-buffer round transfers correctly" || bad "short input differs"

# Exactly one buffer, the boundary case.
rm -rf vm_0 vm_1
python3 -c "open('input.txt','w').write('Z'*64)"
run -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img -f input.txt >/dev/null
cmp -s input.txt vm_1/out.txt && ok "input of exactly BUFFER_SIZE transfers correctly" || bad "boundary input differs"

echo
echo "4. excess bytes discarded"
out="$(run -m 4 -p 2 -i -g $IMG/irq_flood.img $IMG/irq_reader.img)"
grep -q 'sent 128 bytes, hypervisor accepted 64' <<<"$out" \
	&& ok "writer told how many bytes were accepted" || bad "excess not reported" "$out"

echo
echo "5. a reader dying mid-session does not deadlock the writer"
rm -rf vm_0 vm_1 vm_2
python3 -c "open('input.txt','w').write('X'*200)"
start=$(date +%s)
out="$(run -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/crash.img -f input.txt)"
elapsed=$(( $(date +%s) - start ))
grep -q '^\[vm 2\] unexpected exit: KVM_EXIT_SHUTDOWN (8)$' <<<"$out" \
	&& ok "the dead reader reported its exit" || bad "dead reader report missing"
cmp -s input.txt vm_1/out.txt \
	&& ok "the surviving reader lost no data" || bad "surviving reader lost data"
[ "$elapsed" -lt 5 ] \
	&& ok "completed without hitting the watchdog (${elapsed}s)" || bad "took ${elapsed}s"

echo
echo "6. earlier phases still behave"
out="$(run -m 4 -p 2 -g $IMG/hello.img)"
[ "$(grep -c 'IRQ0 received!' <<<"$out")" = 3 ] \
	&& ok "without --irq the phase A interrupt behaviour is unchanged" || bad "phase A behaviour changed" "$out"

echo
echo "7. a reader that does not read the whole round is stopped"
rm -rf vm_0 vm_1 vm_2
python3 -c "open('input.txt','w').write('Z'*128)"
out="$(run -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/irq_short.img -f input.txt)"
grep -q 'read fewer bytes than the round carried' <<<"$out" \
	&& ok "short reader stopped, as the spec requires" || bad "short reader not stopped" "$out"
cmp -s input.txt vm_1/out.txt \
	&& ok "the compliant reader was unaffected" || bad "compliant reader lost data"

echo
echo "8. a VM that never starts must not hang the session"
# Both directions: shared_buf counts every VM at init, so one that dies before
# vm_setup has to retract its obligation or the survivors wait for ever.
start=$(date +%s)
run -m 4 -p 2 -i -g $IMG/irq_writer.img /nonexistent.img >/dev/null 2>&1
rc=$?
elapsed=$(( $(date +%s) - start ))
[ "$rc" -ne 124 ] && [ "$elapsed" -lt 8 ] \
	&& ok "reader that fails to load does not hang the writer (${elapsed}s)" \
	|| bad "hung when a reader failed to load (${elapsed}s)"

start=$(date +%s)
run -m 4 -p 2 -i -g /nonexistent.img $IMG/irq_reader.img >/dev/null 2>&1
rc=$?
elapsed=$(( $(date +%s) - start ))
[ "$rc" -ne 124 ] && [ "$elapsed" -lt 8 ] \
	&& ok "writer that fails to load does not hang the readers (${elapsed}s)" \
	|| bad "hung when the writer failed to load (${elapsed}s)"

rm -f input.txt small.txt
rm -rf vm_0 vm_1 vm_2

echo
echo "----------------------------------------"
echo "phase C: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
