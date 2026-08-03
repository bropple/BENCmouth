# BENCmouth GUI — plan

A standalone desktop front end for BENCmouth, built to the BENCO design style guide.
Windows, Linux, macOS, single executable per platform.

This is a plan, not a commitment to every detail. The parts worth arguing about are
flagged as decisions rather than buried as assumptions.

---

## The constraint that drives everything

The style guide was written for HTML and Python surfaces, so parts of it do not transfer
directly. But the expensive part is not the aesthetic — it is the toolkit.

**Every mainstream GUI toolkit defaults to the opposite of this look.** Native widget sets
(Qt, GTK, wxWidgets, Win32 common controls) draw platform-native chrome, and bending them
toward a dark flat terminal means fighting them at every step: restyling scrollbars,
overriding focus rings, suppressing animations. The work is not "build the UI", it is
"un-build the toolkit's UI".

With an **immediate-mode renderer where we draw the widgets ourselves**, the calculation
inverts. A flat dark rectangle with a 1px border is what you get by default, because a
rectangle is what you were going to draw anyway. There are about a dozen distinct controls
here, and each is roughly fifteen lines. The BENCO palette and flatness are therefore close
to free — they are the path of least resistance, not a surcharge.

What *would* cost real effort, and is out of scope for now:

- scanlines, screen curvature, phosphor bloom, flicker — shader work, and the style guide
  explicitly warns off decorative effects anyway
- animated transitions and hover states
- the glow text-shadow on titles (cheap to fake, but it is a flourish, and skipping it
  costs nothing)

**Scope for the first version: the BENCO palette, Terminess, flat panels with thin dim
borders, and the H. Hex icon.** That is most of the guide's actual substance, and it
arrives essentially for free with the approach below. The CRT flourishes stay available
later if they turn out to be wanted.

---

## Toolkit — deliberately not chosen yet

**No toolkit is committed to, and no dependency is being added today.** BENCmouth has had
zero dependencies from the start, and that is worth keeping until there is a reason to
spend it. The analysis below is recorded so the decision can be made quickly later, not
because it has been made.

The rest of this document — layout, palette, font, icon, audio approach — holds regardless
of which renderer eventually draws it.

### Leading candidate: raylib

[raylib](https://www.raylib.com/) — zlib licensed, written in C99.

Why it would fit this project specifically:

- **It is C.** BENCmouth is C99 with no dependencies. Dear ImGui would mean introducing
  C++ and a second build discipline into a codebase that has neither.
- **One dependency covers everything we need**: window creation, input, immediate-mode
  drawing, TTF loading and rendering, and audio output. The alternative (Nuklear) needs a
  separate windowing backend and a separate audio library bolted on.
- **`DrawTextEx()` takes a letter-spacing argument**, which the style guide calls for on
  headings and labels. Most toolkits have no notion of it.
- **Static linking on all three platforms is well-trodden**, which is what "standalone
  executable" requires.
- **Audio is miniaudio underneath**, giving WASAPI / CoreAudio / ALSA / PulseAudio without
  us writing three backends.

Roughly 1 MB added to the binary, statically linked.

### Alternatives, and why not

| Option | Why not |
|---|---|
| **Dear ImGui** | Excellent, but C++ in a C project, and its aesthetic is tuned for debug tooling — restyling to BENCO is possible but constant |
| **Nuklear** | Pure C, single header, very themeable — genuinely close second. Loses on needing GLFW/SDL *and* an audio library assembled separately |
| **Qt / GTK / wxWidgets** | Large runtime dependencies, hard to ship standalone, and native widgets actively fight the style guide |
| **webview (HTML/CSS)** | Tempting: the style guide is *written* in CSS terms, so styling would be near-free. Fails the standalone test — Linux needs WebKitGTK present at runtime and Windows needs the WebView2 runtime. Reconsider only if "standalone" softens |
| **SDL2 + hand-rolled everything** | Maximum control, but that is what raylib already is, with the tedious parts done |

---

## Architecture

```
src/gui/
  main.c          window, event loop, layout
  bm_ui.c         the widget set - button, slider, text field, dropdown, meter
  bm_theme.c      BENCO palette and font handles, one place only
  bm_audio.c      raylib audio stream fed from bm_read()
  assets/         Terminess TTF + H. Hex icon, embedded as C arrays
```

**The GUI links against the public API only** — `bencmouth.h` and `libbencmouth.a`, exactly
as the CLI does. It never reaches into `src/core/`. That is not tidiness for its own sake:
if the GUI needs something the public API cannot express, that is a finding about the API,
and papering over it with an internal include would hide it.

`src/gui/` is a separate make target. The library, CLI and test builds must never gain a
raylib dependency — `make`, `make test` and `make check-freestanding` keep working with
raylib absent from the machine entirely.

### Audio: stream, don't pre-render

`bm_read()` was built as a pull interface precisely so it could feed an audio callback with
no ring buffer and no thread. The GUI should do exactly that — raylib's `AudioStream` asks
for a buffer, we call `bm_read()`, done.

This is worth insisting on for two reasons. It validates the API design under its intended
use, which nothing currently does. And it makes parameter changes audible *during* an
utterance, which turns voice tuning from "adjust, re-render, listen" into something you can
actually dial in by ear — the thing the whole voice system exists for.

A rolling copy of the last few thousand samples feeds the waveform display.

---

## Screen layout

One window, no modes, no tabs. Roughly 900×640, resizable with a sane minimum.

```
+--------------------------------------------------------------+
| [H]  B E N C M O U T H                  [icon + spaced title] |
|      formant speech synthesis                [dim subtitle]   |
+--------------------------------------------------------------+
|                                                              |
|  +--------------------------------------------------------+  |
|  | text to speak                                          |  |
|  | Hello world.                                           |  |
|  +--------------------------------------------------------+  |
|                                                              |
|  HH EH L OW W ER L D                    [phoneme readout]    |
|                                                              |
|  [ SPEAK ]  [ STOP ]            [ SAVE WAV ]                 |
|                                                              |
|  ------------------------------------------------------------|
|                                                              |
|  VOICE   [ BENCmouth Retro          v ]  [LOAD] [SAVE]       |
|                                                              |
|   pitch        ###########-------------   118 Hz             |
|   speed        ##############----------   1.00               |
|   throat       ##############----------   1.00               |
|   mouth        ##############----------   1.00               |
|   tilt         ########----------------   6.0                |
|   flutter      #####-------------------   0.30               |
|   breathiness  ------------------------   0.0                |
|   open quot.   ############------------   0.50               |
|   gain         ##############----------   1.00               |
|   coart.       ------------------------   0.00               |
|                                                              |
|  ------------------------------------------------------------|
|                                                              |
|  [~~~~~~~~ waveform ~~~~~~~~]        peak 0.55   22050 Hz    |
+--------------------------------------------------------------+
```

The phoneme readout under the text field is `bm_text_to_phonemes()` run live as you type.
It is cheap, it is the single most useful debugging surface the synthesizer has, and it
makes the letter-to-sound rules legible instead of mysterious when a word comes out wrong.

Dividers are **plain 1px horizontal rules in `#2a3a1e`** — the screen equivalent of the
receipt format's dash line. No boxes, no double rules.

---

## Applying the style guide

| Element | Value |
|---|---|
| Window background | `#0c1408` |
| Panel / input background | `#182010` |
| Border | 1px `#2a3a1e` |
| Primary text | `#cdeab0` |
| Secondary text / labels | `#8aa878` |
| Accent, slider fill, active button | `#78b946` |
| Pressed / edge | `#3f5c28` |
| Error text (bad voice file, unknown phoneme) | `#d84a3a` |
| Clip / limiter warning on the meter | `#e8b23d` |
| Corner radius | 3px, everywhere |

### Font: Terminess

One font throughout, at two or three sizes. A single well-chosen monospace does more for
this look than a font pairing does, and it sidesteps the guide's role-mixing rules
entirely.

**Licensing checks out.** Terminess is OFL-1.1 with Reserved Font Names — the rename from
"Terminus" exists specifically to satisfy that clause. Embedding it in a distributed binary
is permitted; the OFL text must ship alongside. Practical consequence: **embed it
unmodified.** Subsetting to shrink the binary would make us a derivative and put the RFN
question back on the table, and the whole font is small enough that it is not worth the
argument.

**It is a bitmap font underneath**, and that shapes how to use it. Terminus was designed at
fixed pixel sizes (12/14/16/18/20/22/24) and the TTF is an outline conversion of that
design. Rendered at arbitrary sizes with antialiasing it goes soft and slightly wrong;
rendered at native sizes with point filtering it is razor-sharp. So:

- pin the UI to native sizes rather than scaling continuously
- load with `LoadFontEx()` and set `TEXTURE_FILTER_POINT`, not bilinear
- for HiDPI, step to the next native size rather than scaling the current one

That constraint is worth accepting rather than fighting: crisp unantialiased text at fixed
sizes *is* the terminal look, arrived at honestly instead of simulated.

`letter-spacing: 1px` on labels and headings via `DrawTextEx()`'s spacing argument; never
on the text the user typed.

Explicitly not doing for now: hover animations, transitions, gradient fills, glow, drop
shadows, progress spinners. A long render shows the word `working` and nothing else.

---

## Icon

Source and generated assets live in `assets/icon/`, outside the gitignored `style/`, because
a shipped binary contains the icon anyway — keeping it private would be pretend privacy.
`style/` stays ignored for the design sources it holds.

```
assets/icon/benco-roster-hex.svg          source: H. Hex, roster colors
assets/icon/hex-{16..512}.png             runtime + Linux
assets/icon/bencmouth.ico                 Windows, 7 sizes, 16-256
```

H. Hex in his roster colors: `#d97a2b` fill, `#8a4d18` edge, with the alert-red visor
stripe. Regenerate with:

```
for s in 16 24 32 48 64 128 256 512; do
  rsvg-convert -w $s -h $s assets/icon/benco-roster-hex.svg -o assets/icon/hex-$s.png
done
magick assets/icon/hex-512.png \
  -define icon:auto-resize=256,128,64,48,32,24,16 assets/icon/bencmouth.ico
```

Per platform:

| Platform | Mechanism |
|---|---|
| Linux | `SetWindowIcon()` at startup from an embedded PNG; a `.desktop` file for the launcher |
| Windows | `.ico` compiled into the exe as a resource (`windres` under MinGW); also `SetWindowIcon()` for the titlebar |
| macOS | `.icns` inside the `.app` bundle. **Needs `iconutil` (macOS-only) or `png2icns` from icoutils** — cannot be generated here |

The ICO is ~370 KB because ImageMagick writes uncompressed entries and the 256×256 one
dominates. Fine for a desktop binary; if it grates, `icotool` can write PNG-compressed
entries, or drop the 256 size at the cost of high-DPI crispness in Explorer.

---

## Build and distribution

A `gui` target, independent of everything else:

```
make gui        # requires raylib; nothing else does
```

| Platform | Links against | Ships as |
|---|---|---|
| Linux | raylib static; X11 (or Wayland), GL, pthread dynamic | bare ELF, optionally an AppImage |
| macOS | raylib static; Cocoa, IOKit, CoreVideo, CoreAudio frameworks | `.app` bundle |
| Windows | raylib static; gdi32, winmm, opengl32 | single `.exe` |

Fonts embedded, so a distributed binary is genuinely one file on Windows and macOS.

---

## Risks and open decisions

**A dependency, in a project that has had none.** This is the real cost. Mitigated by
confining raylib to `src/gui/` and keeping every other target buildable without it, but it
is a change in the project's character and worth saying out loud rather than discovering
later.

**Font licensing.** Terminess is OFL-1.1-RFN. Embedding is permitted; ship `fonts/OFL.txt`
and reference it in the About panel, and embed the font **unmodified** so the Reserved Font
Name clause stays uncontroversial. **Consolas must not be bundled** — it is Microsoft
proprietary, and the style guide scopes it to a Windows-only app that relies on it already
being installed.

**Brand assets versus a gitignored `style/`.** Settled: icon source and generated sizes
live in `assets/icon/` and are committed; `style/` stays ignored for design sources. A
shipped binary contains the icon regardless, so keeping it out of the repo would have been
pretend privacy.

**Wayland vs X11 on Linux.** raylib supports both but they are separate build
configurations. Suggest defaulting to X11 for reach, since XWayland makes it work
everywhere, and revisiting if that ages badly.

**macOS distribution friction.** An unsigned `.app` gets a Gatekeeper warning. Fine for a
personal tool, worth knowing before shipping it to anyone else.

**Open question — live tuning while speaking.** `bm_engine_set_voice()` currently takes
effect at the next frame boundary, which is right for avoiding clicks. Whether dragging a
slider mid-utterance should retune the *current* utterance or only the next one is a
genuine design call, and it is easier to answer with a working slider in hand.

---

## Release builds

CI already builds and tests on Linux, macOS and Windows (`.github/workflows/ci.yml`), which
is what turns the README's platform claims into verified ones rather than asserted ones.
Extending that to produce downloadable binaries is a small step from there:

- tag-triggered job (`on: push: tags: ['v*']`)
- per-platform artifact: bare ELF, a `.app` bundle, and a `.exe`
- attach to a GitHub Release

Worth doing once the GUI exists and there is something a non-developer would want to
download. Until then the CLI is a `make` away and a release pipeline is ceremony.

Two things that will need deciding at that point: macOS binaries are unsigned unless an
Apple Developer account is involved, so users get a Gatekeeper warning; and if the GUI
statically links a toolkit, its license notice has to ship in the release alongside the
font's OFL text.

---

## Suggested order

1. Window, theme, Terminess, H. Hex icon. Nothing functional — get the aesthetic right first,
   because it is the thing most likely to need iteration and the cheapest to change early.
2. The widget set in `bm_ui.c`: button, slider, text field, dropdown, meter.
3. Text field + phoneme readout, wired to `bm_text_to_phonemes()`. No audio yet.
4. Audio streaming from `bm_read()`. **First real milestone** — type, press speak, hear it.
5. Voice sliders bound to `bm_voice`, plus preset dropdown and load/save.
6. Waveform, peak meter, WAV export.
7. Platform packaging.

Steps 1–4 are the ones that prove the concept. If the aesthetic and the live audio both
land, the rest is filling in controls against an API that already works.

---

## Before starting

Confirm the toolkit. raylib is the recommendation and the plan above assumes it, but it is
the one decision that is expensive to reverse once the widget set exists — everything after
step 2 is written against whatever drawing API we pick.
