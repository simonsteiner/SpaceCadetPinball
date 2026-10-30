#!/usr/bin/env bash
# Makes a test build that carries the game data of all four tables, so a device needs nothing but the build.
# A test build holds Microsoft's files: it stays on this machine and the player's devices, copied over USB or the
# local network, and is never committed, uploaded, attached to a pull request or made in CI (docs/deployment.md).
#
# Usage: tools/make-test-build.sh windows [--msvc] | web | macos
#   windows  one executable with the data inside, unpacked into the preferences folder at its first start.
#            Cross-compiled with MinGW (the mingw preset; x86_64-w64-mingw32-gcc on PATH), or with --msvc, from
#            WSL, with the Windows host's Visual Studio in a folder under its %TEMP%.
#   web      a folder to serve over HTTP: the page, the game, its data preloaded from SpaceCadetPinball.data, and
#            the MIT SoundFont the published page offers. Needs EMSDK set (tools/install-emsdk.sh).
#   macos    a DMG with the app, built on a Mac with the macos preset; the data goes into the app's
#            Contents/Resources, which the game searches as the executable's folder, and the app is ad-hoc signed.
#
# It builds the committed HEAD, from a copy made with git archive, so uncommitted changes are not in the build.
# The data comes from the workspace's sibling folders; set these to use others, or to an empty value to leave a
# table out:
#   SCP_DATA_3DPB         3D Pinball for Windows (PINBALL.DAT)           default ../gamedata
#   SCP_DATA_FULLTILT     Full Tilt!'s Space Cadet (CADET.DAT)           default ../gamedata-fulltilt
#   SCP_DATA_DRAGON       Full Tilt!'s Dragon's Keep (DRAGON.DAT)        default ../gamedata-dragon
#   SCP_DATA_SKULDUGGERY  Full Tilt!'s Skulduggery (PIRATES.DAT)         default ../gamedata-skulduggery
# The build goes to $SCP_TESTBUILDS/<commit>/<platform>/ and is built in $SCP_TESTBUILDS/build/; SCP_TESTBUILDS
# defaults to ../testbuilds, and neither may be inside a Git repository. SCP_CMAKE_ARGS adds configure arguments; each
# build configures afresh, so arguments from an earlier run don't stay in the cache (the downloaded SDL does).
# It runs with macOS's bash 3.2 and BSD tools as well as on Linux.
set -euo pipefail

usage() {
	echo "Usage: $0 windows [--msvc] | web | macos" >&2
	exit 2
}

if [ -n "${CI:-}" ]; then
	echo "Test builds carry game data and are never made in CI." >&2
	exit 1
fi
[ $# -ge 1 ] || usage
platform=$1
shift
msvc=false
for arg in "$@"; do
	case $arg in
		--msvc) [ "$platform" = windows ] || usage; msvc=true ;;
		*) usage ;;
	esac
done
case $platform in
	windows | web) ;;
	macos)
		if [ "$(uname -s)" != Darwin ]; then
			echo "The macos test build is made on a Mac" >&2
			exit 1
		fi
		;;
	*) usage ;;
esac

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
# The main checkout too, when this is one of its worktrees
main_repo=$(cd "$(git -C "$repo" rev-parse --path-format=absolute --git-common-dir)/.." && pwd -P)
repo=$(cd "$repo" && pwd -P)
workspace=$(cd "$repo/.." && pwd -P)
commit=$(git -C "$repo" rev-parse --short=10 HEAD)
describe=$(git -C "$repo" describe --always --tags HEAD)
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
	echo "Warning: uncommitted changes are not in the build; it is made from HEAD ($describe)." >&2
fi

# Made absolute through its parent, which has to exist, since macOS has no realpath -m
testbuilds=${SCP_TESTBUILDS:-$workspace/testbuilds}
if ! parent=$(cd "$(dirname "$testbuilds")" 2>/dev/null && pwd -P); then
	echo "No folder $(dirname "$testbuilds") for SCP_TESTBUILDS" >&2
	exit 1
fi
testbuilds=$parent/$(basename "$testbuilds")
out=$testbuilds/$commit/$platform
build=$testbuilds/build
# Neither may end up where git could pick it up
for dir in "$out" "$build"; do
	for checkout in "$repo" "$main_repo"; do
		case $dir/ in
			"$checkout"/*)
				echo "Refusing to write game data inside the repository: $dir" >&2
				exit 1
				;;
		esac
	done
done
mkdir -p "$out" "$build"
for dir in "$out" "$build"; do
	if git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		echo "Refusing to write game data inside a Git repository: $dir" >&2
		exit 1
	fi
done

upper() {
	printf '%s' "$1" | tr '[:lower:]' '[:upper:]'
}

# The file in a folder whose name matches, in any letter case; fails when there is none
find_file() {
	[ -d "$1" ] && find "$1" -maxdepth 1 -type f -iname "$2" -print -quit | grep .
}

# add_table <data folder> <folder in the layout, or .> <DAT file> <sound folder, or .> <music track...>
# Copies what the game reads of one table, in upper case as on the CDs: its DAT file, its picture, the WAV files the
# DAT file names, and the music tracks midi.cpp loads, each as MIDI when there is one, else MDS.
add_table() {
	local data=$1 dest=$layout/$2 dat=$3 sound=$4
	shift 4
	local dat_path sound_data sound_dest file name track
	if ! dat_path=$(find_file "$data" "$dat"); then
		echo "No $dat in $data" >&2
		exit 1
	fi
	mkdir -p "$dest"
	cp "$dat_path" "$dest/$dat"
	if file=$(find_file "$data" "${dat%.*}.BMP"); then
		cp "$file" "$dest/${dat%.*}.BMP"
	fi
	sound_data=$data
	sound_dest=$dest
	if [ "$sound" != . ]; then
		sound_data=$(find "$data" -maxdepth 1 -type d -iname "$sound" -print -quit)
		if [ -z "$sound_data" ]; then
			echo "No $sound folder in $data" >&2
			exit 1
		fi
		sound_dest=$dest/$sound
		mkdir -p "$sound_dest"
	fi
	# Names in the DAT file are in lower case, as "sound1.wav"
	find "$sound_data" -maxdepth 1 -type f -iname '*.wav' -print0 | while IFS= read -r -d '' file; do
		name=$(basename "$file")
		if grep -qaiF -- "$name" "$dat_path"; then
			cp "$file" "$sound_dest/$(upper "$name")"
		fi
	done
	for track in "$@"; do
		if file=$(find_file "$sound_data" "$track.MID") || file=$(find_file "$sound_data" "$track.MDS"); then
			name=$(basename "$file")
			cp "$file" "$sound_dest/$(upper "$name")"
		else
			echo "Warning: no music $track in $sound_data" >&2
		fi
	done
}

# The layout the game searches: both Space Cadets at the top, where "Prefer 3DPB Data" chooses between them, since
# a CADET folder under 3D Pinball's would never be reached; Dragon's Keep and Skulduggery in their Full Tilt! folders
layout=$build/layout
if [ -d "$layout" ]; then
	rm -rf -- "${layout:?}"
fi
mkdir -p "$layout"
data_3dpb=${SCP_DATA_3DPB-$workspace/gamedata}
data_fulltilt=${SCP_DATA_FULLTILT-$workspace/gamedata-fulltilt}
data_dragon=${SCP_DATA_DRAGON-$workspace/gamedata-dragon}
data_skulduggery=${SCP_DATA_SKULDUGGERY-$workspace/gamedata-skulduggery}
tables=()
if [ -n "$data_3dpb" ]; then
	add_table "$data_3dpb" . PINBALL.DAT . PINBALL
	# Only for the font of its messages, which the game reads out of it; it never runs it
	if file=$(find_file "$data_3dpb" PINBALL.EXE); then
		cp "$file" "$layout/PINBALL.EXE"
	else
		echo "Warning: no PINBALL.EXE in $data_3dpb; 3D Pinball's messages fall back to the ImGui font" >&2
	fi
	tables+=("3D Pinball's Space Cadet")
fi
if [ -n "$data_fulltilt" ]; then
	add_table "$data_fulltilt" . CADET.DAT SOUND TABA1 TABA2 TABA3
	tables+=("Full Tilt!'s Space Cadet")
fi
if [ -n "$data_dragon" ]; then
	# DRAGON.EXE never loads TABC3, so neither does the game
	add_table "$data_dragon" DRAGON DRAGON.DAT SOUND TABC1 TABC2 TABC4 TABC5 TABC6 TABC7
	tables+=("Dragon's Keep")
fi
if [ -n "$data_skulduggery" ]; then
	add_table "$data_skulduggery" PIRATE PIRATES.DAT SOUND TABB1 TABB2 TABB3 TABB4 TABB5 TABB6
	tables+=("Skulduggery")
fi
if [ ${#tables[@]} -eq 0 ]; then
	echo "No game data: every SCP_DATA_* is empty" >&2
	exit 1
fi
names=$(printf '%s, ' "${tables[@]}")
echo "Game data: $(find "$layout" -type f | wc -l | tr -d ' ') files, $(du -sh "$layout" | cut -f1), for ${names%, }"

# The committed source, outside the repository, so its bin/ is too
src=$build/src
if [ -d "$src" ]; then
	rm -rf -- "${src:?}"
fi
mkdir -p "$src"
git -C "$repo" archive --format=tar HEAD | tar -x -C "$src"

# The configure arguments every test build gets, and SCP_CMAKE_ARGS's, split into words
# shellcheck disable=SC2206 # SCP_CMAKE_ARGS is a list of words
cmake_args=("-DSCP_BUILD_COMMIT=$describe" ${SCP_CMAKE_ARGS:-})

build_windows_mingw() {
	if ! command -v x86_64-w64-mingw32-gcc >/dev/null; then
		echo "No x86_64-w64-mingw32-gcc on PATH (docs/setup.md, Windows); or use --msvc from WSL" >&2
		exit 1
	fi
	local dir=$build/windows-mingw
	cmake --fresh -S "$src" --preset mingw -B "$dir" "-DSCP_BUNDLED_DATA=$layout" "${cmake_args[@]}"
	cmake --build "$dir" --parallel
	cp "$src/bin/SpaceCadetPinball.exe" "$out/SpaceCadetPinball.exe"
}

# From WSL: builds in the Windows host's %TEMP%, since MSBuild can't work in a WSL path, with the newest Visual Studio
# that has the C++ tools, as the windows-x64 preset does
build_windows_msvc() {
	if ! command -v cmd.exe >/dev/null || ! command -v wslpath >/dev/null; then
		echo "--msvc builds from WSL with the Windows host's Visual Studio" >&2
		exit 1
	fi
	local vswhere="/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" vs win_temp dir
	vs=$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
		-property installationPath | tr -d '\r')
	if [ -z "$vs" ]; then
		echo "No Visual Studio with the C++ tools on the Windows host" >&2
		exit 1
	fi
	win_temp=$(cd /mnt/c && cmd.exe /c echo %TEMP% | tr -d '\r')
	dir=$(wslpath -u "$win_temp")/scp-testbuild
	echo "Building in $win_temp\\scp-testbuild, which holds a copy of the game data until you delete it"
	mkdir -p "$dir"
	for sub in src layout; do
		if [ -d "$dir/$sub" ]; then
			rm -rf -- "${dir:?}/$sub"
		fi
		cp -r "$build/$sub" "$dir/$sub"
	done
	# CMake takes forward slashes; a backslash in a path it globs would escape the next character
	local win_dir cmake_exe
	win_dir=$(wslpath -m "$dir")
	cmake_exe="$vs\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
	printf '%s\r\n' "@echo off" \
		"\"$cmake_exe\" --fresh -S \"$win_dir/src\" -B \"$win_dir/build\" -A x64 -DCMAKE_WIN32_EXECUTABLE=ON -DSCP_VENDORED_SDL=ON ^" \
		"  \"-DSCP_BUNDLED_DATA=$win_dir/layout\" \"-DSCP_BUILD_COMMIT=$describe\" ${SCP_CMAKE_ARGS:-} || exit /b 1" \
		"\"$cmake_exe\" --build \"$win_dir/build\" --config Release --parallel || exit /b 1" >"$dir/build.cmd"
	(cd /mnt/c && cmd.exe /c "$(wslpath -w "$dir/build.cmd")")
	cp "$dir/src/bin/Release/SpaceCadetPinball.exe" "$out/SpaceCadetPinball.exe"
}

# The SoundFont the published page downloads, pinned as in .github/workflows/Pages.yml
soundfont_base=https://raw.githubusercontent.com/musescore/MuseScore/v2.3.2/share/sound
soundfont_files=(
	"FluidR3Mono_GM.sf3 cfcd66d89e8386823400eca64934b14fbea7bf48ba1f00d21189af1262794ec2"
	"FluidR3Mono_License.md 09926f9451f751408abd65162c99343f840234d2a708584808fd46427633063f"
)

# macOS has shasum but not always sha256sum
sha256() {
	if command -v sha256sum >/dev/null; then
		sha256sum "$1"
	else
		shasum -a 256 "$1"
	fi | cut -d ' ' -f 1
}

build_web() {
	if [ -z "${EMSDK:-}" ]; then
		echo "Set EMSDK to an Emscripten SDK (tools/install-emsdk.sh)" >&2
		exit 1
	fi
	local dir=$build/web entry name hash
	cmake --fresh -S "$src" --preset web -B "$dir" "-DSCP_BUNDLED_DATA=$layout" "${cmake_args[@]}"
	cmake --build "$dir" --parallel
	cp "$src/bin/SpaceCadetPinball.html" "$out/index.html"
	cp "$src/bin/SpaceCadetPinball.js" "$src/bin/SpaceCadetPinball.wasm" "$src/bin/SpaceCadetPinball.data" "$out/"
	# Downloaded once into the build folder
	mkdir -p "$build/soundfont"
	for entry in "${soundfont_files[@]}"; do
		name=${entry% *}
		hash=${entry#* }
		if [ ! -f "$build/soundfont/$name" ] || [ "$(sha256 "$build/soundfont/$name")" != "$hash" ]; then
			curl -sSfL --proto =https --proto-redir =https -o "$build/soundfont/$name" "$soundfont_base/$name"
			if [ "$(sha256 "$build/soundfont/$name")" != "$hash" ]; then
				echo "$name doesn't match its pinned SHA-256" >&2
				exit 1
			fi
		fi
		cp "$build/soundfont/$name" "$out/$name"
	done
}

# On the Mac: the app as build-mac-app.sh makes it, with the data in Contents/Resources, which SDL_GetBasePath returns
# for an app, signed ad hoc after the data is in, so macOS asks only once (docs/deployment.md)
build_macos() {
	local dir=$build/macos app=$build/SpaceCadetPinball.app version dmg
	cmake --fresh -S "$src" --preset macos -B "$dir" "${cmake_args[@]}"
	cmake --build "$dir" --parallel "$(sysctl -n hw.ncpu)"
	version=$(sed -n "s/^sw_version='\(.*\)'$/\1/p" "$src/build-mac-app.sh")
	if [ -z "$version" ]; then
		echo "No sw_version='...' line in build-mac-app.sh" >&2
		exit 1
	fi
	if [ -d "$app" ]; then
		rm -rf -- "${app:?}"
	fi
	mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
	sed "s/CHANGEME_SW_VERSION/$version/" "$src/Platform/macOS/Info.plist" >"$app/Contents/Info.plist"
	cp "$src/Platform/macOS/SpaceCadetPinball.icns" "$app/Contents/Resources/"
	cp "$src/bin/SpaceCadetPinball" "$app/Contents/MacOS/"
	printf 'APPL????' >"$app/Contents/PkgInfo"
	cp -R "$layout/." "$app/Contents/Resources/"
	codesign --force --sign - "$app"
	codesign --verify --strict "$app"
	dmg=$out/SpaceCadetPinball-$commit-mac.dmg
	hdiutil create -fs HFS+ -srcfolder "$app" -volname "SpaceCadetPinball test build" -ov "$dmg"
}

case $platform in
	macos)
		build_macos
		echo "Test build $describe: $out/SpaceCadetPinball-$commit-mac.dmg"
		echo "Copy the app from it to the Mac and start it; the first start asks once, as for any app without a" \
			"Developer ID (System Settings > Privacy & Security > Open Anyway, the context menu's Open before" \
			"macOS 15, or xattr -d com.apple.quarantine on the app). Never upload it."
		;;
	web)
		build_web
		echo "Test build $describe: $out/"
		echo "Serve it over HTTP, since browsers don't run it from file://, for example with" \
			"python3 -m http.server --directory '$out' 8000, and open http://localhost:8000/." \
			"For a phone, add --bind with this machine's address on the local network. Never upload it."
		;;
	windows)
		if $msvc; then
			build_windows_msvc
		else
			build_windows_mingw
		fi
		echo "Test build $describe: $out/SpaceCadetPinball.exe"
		echo "Copy it to the Windows device over USB or the local network and run it; its first start unpacks the" \
			"data into %APPDATA%\\SpaceCadetPinball. Never upload it."
		;;
esac
