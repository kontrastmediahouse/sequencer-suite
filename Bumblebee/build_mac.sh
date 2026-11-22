#!/usr/bin/env bash
# Build Bumblebee (VST3 + AU + Standalone) on macOS, Apple Silicon native.
# Installs to ~/Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/Components
set -euo pipefail
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode Command Line Tools are missing. Installing (re-run this script when done)..."
  xcode-select --install
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is missing. Install it with:  brew install cmake   (or from cmake.org)"
  exit 1
fi

ARCHS="${ARCHS:-arm64}"          # ARCHS="arm64;x86_64" ./build_mac.sh  for a universal build

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

echo "Running engine tests..."
./build/bee_test

# Ad-hoc sign the installed copies so hosts load them without complaints
for p in "$HOME/Library/Audio/Plug-Ins/VST3/Bumblebee.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Bumblebee.component"; do
  [ -d "$p" ] && codesign --force --deep -s - "$p" && xattr -dr com.apple.quarantine "$p" 2>/dev/null || true
done

echo
echo "Done. Installed:"
echo "  ~/Library/Audio/Plug-Ins/VST3/Bumblebee.vst3"
echo "  ~/Library/Audio/Plug-Ins/Components/Bumblebee.component"
echo "  Standalone app: build/Bumblebee_artefacts/Release/Standalone/Bumblebee.app"
echo "Restart Cubase (or rescan plug-ins in Studio > VST Plug-in Manager)."
