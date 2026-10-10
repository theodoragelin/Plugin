#!/bin/bash
# Build Selene on this Mac and install it straight into Logic's plug-in folders.
# Usage (from the repo folder):   ./scripts/install-mac.sh
# First build takes a few minutes (downloads JUCE); after that ~30-60 s.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Installing Apple command line tools (accept the popup, then re-run this script)…"
  xcode-select --install || true
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then brew install cmake
  else echo "❌ cmake is missing. Install Homebrew (https://brew.sh) then run: brew install cmake"; exit 1; fi
fi

ARCH=$(uname -m)   # build only for this Mac's chip = faster
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH"
cmake --build build-mac --config Release --target Selene_AU Selene_VST3 -j "$(sysctl -n hw.ncpu)"

ART=build-mac/Selene_artefacts/Release
COMP=~/Library/Audio/Plug-Ins/Components
VST3=~/Library/Audio/Plug-Ins/VST3
mkdir -p "$COMP" "$VST3"
rm -rf "$COMP/Selene.component" "$VST3/Selene.vst3"
cp -R "$ART/AU/Selene.component" "$COMP/"
cp -R "$ART/VST3/Selene.vst3" "$VST3/"
codesign --force --deep --sign - "$COMP/Selene.component" "$VST3/Selene.vst3" 2>/dev/null || true
xattr -dr com.apple.quarantine "$COMP/Selene.component" "$VST3/Selene.vst3" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "✅ Selene installed. Quit & reopen Logic (or re-insert the plug-in) to hear the new version."
