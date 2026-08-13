# dmgbuild settings for the BENCmouth plug-in disk image.
#
# The application image has two icons and an arrow drawn between them, because
# there is one gesture and the background can say what it is. This one has up
# to five things in it and the gesture is "double-click the script", so it is
# laid out as a list instead: a window somebody reads rather than one they drag
# across.
#
# See tools/dmg-settings.py for why dmgbuild writes the .DS_Store rather than
# Finder being asked to.
#
#   dmgbuild -s tools/dmg-settings-plugins.py -D srcdir=stage "BENCmouth Plug-Ins" out.dmg

import os

srcdir = defines.get("srcdir", "stage")               # noqa: F821 - dmgbuild

format = "UDZO"
compression_level = 9
size = None

# Whatever was staged, in a fixed order so the window reads the same however
# much of it was built. The installer script first: it is the thing to do.
_wanted = [
    "Install Plug-Ins.command",
    "BENCmouth.clap",
    "BENCmouth.vst3",
    "BENCmouth.component",
    "BENCmouth.app",
    "README.md",
    "LICENSE",
    "NOTICE",
]

files = [os.path.join(srcdir, n) for n in _wanted
         if os.path.exists(os.path.join(srcdir, n))]

symlinks = {"Applications": "/Applications"}

_icns = os.path.join(srcdir, "BENCmouth.app", "Contents", "Resources",
                     "BENCmouth.icns")
if os.path.exists(_icns):
    icon = _icns

# Two columns of a list, not a drag target. No background image: the
# application image's has an arrow painted on it pointing at /Applications,
# which would be pointing at the wrong thing here.
window_rect = ((200, 120), (620, 420))

_present = [n for n in _wanted if os.path.exists(os.path.join(srcdir, n))]
icon_locations = {}
for _i, _n in enumerate(_present):
    icon_locations[_n] = (150, 80 + _i * 62)
icon_locations["Applications"] = (440, 80)

default_view = "icon-view"
icon_size = 64
text_size = 12
show_icon_preview = False
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

arrange_by = None
grid_offset = (0, 0)
