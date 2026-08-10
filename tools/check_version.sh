#!/bin/sh
#
# The tag has to be the version the code thinks it is.
#
# include/bencmouth.h carries BM_VERSION_MAJOR/MINOR/PATCH, and the GUI's info
# panel prints them. Nothing compared that to the tag, so v0.2.0 and v0.2.1
# both shipped an About box reading "BENCmouth 0.1.3" - the constant had not
# moved since before either release, and there was no way to find that out
# except by opening the panel and looking.
#
#   tools/check_version.sh          print the version in the header
#   tools/check_version.sh v0.2.2   and require the tag to agree
#
set -eu
cd "$(dirname "$0")/.."

H=include/bencmouth.h

field() {
    v=$(sed -n "s/^#define BM_VERSION_$1[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p" "$H")
    [ -n "$v" ] || { echo "cannot read BM_VERSION_$1 out of $H"; exit 1; }
    echo "$v"
}

V="$(field MAJOR).$(field MINOR).$(field PATCH)"
echo "  $H  $V"

if [ $# -gt 0 ]; then
    want=${1#v}
    if [ "$want" != "$V" ]; then
        echo
        echo "  tag $1 does not match $H ($V)."
        echo "  bump the three constants, or tag v$V."
        exit 1
    fi
    echo "  ok  tag $1"
fi
