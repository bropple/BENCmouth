# Brand assets

Everything in this directory is artwork the build reads. Nothing here is decorative:
each file is consumed by a script or compiled into a binary, and several have sizes
that are fixed by something outside this project rather than chosen.

This file exists because `tools/dmg-settings.py` points at it — the disk image's
layout and its background are two halves of one design, and neither can be changed
without the other.

## What is here

| File | Size | Read by | Notes |
|---|---|---|---|
| `BENCO_Logo_Terminal.png` | 1095 × 255 | `Makefile` (embedded in the GUI), `make-installer-art.sh` | White on transparent. The GUI draws it; the installer art trims and scales it. |
| `BENCO_Logo_README.png` | 640 × 169 | `README.md` | The masthead. Not embedded in anything. |
| `dmg-background.png` | 640 × 400 | `tools/dmg-settings.py`, `tools/macos-dmg.sh` | The mounted volume's window. **See below.** |
| `dmg-background@2x.png` | 1280 × 800 | `tools/macos-dmg.sh` | Retina pair for the above. |
| `nsis-welcome.bmp` | 164 × 314 | `tools/windows-installer.nsi` | Generated. 24-bit BMP. |
| `nsis-header.bmp` | 150 × 57 | `tools/windows-installer.nsi` | Generated. 24-bit BMP. |

## The disk image background and the icon positions are one thing

`dmg-background.png` has an arrow drawn on it, pointing from where `BENCmouth.app`
sits to where the `Applications` symlink sits. Those two positions are not in the
image — they are in `tools/dmg-settings.py`:

```python
window_rect    = ((200, 120), (640, 400))     # the window is exactly the image
icon_locations = {appname: (160, 250), "Applications": (480, 250)}
```

The window is sized to the image precisely so the drawn arrow lands between the two
real icons. **Move an icon and the arrow has to move with it**, and vice versa — there
is no mechanism that keeps them together, which is why this paragraph is here.

The `@2x` file is not optional polish. The window carries the wordmark and 17 px type,
and a 1× image on a Retina display is visibly soft on the one screen this whole
aesthetic was built for. `tools/macos-dmg.sh` packs both into a single multi-resolution
TIFF with `tiffutil`, which is how a background gets to be crisp on both; without
`tiffutil` it falls back to the 1× PNG.

## The two BMPs are generated, and committed anyway

`tools/make-installer-art.sh` regenerates `nsis-welcome.bmp` and `nsis-header.bmp`
from `BENCO_Logo_Terminal.png`, `../icon/hex-256.png` and `../fonts/TerminusTTF.ttf`.
Run it when the brand assets or the palette change, **look at what came out**, and
commit the result.

They are committed rather than built during the release for two reasons: the release
runner would need ImageMagick installed, and art that regenerates on every release is
art that can change without anyone seeing it.

Both sizes are fixed by NSIS's Modern UI 2 layout, not chosen — 164 × 314 for the panel
down the left of the first and last pages, 150 × 57 for the tile at the top right of
every page in between. Both must be **24-bit BMP**: a PNG is silently refused, and a
32-bit BMP renders with a black box where the alpha was, which is how you ship an
installer with a hole in it. `makensis -WX` is what turns that from a warning into a
failed build.

## The palette

Taken from `src/gui/bm_ui.c` so the installer, the disk image and the program itself
are the same green:

```
background  #0c1408
border      #2a3a1e
dim text    #8aa878
accent      #78b946
```

The installer's header tile sits on a white strip that MUI draws itself, with black
text on top. That is why the tile is dark-on-white by design and why
`MUI_BGCOLOR` is deliberately not set — it would paint the strip and leave MUI's own
black text unreadable on it.
