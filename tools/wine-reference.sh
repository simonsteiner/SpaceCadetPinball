#!/usr/bin/env bash
# Runs the original Full Tilt! executables under Wine as a reference, without root and without network.
# See docs/wine-reference.md. Everything lives in $SCP_WINE (default ~/.cache/scp/wine), outside the repository.
# It runs without sound; set SCP_WINE_SOUND=1 to hear the EXE.
#
#   tools/wine-reference.sh setup                       unpack Ubuntu's Wine packages and make the prefix
#   tools/wine-reference.sh start <data folder> <EXE> [args...]
#                                                       copy the folder into the prefix and start the EXE
#   tools/wine-reference.sh exec <command...>           run a command in the reference's namespace (DISPLAY is set)
#   tools/wine-reference.sh stop                        end Wine and the namespace
set -euo pipefail

W=${SCP_WINE:-$HOME/.cache/scp/wine}
DISPLAY_NUM=${SCP_WINE_DISPLAY:-93}
ROOT=$W/root
LIB=$ROOT/usr/lib/x86_64-linux-gnu
PACKAGES=(wine wine64 libwine fonts-wine wine-common libz-mingw-w64 libxkbregistry0 libcapi20-3t64
	libgphoto2-6t64 libgphoto2-port12t64 libgstreamer-plugins-base1.0-0 libpcap0.8t64)

export PATH=$LIB/wine:$PATH
export LD_LIBRARY_PATH=$LIB:$LIB/wine/x86_64-unix
export WINEDLLPATH=$LIB/wine
export WINEPREFIX=$W/prefix
export WINEARCH=win64
export WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES="mscoree,mshtml="
# Silent unless SCP_WINE_SOUND=1: without its audio drivers Wine has no sound device. Under WSLg it would otherwise
# reach the desktop's PulseAudio server through PULSE_SERVER, a socket the namespace doesn't cut off.
if [[ ${SCP_WINE_SOUND:-0} != 1 ]]; then
	WINEDLLOVERRIDES+=";winepulse.drv,winealsa.drv,wineoss.drv="
	unset PULSE_SERVER
fi
export DISPLAY=:$DISPLAY_NUM

# The namespace has no network but its own loopback. Xvfb runs inside it, because WSLg's /tmp/.X11-unix is
# read-only, so Xvfb listens only on an abstract socket, which a process in another namespace can't reach.
ns_pid() {
	[[ -f $W/ns.pid ]] && kill -0 "$(cat "$W/ns.pid")" 2>/dev/null && cat "$W/ns.pid"
}

ensure_ns() {
	ns_pid >/dev/null && return
	# shellcheck disable=SC2016 # $$ and the arguments expand in the inner shell
	unshare -n --map-current-user sh -c 'echo $$ > "$1"; exec Xvfb ":$2" -screen 0 1280x1024x24 -nolisten tcp' \
		sh "$W/ns.pid" "$DISPLAY_NUM" >"$W/xvfb.log" 2>&1 &
	sleep 2
	ns_pid >/dev/null || { cat "$W/xvfb.log" >&2; exit 1; }
}

in_ns() {
	nsenter --preserve-credentials -U -n -t "$(ns_pid)" "$@"
}

case ${1:-} in
setup)
	mkdir -p "$W/debs" "$ROOT"
	(cd "$W/debs" && apt-get download "${PACKAGES[@]}")
	for deb in "$W"/debs/*.deb; do dpkg-deb -x "$deb" "$ROOT"; done
	# Debian's wineserver looks for its NLS files in <datadir>/wine/nls.
	ln -sfn . "$ROOT/usr/share/wine/wine"
	# user32 needs zlib1.dll from libz-mingw-w64. Wine loads it as a native DLL, from its own DLL folders
	# while it creates the prefix and from the prefix afterwards, so the first boot ends in an error.
	ln -sf "$ROOT/usr/x86_64-w64-mingw32/lib/zlib1.dll" "$LIB/wine/x86_64-windows/zlib1.dll"
	ln -sf "$ROOT/usr/i686-w64-mingw32/lib/zlib1.dll" "$LIB/wine/i386-windows/zlib1.dll"
	ensure_ns
	in_ns wine wineboot -i || true
	in_ns wineserver -w
	cp "$ROOT/usr/x86_64-w64-mingw32/lib/zlib1.dll" "$WINEPREFIX/drive_c/windows/system32/"
	cp "$ROOT/usr/i686-w64-mingw32/lib/zlib1.dll" "$WINEPREFIX/drive_c/windows/syswow64/"
	in_ns wine wineboot -u
	in_ns wineserver -w
	mkdir -p "$WINEPREFIX/drive_c/scratch"
	# Keep the prefix away from the rest of the disk: no Z: drive for /, no links into $HOME.
	rm -f "$WINEPREFIX/dosdevices/z:"
	find "$WINEPREFIX/drive_c/users/$USER" -maxdepth 1 -type l | while read -r link; do
		rm "$link"
		mkdir "$link"
	done
	echo "Wine reference ready in $W"
	;;
start)
	folder=${2:?data folder}
	exe=${3:?executable name}
	shift 3
	ensure_ns
	scratch=$WINEPREFIX/drive_c/scratch/$(basename "$folder")
	[[ -d $scratch ]] || cp -r "$folder" "$scratch"
	cd "$scratch"
	in_ns wine "$exe" "$@" >"$W/run.log" 2>&1 &
	# Xvfb has no window manager, and the game places its window partly off screen: move it to 0,0.
	# It appears once the game's startup sound has played, after about half a minute.
	for _ in $(seq 1 120); do
		sleep 1
		if in_ns uv run -q --offline --with python-xlib python3 -I - "$exe" <<'EOF'; then
import sys
from Xlib import display
d = display.Display()
for w in d.screen().root.query_tree().children:
    name = w.get_wm_name()
    if w.get_attributes().map_state == 2 and name and name.startswith('Full Tilt!'):
        w.configure(x=0, y=0)
        d.sync()
        print(f'{name} on {d.get_display_name()}')
        sys.exit(0)
sys.exit(1)
EOF
			exit 0
		fi
	done
	echo "no game window after 120 s; see $W/run.log" >&2
	exit 1
	;;
exec)
	shift
	in_ns "$@"
	;;
stop)
	if ns_pid >/dev/null; then
		in_ns wineserver -k || true
		kill "$(ns_pid)"
	fi
	rm -f "$W/ns.pid"
	;;
*)
	sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
	exit 1
	;;
esac
