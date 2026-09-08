#!/usr/bin/env bash
# Phase A defense demo: multiple VMs, parametric paging, option validation,
# fault isolation, serial I/O.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/demo_lib.sh"
build

heading "A1  Two VMs, one thread each, output serialized per line"
step $HV --memory 4 --page 2 --guest $IMG/hello.img $IMG/hello.img
pause

heading "A2  Parametric paging: every --memory / --page combination"
for m in 2 4 8; do
	for p in 4 2; do
		step $HV -m $m -p $p -g $IMG/mem_probe.img
	done
done
pause

heading "A3  Option validation"
step $HV -m 3 -g $IMG/hello.img
step $HV -p 8 -g $IMG/hello.img
step $HV -m 4
step $HV -x
pause

heading "A4  Fault isolation: vm 0 executes ud2, vm 1 must finish"
step $HV -m 4 -p 4 -g $IMG/crash.img $IMG/hello.img
pause

heading "A5  Serial input and output on port 0xE9"
echo
echo "\$ echo 'hello serial world' | $HV -g $IMG/echo.img"
echo "────────────────────────────────────────────────────────────"
echo "hello serial world" | $HV -g $IMG/echo.img
echo "────────────────────────────────────────────────────────────"
