#!/bin/sh
#
# Build a drag-to-Applications disk image around an existing .app.
#
# A .app is a directory that Finder draws as a single icon. That works, but it
# only survives a download if whatever unpacked the archive kept it intact and
# the person on the other end knows not to open it. A tarball gives neither
# guarantee, and the first person to unpack one saw a folder called Contents
# and reasonably had no idea which part was the program.
#
# A .dmg removes the question. It mounts as a window with the application on
# one side and a shortcut to /Applications on the other, so the obvious gesture
# is the correct one and there is nothing to explain.
#
#   tools/macos-dmg.sh <app> <out.dmg> [volume name]
#
# Run on macOS: hdiutil, osascript and tiffutil have no equivalent elsewhere.
#
# The window styling is done the way every disk image does it, because there is
# only one way: mount a writable image, tell Finder where to put things, let it
# write a .DS_Store, then compress the result read-only. The layout is not
# stored in the image as data - it is stored in a file Finder writes when you
# ask it nicely.

set -eu

APP="$1"
OUT="$2"
VOL="${3:-BENCmouth}"
NAME="$(basename "$APP")"

WORK="$(mktemp -d)"
STAGE="$WORK/stage"
RW="$WORK/rw.dmg"
MNT="/Volumes/$VOL"

mkdir -p "$STAGE/.background"
cp -R "$APP" "$STAGE/"

# The symlink is the whole point of the layout. Without it the window is just a
# folder with an application in it and the drag has nowhere to go.
ln -s /Applications "$STAGE/Applications"

# Retina matters here: the background carries the wordmark and 17 px type, and
# a 1x image on a 2x display is visibly soft. tiffutil packs both resolutions
# into one file, which is how a background gets to be crisp on both.
BG=background.png
cp assets/brand/dmg-background.png "$STAGE/.background/background.png"
if command -v tiffutil >/dev/null 2>&1 && \
   [ -f assets/brand/dmg-background@2x.png ]; then
    tiffutil -cathidpicheck assets/brand/dmg-background.png \
             assets/brand/dmg-background@2x.png \
             -out "$STAGE/.background/background.tiff" >/dev/null 2>&1 && \
        BG=background.tiff
fi

# H. Hex on the mounted volume itself, in the sidebar and on the desktop.
if [ -f "$APP/Contents/Resources/BENCmouth.icns" ]; then
    cp "$APP/Contents/Resources/BENCmouth.icns" "$STAGE/.VolumeIcon.icns"
fi

# Writable and generously sized: Finder needs room to write .DS_Store, and an
# image sized exactly to its contents has none.
hdiutil create -srcfolder "$STAGE" -volname "$VOL" -fs HFS+ \
    -format UDRW -size 128m -ov "$RW" >/dev/null

hdiutil detach "$MNT" >/dev/null 2>&1 || true
hdiutil attach "$RW" -nobrowse -readwrite -noverify >/dev/null

# The icon coordinates are the background image's pixel coordinates, so the
# arrow drawn between them lands between them.
osascript <<APPLESCRIPT >/dev/null || echo "warning: could not style the window"
tell application "Finder"
    tell disk "$VOL"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 840, 520}
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 96
        set text size of opts to 12
        set background picture of opts to file ".background:$BG"
        set position of item "$NAME" of container window to {160, 250}
        set position of item "Applications" of container window to {480, 250}
        close
        open
        update without registering applications
        delay 2
    end tell
end tell
APPLESCRIPT

# The bit that tells Finder to use .VolumeIcon.icns rather than a blank disk.
if [ -f "$MNT/.VolumeIcon.icns" ] && command -v SetFile >/dev/null 2>&1; then
    SetFile -a C "$MNT" || echo "warning: could not set the volume icon bit"
fi

chmod -Rf go-w "$MNT" 2>/dev/null || true
sync
hdiutil detach "$MNT" >/dev/null

rm -f "$OUT"
hdiutil convert "$RW" -format UDZO -imagekey zlib-level=9 -o "$OUT" >/dev/null

echo "built $OUT"
