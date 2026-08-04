#!/bin/sh
#
# Wrap the GUI binary in a .app bundle.
#
# macOS is the one platform where the window icon cannot come from the program
# itself. Windows reads it from a resource compiled into the executable and X11
# takes it from _NET_WM_ICON, which the binary sets at startup - but GLFW's
# Cocoa backend ignores glfwSetWindowIcon entirely, because a bare Mach-O
# executable has no Finder or Dock identity to attach an icon to. The icon is a
# property of a bundle, so there has to be a bundle.
#
# Everything else the program needs is already inside the binary, which is what
# makes this a wrapper and not an install step: Contents/MacOS holds one file.
#
#   tools/macos-app.sh <binary> <output.app>
#
# Run on macOS: iconutil ships with the developer tools and has no equivalent
# elsewhere.

set -eu

BIN="$1"
APP="$2"
NAME="$(basename "$APP" .app)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/$NAME"
chmod +x "$APP/Contents/MacOS/$NAME"

# The iconset names are fixed; iconutil rejects anything it does not recognise.
# There is no 1024 px source, so 512x512@2x is simply absent - the set does not
# have to be complete, only correctly named.
ICONSET="$(mktemp -d)/icon.iconset"
mkdir -p "$ICONSET"
cp assets/icon/hex-16.png  "$ICONSET/icon_16x16.png"
cp assets/icon/hex-32.png  "$ICONSET/icon_16x16@2x.png"
cp assets/icon/hex-32.png  "$ICONSET/icon_32x32.png"
cp assets/icon/hex-64.png  "$ICONSET/icon_32x32@2x.png"
cp assets/icon/hex-128.png "$ICONSET/icon_128x128.png"
cp assets/icon/hex-256.png "$ICONSET/icon_128x128@2x.png"
cp assets/icon/hex-256.png "$ICONSET/icon_256x256.png"
cp assets/icon/hex-512.png "$ICONSET/icon_256x256@2x.png"
cp assets/icon/hex-512.png "$ICONSET/icon_512x512.png"

iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/$NAME.icns"

# The preset voice files travel inside the bundle. A .app is meant to be one
# self-contained thing a user drags to Applications; a folder of .voice files
# that had to stay next to it would defeat that, and LOAD knows to look here.
if [ -d voices ]; then
    cp -R voices "$APP/Contents/Resources/voices"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>              <string>$NAME</string>
    <key>CFBundleDisplayName</key>       <string>BENCmouth</string>
    <key>CFBundleExecutable</key>        <string>$NAME</string>
    <key>CFBundleIdentifier</key>        <string>net.ropple.bencmouth</string>
    <key>CFBundleIconFile</key>          <string>$NAME</string>
    <key>CFBundlePackageType</key>       <string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${BM_VERSION:-0.0.0}</string>
    <key>CFBundleVersion</key>           <string>${BM_VERSION:-0.0.0}</string>
    <key>LSMinimumSystemVersion</key>    <string>11.0</string>
    <!-- Without this the window is drawn at 1x and scaled up, which on a
         Retina display makes a bitmap font look exactly as bad as it sounds. -->
    <key>NSHighResolutionCapable</key>   <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
</dict>
</plist>
PLIST

echo "built $APP"
