#!/bin/sh
#
# Build a drag-to-Applications disk image around an existing .app.
#
# A .app is a directory that Finder draws as a single icon. That works, but it
# survives a download only if whatever unpacked the archive kept it intact and
# the person on the other end knows not to open it. A .dmg removes the question:
# it mounts as a window with the application on one side and a shortcut to
# /Applications on the other, and the only sensible thing to do with it is the
# right one.
#
#   tools/macos-dmg.sh <app> <out.dmg> [volume name]
#
# Run on macOS: hdiutil has no equivalent elsewhere.

set -eu

APP="$1"
OUT="$2"
VOL="${3:-BENCmouth}"

STAGE="$(mktemp -d)/dmg"
mkdir -p "$STAGE"

cp -R "$APP" "$STAGE/"

# The whole point of the layout. A symlink to /Applications is what makes the
# drag work; without it the window is just a folder with an app in it.
ln -s /Applications "$STAGE/Applications"

# Anything the user should read before dragging, and nothing they have to keep.
for f in LICENSE NOTICE; do
    [ -f "$f" ] && cp "$f" "$STAGE/"
done

rm -f "$OUT"
hdiutil create \
    -volname "$VOL" \
    -srcfolder "$STAGE" \
    -fs HFS+ \
    -format UDZO \
    -ov \
    "$OUT"

echo "built $OUT"
