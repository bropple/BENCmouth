#!/bin/sh
#
# Build the plug-in disk image: the CLAP, the VST3 and the AU if they were
# built, the editor beside them, and a script that puts each where its host
# looks.
#
# A separate image from the application's, because these install somewhere
# else. The .app goes to /Applications by dragging, which is a gesture everyone
# knows; a plug-in goes to ~/Library/Audio/Plug-Ins/<format>, which is a
# hidden directory with three subdirectories and no obvious gesture at all. So
# that part is a script somebody double-clicks.
#
#   tools/macos-plugin-dmg.sh <out.dmg> [volume name]
#
# Takes whatever has been built. Missing pieces are left out rather than faked.

set -eu

OUT="$1"
VOL="${2:-BENCmouth Plug-Ins}"
DMGBUILD="${DMGBUILD:-dmgbuild}"

APP=BENCmouth.app
CLAP=build/BENCmouth.clap
VST3=build/BENCmouth.vst3
AU=build/BENCmouth.component

STAGE="$(mktemp -d)/stage"
mkdir -p "$STAGE"

HAVE=""
[ -d "$APP" ]  && { cp -R "$APP"  "$STAGE/"; HAVE="$HAVE app"; }
[ -d "$CLAP" ] && { cp -R "$CLAP" "$STAGE/"; HAVE="$HAVE clap"; }
[ -d "$VST3" ] && { cp -R "$VST3" "$STAGE/"; HAVE="$HAVE vst3"; }
[ -d "$AU" ]   && { cp -R "$AU"   "$STAGE/"; HAVE="$HAVE au"; }
[ -n "$HAVE" ] || { echo "nothing to package - build something first" >&2; exit 1; }
echo "packaging:$HAVE"

# The editor is the .app, and the plugin looks for it beside itself and then in
# /Applications. Installing it there is what makes the plugin's window open.
ln -s /Applications "$STAGE/Applications"

cat > "$STAGE/Install Plug-Ins.command" <<'INSTALL'
#!/bin/sh
#
# Copies the plug-ins into your user plug-in folders and clears the quarantine
# flag macOS puts on anything downloaded. No administrator password: everything
# goes into your own Library.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"

CLAPDIR="$HOME/Library/Audio/Plug-Ins/CLAP"
VST3DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AUDIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$CLAPDIR" "$VST3DIR" "$AUDIR"

installed=""

if [ -d "$HERE/BENCmouth.clap" ]; then
    rm -rf "$CLAPDIR/BENCmouth.clap"
    cp -R "$HERE/BENCmouth.clap" "$CLAPDIR/"
    xattr -dr com.apple.quarantine "$CLAPDIR/BENCmouth.clap" 2>/dev/null || true
    installed="$installed\n  CLAP  -> $CLAPDIR"
fi

# The VST3 and the AU are shims: each one finds BENCmouth.clap at run time and
# loads it. Neither does anything without the CLAP, which is why this script
# installs whatever it has rather than offering a choice.
if [ -d "$HERE/BENCmouth.vst3" ]; then
    rm -rf "$VST3DIR/BENCmouth.vst3"
    cp -R "$HERE/BENCmouth.vst3" "$VST3DIR/"
    xattr -dr com.apple.quarantine "$VST3DIR/BENCmouth.vst3" 2>/dev/null || true
    installed="$installed\n  VST3  -> $VST3DIR"
fi

if [ -d "$HERE/BENCmouth.component" ]; then
    rm -rf "$AUDIR/BENCmouth.component"
    cp -R "$HERE/BENCmouth.component" "$AUDIR/"
    xattr -dr com.apple.quarantine "$AUDIR/BENCmouth.component" 2>/dev/null || true
    installed="$installed\n  AU    -> $AUDIR"
fi

# The editor. The plugin opens its window by starting this program, so a
# plugin without it plays a song it cannot show you.
#
# Always replaced, never skipped. BENCsynth's first version of this script
# installed the editor only when /Applications was empty of it, which meant
# every reinstall after the first silently kept the old one - and an old editor
# handed a flag it does not know is an editor that opens the wrong window. The
# plugin and the editor are one program in two files and they move together.
if [ -d "$HERE/BENCmouth.app" ]; then
    rm -rf /Applications/BENCmouth.app
    cp -R "$HERE/BENCmouth.app" /Applications/ 2>/dev/null || true
    xattr -dr com.apple.quarantine /Applications/BENCmouth.app 2>/dev/null || true
    installed="$installed\n  editor-> /Applications/BENCmouth.app"
fi

printf '\nInstalled:%b\n\n' "$installed"
printf 'Open your DAW and rescan its plug-ins.\n'
printf 'BENCmouth is an instrument - put it on an instrument track and press play.\n\n'
printf 'Press return to close this window.\n'
read -r _
INSTALL
chmod +x "$STAGE/Install Plug-Ins.command"

cp README.md LICENSE NOTICE "$STAGE/" 2>/dev/null || true

# Same styling path as the application image: dmgbuild writes the .DS_Store
# directly, because the usual way to get one is to mount the image and drive
# Finder over AppleScript, which needs a session no build runner has.
if command -v "$DMGBUILD" >/dev/null 2>&1; then
    "$DMGBUILD" -s tools/dmg-settings-plugins.py \
                -D srcdir="$STAGE" \
                "$VOL" "$OUT"
else
    echo "dmgbuild not found - building a plain image" >&2
    hdiutil create -volname "$VOL" -srcfolder "$STAGE" -ov -format UDZO "$OUT"
fi

echo "built $OUT"
