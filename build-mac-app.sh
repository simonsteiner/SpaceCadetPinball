#!/usr/bin/env bash

set -xe

# SDL is fetched and linked statically by the macos preset
cmake --preset macos
cmake --build --preset macos --parallel "$(sysctl -n hw.ncpu)"

sw_version='2.1.1'

mkdir -p SpaceCadetPinball.app/Contents/MacOS
mkdir -p SpaceCadetPinball.app/Contents/Resources

cp -a Platform/macOS/Info.plist SpaceCadetPinball.app/Contents/
cp -a Platform/macOS/SpaceCadetPinball.icns SpaceCadetPinball.app/Contents/Resources/
cp -a bin/SpaceCadetPinball SpaceCadetPinball.app/Contents/MacOS/

sed -i '' "s/CHANGEME_SW_VERSION/$sw_version/" SpaceCadetPinball.app/Contents/Info.plist

echo -n "APPL????" > SpaceCadetPinball.app/Contents/PkgInfo

hdiutil create -fs HFS+ -srcfolder SpaceCadetPinball.app -volname "SpaceCadetPinball $sw_version" "SpaceCadetPinball-$sw_version-mac.dmg"

rm -r SpaceCadetPinball.app
