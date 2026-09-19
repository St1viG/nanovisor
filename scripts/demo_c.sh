#!/usr/bin/env bash
# Phase C demo: mode assignment, the barrier, the full spec scenario.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/demo_lib.sh"
build

rm -rf vm_0 vm_1 vm_2

heading "C1  Mode assignment: exactly one writer, the rest readers"
step $HV -m 4 -p 2 -i -g $IMG/irq_probe.img $IMG/irq_probe.img $IMG/irq_probe.img
echo
echo "The writing role is selectable:"
step $HV -m 4 -p 2 -i -w 2 -g $IMG/irq_probe.img $IMG/irq_probe.img $IMG/irq_probe.img
pause

heading "C2  The barrier: the writer never advances before every reader acks"
rm -rf vm_0 vm_1 vm_2
python3 -c "open('input.txt','w').write('A'*64 + 'B'*64 + 'C'*20)"
echo "input.txt is 148 bytes and BUFFER_SIZE is 64, so this is 3 rounds + the sentinel."
step $HV -m 4 -p 2 -i -v -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/irq_reader.img -f input.txt
echo
echo "Read the trace: readers_pending reaches 0 before every 'writer published'."
pause

heading "C3  Full spec scenario: one writer streams a file to two readers"
rm -rf vm_0 vm_1 vm_2
python3 -c "
lines = ['line %03d: the quick brown fox jumps over the lazy dog' % i for i in range(1, 21)]
open('input.txt','w').write('\n'.join(lines) + '\n')"
step wc -c input.txt
step $HV -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/irq_reader.img -f input.txt
step cmp input.txt vm_1/out.txt
step cmp input.txt vm_2/out.txt
pause

heading "C4  Bytes beyond BUFFER_SIZE are discarded"
step $HV -m 4 -p 2 -i -g $IMG/irq_flood.img $IMG/irq_reader.img
pause

heading "C5  A reader that dies mid-session must not deadlock the writer"
rm -rf vm_0 vm_1 vm_2
python3 -c "open('input.txt','w').write('X'*200)"
step $HV -m 4 -p 2 -i -g $IMG/irq_writer.img $IMG/irq_reader.img $IMG/crash.img -f input.txt
step cmp input.txt vm_1/out.txt
rm -f input.txt
