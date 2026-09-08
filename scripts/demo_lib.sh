# Shared helpers for the defense demo scripts. Sourced, not executed.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

HV=host/build/hypervisor
IMG=guest/build

# Pause between steps only when a person is watching.
pause() {
	if [ -t 0 ]; then
		printf '\n    -- press enter --'
		read -r _
		printf '\n'
	else
		echo
	fi
}

heading() {
	echo
	echo "════════════════════════════════════════════════════════════"
	echo "  $*"
	echo "════════════════════════════════════════════════════════════"
}

# Print a command the way it would be typed, then run it.
step() {
	echo
	echo "\$ $*"
	echo "────────────────────────────────────────────────────────────"
	"$@"
	echo "────────────────────────────────────────────────────────────"
	echo "exit status: $?"
}

build() {
	make -C host >/dev/null && make -C guest >/dev/null || { echo "build failed"; exit 1; }
}
