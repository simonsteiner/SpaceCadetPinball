#!/usr/bin/env bash
# Installs the pinned Emscripten SDK into the folder given (CI uses it; locally any emsdk of this version works).
# Usage: tools/install-emsdk.sh <folder>; then set EMSDK to that folder before configuring the web preset.
set -euo pipefail
dir=${1:?usage: tools/install-emsdk.sh <folder>}
version=6.0.12
commit=35ff8a6d150541276abbc6bae512ca90bcfbe220 # the emsdk tag $version

if [ ! -d "$dir/.git" ]; then
	git clone --quiet https://github.com/emscripten-core/emsdk.git "$dir"
fi
git -C "$dir" checkout --quiet "$commit"
"$dir/emsdk" install "$version"
"$dir/emsdk" activate "$version"
