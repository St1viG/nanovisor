#!/usr/bin/env bash
#
# Runs every suite: the phase 0 regression gate and the phase A, B and C
# verification demos. Also reachable as `make test`.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

suites=(
	"scripts/regress.sh:phase 0 regression gate"
	"scripts/test_phase_a.sh:phase A"
	"scripts/test_phase_b.sh:phase B"
	"scripts/test_phase_c.sh:phase C"
)

failed=()

for entry in "${suites[@]}"; do
	script="${entry%%:*}"
	name="${entry#*:}"

	echo "========================================"
	echo "  $name"
	echo "========================================"

	if "$script"; then
		echo
	else
		failed+=("$name")
		echo
	fi
done

echo "========================================"
if [ "${#failed[@]}" -eq 0 ]; then
	echo "  ALL SUITES PASSED"
	echo "========================================"
	exit 0
fi

echo "  FAILED: ${failed[*]}"
echo "========================================"
exit 1
