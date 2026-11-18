#!/bin/bash
# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# PolyForm Noncommercial License 1.0.0: not for sale, and this notice stays in every copy.
# Packs dist/VPinballForGameVault_<version>_x64.appx for Xbox Dev Mode: the standalone build (Release_BGFX x64), our Xbox SDL3 (sdl3-uwp)
# in place of SDL364.dll, the Visual C++ runtime app-local, a test table; marks the exe AppContainer and signs it.
# xbox/package.sh <version>
set -e
V=${1:-0.1.0.0}
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(dirname "$REPO")
SDL3_UWP=${SDL3_UWP:-$ROOT/sdl3-uwp}
PFX_FILE=${PFX_FILE:-$ROOT/GameVault/cert/GameVault.pfx}
BIN=$REPO/.build/bin/vpx-standalone/Release_BGFX-x64
P=$REPO/.build/xbox-pkg
VC=$(ls -d "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/"*/bin/Hostx64/x64 | tail -1)
CRT=$(ls -d "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/"*/x64/Microsoft.VC143.CRT | tail -1)
SDK="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"

rm -rf "$P"; mkdir -p "$P/tables" "$P/licenses"
cp "$REPO/xbox/AppxManifest.xml" "$P/"
cp -r "$REPO/xbox/Assets" "$P/"
cp "$BIN/VPinballX_BGFXStandalone64.exe" "$P/"
for f in "$BIN"/*.dll; do
  case "$(basename "$f")" in
    SciLexerVP64.dll|D3DX9_4364.dll) ;;   # Windows editor / DirectX 9 only
    *) cp "$f" "$P/" ;;
  esac
done
cp -r "$BIN/assets" "$BIN/scripts" "$BIN/plugins" "$P/"
# Xbox SDL3: same functions as SDL 3.4, WinRT window + pads
cp "$SDL3_UWP/build-uwp/RelWithDebInfo/SDL3.dll" "$P/SDL364.dll"
cp "$SDL3_UWP/build-uwp/RelWithDebInfo/SDL3.dll" "$P/plugins/flexdmd/SDL364.dll"
for f in vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_atomic_wait.dll; do cp "$CRT/$f" "$P/"; done
cp "$REPO/tests/assets/test000-default-table.vpx" "$P/tables/"
cp "$REPO/docs/license.txt" "$P/licenses/VPinballX-license.txt"
cp "$SDL3_UWP/LICENSE.txt" "$P/licenses/SDL3-LICENSE.txt"
# the libraries inside, each with its own licence
EXT=$REPO/external/windows-x64/Release
for l in "bgfx/bgfx.cmake/bgfx/LICENSE:bgfx" "freeimage/freeimage/license-fi.txt:FreeImage" "libaltsound/libaltsound/LICENSE:libaltsound" \
         "libdmdutil/libdmdutil/LICENSE:libdmdutil" "libdof/libdof/LICENSE:libdof" "libmysofa/libmysofa/LICENSE:libmysofa" \
         "libpupdmd/libpupdmd/LICENSE:libpupdmd" "libserum/libserum/LICENSE.md:libserum" "libspatialaudio/libspatialaudio/LICENSE:libspatialaudio" \
         "libvni/libvni/LICENSE.md:libvni" "opus/ogg/COPYING:ogg" "pinmame/pinmame/LICENSE:PinMAME" "pinmame-data/gamedefs/LICENSE:pinmame-gamedefs" \
         "zlib/zlib/LICENSE:zlib" "ffmpeg/ffmpeg/LICENSE.md:FFmpeg" "ffmpeg/ffmpeg/COPYING.LGPLv2.1:FFmpeg-LGPL-2.1" "ffmpeg/ffmpeg/COPYING.GPLv2:FFmpeg-GPL-2"; do
  cp "$EXT/${l%%:*}" "$P/licenses/${l##*:}-license.txt"
done
# the VBScript engine is Wine's (LGPL 2.1), https://github.com/vpinball/libwinevbs
cp "$EXT/ffmpeg/ffmpeg/COPYING.LGPLv2.1" "$P/licenses/libwinevbs-Wine-LGPL-2.1.txt"
cat > "$P/licenses/SOURCE.txt" <<'SRC'
Game Vault · Visual Pinball is Visual Pinball X standalone (https://github.com/vpinball/vpinball), built for Xbox by
Skitzelles for Game Vault. The source of this build, with all changes: https://github.com/skitzellez/VPinball-GameVault
(branch gamevault). The libraries keep their own licences, in this folder.
SRC

# nothing from the build PC's folders may ship
PY=${PY:-/c/Users/GamerDad/AppData/Local/Programs/Python/Python312/python.exe}
"$PY" "$(cygpath -w "$REPO/xbox/scrub_paths.py")" "$(cygpath -w "$P")" "$(cygpath -w "$ROOT")"
"$VC/editbin.exe" //nologo //APPCONTAINER //SUBSYSTEM:WINDOWS "$P/VPinballX_BGFXStandalone64.exe"
sed -i "s/ Version=\"[0-9.]*\" ProcessorArchitecture/ Version=\"$V\" ProcessorArchitecture/" "$P/AppxManifest.xml"
mkdir -p "$REPO/dist"; rm -f "$REPO/dist/VPinballForGameVault_$V""_x64.appx"
"$SDK/makeappx.exe" pack //o //d "$(cygpath -w "$P")" //p "$(cygpath -w "$REPO/dist/VPinballForGameVault_$V""_x64.appx")" >/dev/null
PFX_PASSWORD=${PFX_PASSWORD:-$(/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -w "$ROOT/GameVault/pfx_password.ps1")" | tr -d '\r\n')}
"$SDK/signtool.exe" sign //fd SHA256 //a //f "$(cygpath -w "$PFX_FILE")" //p "$PFX_PASSWORD" "$(cygpath -w "$REPO/dist/VPinballForGameVault_$V""_x64.appx")" >/dev/null
ls -la "$REPO/dist/VPinballForGameVault_$V""_x64.appx"
