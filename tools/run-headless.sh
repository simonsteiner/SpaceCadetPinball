#!/usr/bin/env bash
# Runs the game on its own Xvfb display, so several sessions can test at once without meeting.
# Usage: tools/run-headless.sh <game data folder> [game arguments...]
# Prints "display :N pid P" on stderr once the game has started, then waits for it to exit.
# Drive the game on that display (for example with XTest) and stop it with "kill P", never with
# pkill or killall by name: the game runs as a copy named scp-headless-<this script's PID>, so a
# name another session matches can't reach it, and its settings go to a temporary XDG_DATA_HOME.
# Set SCP_EXE to run another executable, SCP_XDG_DATA_HOME to keep the settings, and
# SCP_SCREEN to change the screen size (default 1280x1024x24).
set -euo pipefail

if [ $# -lt 1 ]; then
	echo "Usage: $0 <game data folder> [game arguments...]" >&2
	exit 2
fi
data=$(cd "$1" && pwd)
shift
exe=${SCP_EXE:-$(cd "$(dirname "$0")/.." && pwd)/bin/SpaceCadetPinball}
if [ ! -x "$exe" ]; then
	echo "Not an executable: $exe" >&2
	exit 1
fi

tmp=$(mktemp -d)
xvfb_pid=
game_pid=
cleanup() {
	[ -n "$game_pid" ] && kill "$game_pid" 2>/dev/null || true
	[ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null || true
	rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# The first free display from 100 up. Xvfb's lock file decides between sessions that pick the same
# number; -displayfd would pick one too, but it gives up where /tmp/.X11-unix isn't writable.
display=
for n in $(seq 100 199); do
	[ -e "/tmp/.X$n-lock" ] && continue
	Xvfb ":$n" -screen 0 "${SCP_SCREEN:-1280x1024x24}" -nolisten tcp >/dev/null 2>&1 &
	xvfb_pid=$!
	for _ in $(seq 50); do
		kill -0 "$xvfb_pid" 2>/dev/null || break
		if [ "$(tr -d ' ' <"/tmp/.X$n-lock" 2>/dev/null)" = "$xvfb_pid" ]; then
			display=$n
			break 2
		fi
		sleep 0.1
	done
	kill "$xvfb_pid" 2>/dev/null || true
	xvfb_pid=
done
if [ -z "$display" ]; then
	echo "No free display between :100 and :199" >&2
	exit 1
fi

copy=$tmp/scp-headless-$$
cp "$exe" "$copy"
mkdir -p "$tmp/xdg"
(cd "$data" && DISPLAY=:$display SDL_VIDEODRIVER=x11 XDG_DATA_HOME=${SCP_XDG_DATA_HOME:-$tmp/xdg} exec "$copy" "$@") &
game_pid=$!
echo "display :$display pid $game_pid" >&2

status=0
wait "$game_pid" || status=$?
game_pid=
exit "$status"
