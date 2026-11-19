#!/usr/bin/env bash
# Kadran-linux-x64-<tag>.AppImage and .tar.gz from a Release build.
# usage: package-linux.sh <build dir> <output dir> <tag>   (QMAKE must point at the Qt to bundle)
set -euo pipefail
build=$(cd "$1" && pwd); out=$(mkdir -p "$2" && cd "$2" && pwd); tag=$3
root=$(cd "$(dirname "$0")/.." && pwd)
name=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["name"])' "$build/application.json")
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
app="$work/AppDir"
mkdir -p "$app/usr/bin" "$app/usr/share/applications" "$app/usr/share/icons/hicolor/256x256/apps"
cp "$build/$name" "$app/usr/bin/kadran"
cp "$root/packaging/kadran.desktop" "$app/usr/share/applications/kadran.desktop"
mkdir -p "$app/usr/share/doc/kadran" && cp "$root/LICENSE" "$app/usr/share/doc/kadran/LICENSE.txt"
python3 "$root/scripts/make-icon.py" "$app/usr/share/icons/hicolor/256x256/apps/kadran.png"

cd "$work"
base=https://github.com/linuxdeploy
for tool in linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
            linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage; do
  curl -fsSL --retry 4 -o "$(basename "$tool")" "https://github.com/$tool"
  chmod +x "$(basename "$tool")"
done
export APPIMAGE_EXTRACT_AND_RUN=1 PATH="$work:$PATH" LINUXDEPLOY_OUTPUT_VERSION="$tag" QMAKE=${QMAKE:?QMAKE is not set}
export EXTRA_QT_PLUGINS="platforms;platformthemes;xcbglintegration"
./linuxdeploy-x86_64.AppImage --appdir "$app" --plugin qt --output appimage \
  -d "$app/usr/share/applications/kadran.desktop" -i "$app/usr/share/icons/hicolor/256x256/apps/kadran.png"
ai=$(ls Kadran*.AppImage | head -1)
cp "$ai" "$out/Kadran-linux-x64-$tag.AppImage"

# The tar.gz: the same bundle, as a folder
mkdir -p "pkg/Kadran-$tag"
cp -a "$app"/. "pkg/Kadran-$tag/"
ln -sf usr/bin/kadran "pkg/Kadran-$tag/Kadran"
tar -C pkg -czf "$out/Kadran-linux-x64-$tag.tar.gz" "Kadran-$tag"
