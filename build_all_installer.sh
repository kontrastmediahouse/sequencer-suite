#!/usr/bin/env bash
# Builds all four plug-ins and packages them into ONE installer:
#     dist/Sequencer-Suite-<version>.dmg
#
#   SnakeSeq            (MDD Snake port)
#   TGB-420 Sequencer   (TGB-420 port)
#   SNAKE-420           (Snake + TGB-420 as linked modules)
#   Bumblebee           (Snake + TGB-420 fused into one sequencer)
#
# Each plug-in is installed as VST3 + AU + Standalone app. The installer has a
# "Customize" button, so you can untick any plug-in you don't want.
#
# Runs on any Mac with Xcode Command Line Tools and CMake, or on GitHub Actions
# (see .github/workflows/build-all.yml). JUCE 8.0.4 is downloaded once and shared.
#
# Environment (all optional):
#   ARCHS="arm64;x86_64"   default universal; ARCHS=arm64 builds faster (Apple Silicon only)
#   ONLY="Bumblebee TGB420" build just these (folder names)
#   DEV_ID_APP="Developer ID Application: Your Name (TEAMID)"
#   DEV_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
#   NOTARY_PROFILE="suite-notary"  (xcrun notarytool store-credentials)
# Without these, everything is ad-hoc signed: allow the installer once in
# System Settings > Privacy & Security > "Open Anyway".
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(pwd)"

SUITE_VERSION="${SUITE_VERSION:-1.0.0}"
ARCHS="${ARCHS:-arm64;x86_64}"
JUCE_TAG="8.0.4"
BUILD="$ROOT/build-suite"
STAGE_ROOT="$BUILD/stage"
PKGS="$BUILD/pkgs"
DIST="$ROOT/dist"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# folder | CMake target | product name | option prefix | package id
PLUGINS=(
  "SnakeSeq|SnakeSeq|SnakeSeq|SNAKE|com.snakeport.snakeseq"
  "TGB420|TGB420|TGB-420 Sequencer|TGB|com.tgbport.tgb420"
  "Snake420|SNAKE420|SNAKE-420|S420|com.s420port.snake420"
  "Bumblebee|Bumblebee|Bumblebee|BEE|com.beeworks.bumblebee"
)

command -v cmake >/dev/null || { echo "CMake missing: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Run: xcode-select --install"; exit 1; }

# ---- JUCE, once for all projects ---------------------------------------------------
if [ ! -f "$ROOT/JUCE/CMakeLists.txt" ]; then
  echo "==> Downloading JUCE $JUCE_TAG"
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$ROOT/JUCE"
fi

rm -rf "$STAGE_ROOT" "$PKGS"
mkdir -p "$PKGS" "$DIST"
PKG_ARGS=()
BUILT=()

for entry in "${PLUGINS[@]}"; do
  IFS='|' read -r DIR TARGET NAME PREFIX PKGID <<< "$entry"
  if [ -n "${ONLY:-}" ] && [[ " $ONLY " != *" $DIR "* ]]; then continue; fi

  echo
  echo "================================================================"
  echo "==> $NAME  ($DIR, $ARCHS)"
  echo "================================================================"
  B="$BUILD/$DIR"
  cmake -S "$ROOT/$DIR" -B "$B" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
        -DJUCE_DIR="$ROOT/JUCE" \
        -D"${PREFIX}_COPY_AFTER_BUILD"=OFF \
        -D"${PREFIX}_BUILD_TESTS"=ON
  cmake --build "$B" --config Release -j "$JOBS"

  echo "==> Tests for $NAME"
  ctest --test-dir "$B" -C Release --output-on-failure

  ART="$B/${TARGET}_artefacts/Release"
  S="$STAGE_ROOT/$DIR"
  mkdir -p "$S/Library/Audio/Plug-Ins/VST3" "$S/Library/Audio/Plug-Ins/Components" "$S/Applications"
  cp -R "$ART/VST3/$NAME.vst3"      "$S/Library/Audio/Plug-Ins/VST3/"
  cp -R "$ART/AU/$NAME.component"   "$S/Library/Audio/Plug-Ins/Components/"
  cp -R "$ART/Standalone/$NAME.app" "$S/Applications/"

  echo "==> Signing $NAME"
  SIGN_ID="${DEV_ID_APP:--}"
  for b in "$S/Library/Audio/Plug-Ins/VST3/$NAME.vst3" \
           "$S/Library/Audio/Plug-Ins/Components/$NAME.component" \
           "$S/Applications/$NAME.app"; do
    if [ "$SIGN_ID" = "-" ]; then
      codesign --force --deep -s - "$b"
    else
      codesign --force --deep --options runtime --timestamp -s "$SIGN_ID" "$b"
    fi
  done

  echo "==> Package for $NAME"
  PLIST="$BUILD/$DIR-components.plist"
  pkgbuild --analyze --root "$S" "$PLIST"
  n=0
  while /usr/libexec/PlistBuddy -c "Print :$n" "$PLIST" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Delete :$n:BundleIsRelocatable" "$PLIST" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :$n:BundleIsRelocatable bool false" "$PLIST"
    n=$((n + 1))
  done
  VERSION="$(grep -Eo 'project\([A-Za-z0-9_]+ VERSION [0-9.]+' "$ROOT/$DIR/CMakeLists.txt" | awk '{print $3}')"
  pkgbuild --root "$S" --component-plist "$PLIST" \
           --identifier "$PKGID.pkg" --version "${VERSION:-1.0.0}" \
           --install-location / "$PKGS/$DIR.pkg"
  PKG_ARGS+=(--package "$PKGS/$DIR.pkg")
  BUILT+=("$NAME")
done

[ ${#BUILT[@]} -gt 0 ] || { echo "Nothing built (check ONLY=...)"; exit 1; }

# ---- one installer with a choice per plug-in ---------------------------------------------
echo
echo "==> Building the suite installer"
DISTXML="$BUILD/distribution.xml"
productbuild --synthesize "${PKG_ARGS[@]}" "$DISTXML"
# show the "Customize" button so single plug-ins can be unticked; add a title
sed -i '' -e 's/customize="never"/customize="allow"/' "$DISTXML"
sed -i '' -e "s#<installer-gui-script minSpecVersion=\"\\([0-9]*\\)\">#<installer-gui-script minSpecVersion=\"\\1\">\\
    <title>Sequencer Suite $SUITE_VERSION</title>#" "$DISTXML"

PKG="$BUILD/Install Sequencer Suite.pkg"
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  productbuild --distribution "$DISTXML" --package-path "$PKGS" --sign "$DEV_ID_INSTALLER" "$PKG"
else
  productbuild --distribution "$DISTXML" --package-path "$PKGS" "$PKG"
fi

# ---- DMG -------------------------------------------------------------------------------
echo "==> Building DMG"
DMG_DIR="$BUILD/dmg"
rm -rf "$DMG_DIR" && mkdir -p "$DMG_DIR"
cp "$PKG" "$DMG_DIR/"
{
  echo "Sequencer Suite $SUITE_VERSION  (VST3 / AU / Standalone, macOS 11+)"
  echo
  echo "Contains:"
  for n in "${BUILT[@]}"; do echo "  - $n"; done
  cat <<'EOF'

INSTALL
1. Double-click "Install Sequencer Suite.pkg".
   macOS may block it the first time (it is not notarised): open
   System Settings > Privacy & Security, scroll down, click "Open Anyway".
2. Click "Customize" in the installer to skip any plug-in you don't want.
3. Start Cubase. If a plug-in is missing: Studio > VST Plug-in Manager > rescan.

USE IN CUBASE
- Load the plug-in on an INSTRUMENT track (it makes MIDI, not audio).
- On your synth's track set MIDI input to the plug-in's name and enable Monitor.
- Press Play.

INSTALLED TO
  /Library/Audio/Plug-Ins/VST3/<name>.vst3
  /Library/Audio/Plug-Ins/Components/<name>.component
  /Applications/<name>.app
To uninstall a plug-in, delete those three items.
EOF
} > "$DMG_DIR/Read Me.txt"

DMG="$DIST/Sequencer-Suite-$SUITE_VERSION.dmg"
rm -f "$DMG"
for try in 1 2 3; do   # hdiutil is sometimes "busy" on CI machines; retry
  hdiutil create -volname "Sequencer Suite $SUITE_VERSION" -srcfolder "$DMG_DIR" -fs HFS+ -format UDZO -ov "$DMG" && break
  [ "$try" = 3 ] && exit 1
  echo "hdiutil failed, retrying in 10 s"; sleep 10
done
if [ -n "${DEV_ID_APP:-}" ]; then codesign --force --timestamp -s "$DEV_ID_APP" "$DMG"; fi

if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${DEV_ID_INSTALLER:-}" ]; then
  echo "==> Notarising (a few minutes)"
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
fi

echo
echo "Done: $DMG"
echo "Contains: ${BUILT[*]}"
