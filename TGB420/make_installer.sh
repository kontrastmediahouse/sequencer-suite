#!/usr/bin/env bash
# Builds TGB-420 Sequencer and packages it as  dist/TGB-420-Sequencer-<version>.dmg
# The DMG contains a standard macOS installer (.pkg) that installs:
#   VST3 -> /Library/Audio/Plug-Ins/VST3/TGB-420 Sequencer.vst3
#   AU   -> /Library/Audio/Plug-Ins/Components/TGB-420 Sequencer.component
#   App  -> /Applications/TGB-420 Sequencer.app   (standalone)
#
# Optional code signing / notarisation (needs a paid Apple Developer account):
#   export DEV_ID_APP="Developer ID Application: Your Name (TEAMID)"
#   export DEV_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
#   export NOTARY_PROFILE="tgb-notary"   # created once with: xcrun notarytool store-credentials
# Without these, the installer is unsigned: it works on your Mac; on other Macs users
# must right-click the .pkg > Open (or allow it in System Settings > Privacy & Security).
set -euo pipefail
cd "$(dirname "$0")"

VERSION="$(grep -Eo 'project\(TGB420 VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')"
ARCHS="${ARCHS:-arm64;x86_64}"   # universal by default, so the installer works on any Mac
BUILD=build-installer
STAGE="$BUILD/stage"
DIST=dist

command -v cmake >/dev/null || { echo "CMake missing: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Run: xcode-select --install"; exit 1; }

NAME="TGB-420 Sequencer"
echo "==> Building $NAME $VERSION ($ARCHS)"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
      -DTGB_COPY_AFTER_BUILD=OFF
cmake --build "$BUILD" --config Release -j "$(sysctl -n hw.ncpu)"
"$BUILD/engine_test"

ART="$BUILD/TGB420_artefacts/Release"
rm -rf "$STAGE" && mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Applications" "$DIST"
cp -R "$ART/VST3/$NAME.vst3"           "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/$NAME.component"        "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/$NAME.app"      "$STAGE/Applications/"

echo "==> Signing bundles"
SIGN_ID="${DEV_ID_APP:--}"   # "-" = ad-hoc signature
for b in "$STAGE/Library/Audio/Plug-Ins/VST3/$NAME.vst3" \
         "$STAGE/Library/Audio/Plug-Ins/Components/$NAME.component" \
         "$STAGE/Applications/$NAME.app"; do
  if [ "$SIGN_ID" = "-" ]; then
    codesign --force --deep -s - "$b"
  else
    codesign --force --deep --options runtime --timestamp -s "$SIGN_ID" "$b"
  fi
done

echo "==> Building installer package"
COMPONENT_PLIST="$BUILD/components.plist"
pkgbuild --analyze --root "$STAGE" "$COMPONENT_PLIST"
# Never let the installer "relocate" bundles to an old copy somewhere else on disk
n=0
while /usr/libexec/PlistBuddy -c "Print :$n" "$COMPONENT_PLIST" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Set :$n:BundleIsRelocatable false" "$COMPONENT_PLIST"
  n=$((n + 1))
done

pkgbuild --root "$STAGE" --component-plist "$COMPONENT_PLIST" \
         --identifier com.tgbport.tgb420.pkg --version "$VERSION" \
         --install-location / "$BUILD/TGB420-component.pkg"

PKG="$BUILD/TGB420 $VERSION.pkg"
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  productbuild --package "$BUILD/TGB420-component.pkg" --sign "$DEV_ID_INSTALLER" "$PKG"
else
  productbuild --package "$BUILD/TGB420-component.pkg" "$PKG"
fi

echo "==> Building DMG"
DMG_DIR="$BUILD/dmg"
rm -rf "$DMG_DIR" && mkdir -p "$DMG_DIR"
cp "$PKG" "$DMG_DIR/Install TGB-420 Sequencer.pkg"
cat > "$DMG_DIR/Read Me.txt" <<EOF
TGB-420 Sequencer $VERSION - stage sequencer (VST3 / AU / Standalone)

1. Double-click "Install TGB-420 Sequencer.pkg" and follow the steps.
2. Restart your DAW (Cubase: Studio > VST Plug-in Manager > rescan if needed).

Cubase: load TGB-420 Sequencer on an Instrument track, then set another
track's MIDI input to "TGB-420 Sequencer" and enable Monitor. Press Play.

Installs to:
  /Library/Audio/Plug-Ins/VST3/TGB-420 Sequencer.vst3
  /Library/Audio/Plug-Ins/Components/TGB-420 Sequencer.component
  /Applications/TGB-420 Sequencer.app

To uninstall, delete those three items.
EOF

DMG="$DIST/TGB-420-Sequencer-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "TGB-420 $VERSION" -srcfolder "$DMG_DIR" -fs HFS+ -format UDZO -ov "$DMG"
[ -n "${DEV_ID_APP:-}" ] && codesign --force --timestamp -s "$DEV_ID_APP" "$DMG"

if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${DEV_ID_INSTALLER:-}" ]; then
  echo "==> Notarising (takes a few minutes)"
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
fi

echo
echo "Done: $DMG"
