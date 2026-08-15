A formant speech synthesizer in C99. Text in, speech out.

An original work in the spirit of S.A.M., built from the published literature on
cascade/parallel formant synthesis rather than from any existing implementation.
See `ref/README.md` for provenance, including the sources deliberately not consulted.

## New in 0.3.1

**The GUI opens ten and a half seconds faster on Windows**, on the machines where
that was happening. Nothing was wrong with the synthesizer: raylib wakes GLFW's
joystick subsystem just before it creates the window, and on Windows that
enumerates every game controller the machine has ever had attached — present or
not, each answered by its own driver at its own pace. A rig with a lot of things
plugged into it pays for all of them, before the window appears. This program has
never read a joystick, so it no longer asks.

If a raylib program is slow to open on your machine, Windows' own Game Controllers
panel (`joy.cpl`) will be slow too — the same enumeration, with no raylib anywhere
near it.

**The multisampling hint is gone** along with it. Measured at 703 pixels of a
972,800 pixel window, seven hundredths of one per cent, because this interface is
rectangles and text. It also asks the driver to go looking for a multisampled
pixel format at startup, which some are slow about.

Neither touches the synthesizer, the CLI, or any plugin format — the core does
not link raylib at all.

## New in 0.3.0

**The piano roll and the plugin are more experimental than the rest of this.** The
synthesizer, the CLI and the first two tabs have been through several releases; these
have been through one. Expect rough edges, and expect some of this to change shape.
Songs are readable `.bmsong` text, so nothing you write is trapped in a format that
only this version understands.

**A piano roll, and a plugin.**

The GUI has a third tab: notes on a grid, a syllable typed into each, lengths dragged.
It is honest about time in a way the score tab could not be — `[dur N]` makes a note last
N milliseconds *including its consonants*, and where a syllable will not fit, the note
overruns and says so in amber rather than quietly pushing everything after it late.
`TIE` slurs: the note carries on the vowel already sounding and glides onto its pitch,
with the word's closing consonants moved to the end of the slur, which is what a singer
does with "straight" held over two notes.

**Editing it.** Ctrl+click selects more than one note — drag them together and they keep
their spacing, or take exactly two and `TIE` joins them, the later one giving up its word
to carry the earlier one's vowel. Right-click a note for a menu: delete it, send it up or
down an octave, move it to a named pitch. Drag either edge to change where a note starts
or how long it lasts, with the cursor saying so before you press. Ctrl+Z and Ctrl+Y, with
a drag or a typed word counting as one step rather than sixty. Zoom in and out, and the
grid takes whatever height the window has. Dragging a note up or down says it at the new
pitch as it passes, so choosing a note by ear does not mean letting go to hear it.

**The voice's pitch sliders go dim on a score that names its own notes.** They were live
controls that did nothing: `[note]` is a key on the keyboard rather than a shift of the
voice, and the roll writes one on every note, so `pitch` and `range` decided nothing while
the effects beside them went on working. Both rows now say so. `[pitch]` is the other
thing — it moves the line without replacing the voice underneath — and leaves them live.

**Fixed: a word too long for one note was cut mid-phoneme.** A note holds one syllable,
but the word box takes more, and "extraordinary" spells to 33 characters against a buffer
of 32. The tail was dropped wherever it landed, which is the bad part — the pieces ARPABET
leaves behind are mostly still phonemes, `NG` cut short is `N` and `TH` is `T`, so the
note went on singing with a sound in it nobody chose. It now stops at a phoneme boundary
and says the word did not fit.

**`BENCmouth.clap`** — the same song inside a DAW. It is a score player: the plugin owns
the song, the host owns the transport, and pressing play, scrubbing or looping is followed
exactly, because the score is rendered ahead of time and the host's playhead indexes into
it. A project file stores the song as readable `.bmsong` text.

**CLAP, VST3 and — on macOS — AU.** The VST3 and the AU are shims that load the CLAP, so
they always install together with it; a shim on its own appears in a host's list and then
fails to load, which looks like a broken plugin rather than a missing one.

- **Windows**: the installer offers *CLAP plugin* and *VST3 plugin* as components. Ticking
  the VST3 ticks the CLAP.
- **macOS**: a second disk image, `bencmouth-plugins-*-macos-universal.dmg`, with every
  format in it and an **Install Plug-Ins** script that puts each where its host looks. It
  carries the application too — the plugin opens its window by starting it. Universal and
  ad-hoc signed; not notarized, so Gatekeeper wants telling once.
- **Linux**: `BENCmouth.clap` into `~/.clap`, `BENCmouth.vst3` into `~/.vst3`, or
  `make clap-install vst3-install`.

**The plugin's window is the BENCmouth application**, started by the plugin and talking to
it through a block they share. It opens on the project's song and publishes every edit
back. That it is a separate process is forced rather than chosen: raylib keeps its window
in one file-scope global, so a process gets exactly one however many instances a host
loads — and a crashing editor does not take the DAW with it.

**Fixed: the installer could crash while adding `bm` to PATH.** The buffer it appends the
directory to was allocated with no room for it — "the size of your PATH, plus zero" — so
every `/PATH` install wrote about fifty bytes past the end of a heap block, and whether
that crashed depended on how the allocator had rounded the block and what happened to be
next to it. On a profile with no PATH at all it allocated nothing and copied the whole
directory into it.

The cause was one register. The size of the slack was worked out into `$0`, handed to a
macro, and read back after two `System::Call`s had used `$0` for their return codes — so
by the time it was used it was the number zero. The macro now captures its argument on its
first line, before anything can overwrite it, which is the property it should have had:
the same macro was correct all along when the uninstaller passed it a literal.

CI now runs the installer under full page heap, which puts an inaccessible page
immediately after every allocation — so this class of bug stops being a matter of luck and
becomes an access violation on the instruction that causes it. Every existing PATH check
passed while this bug shipped; that is what the new one is for.

## New in 0.2.4

**The Windows installer can put `bm` on your PATH.** A tickbox, off by default, and the
uninstaller takes the entry out again — exactly the one it added, leaving everything
else in the string alone. `setup.exe /S /PATH` does it unattended.

0.2.2 shipped without this on the grounds that a stock NSIS holds a string in 1024
characters, so reading a long PATH and writing it back truncates it. That reasoning was
right about the danger and wrong about the conclusion: the value never has to enter an
NSIS string at all. It is read into memory, edited there and written back, so its length
stops being a factor. It also writes your *account's* PATH rather than the machine's,
which is the shorter string and the recoverable mistake.

CI installs the built `setup.exe` on a real Windows runner, checks that a default
install leaves PATH untouched, that `/PATH` adds exactly one entry and that installing
twice does not add it twice, and that after uninstalling PATH is byte-for-byte what it
was. That last one is the assertion worth having — building an installer proves nothing
about what it does to a registry.

## New in 0.2.3

**There is a vocoder.** A proper channel vocoder at the end of the signal path, not an
imitation of one: sixteen third-octave channels measure how loud the voice is in each
band, and a carrier generated inside the stage is cut into the same sixteen bands and
turned up or down to match. Nothing of the input reaches the output except those sixteen
numbers, which is why what comes out has the *words* and the *carrier's* pitch. A vocoder
is a monotone by construction, and that is most of why it sounds like a machine.

Two controls, `vocoder` and `vocoder_hz`, and two presets: `Vocoder` on its own, and
`Chorale` an octave lower with a room around it. The chain now runs **ring → comb →
chorus → drive → vocoder → crush → echo → reverb** — it goes last of the things done to
the voice as a voice, because everything before it survives to the output only as
spectrum. A ring modulator ahead of it is audible, a chorus ahead of it very nearly is
not, and drive ahead of it is the useful one: the harmonics it adds are what open the top
channels.

The carrier turns itself into noise when the voice stops being voiced, decided from the
same sixteen measurements — the share of energy above 2.5 kHz runs 0.03 or less on vowels
and nasals against 0.56 to 0.91 on the voiceless fricatives, which is two orders of
magnitude and does not need a clever detector. Without it an `/s/` arrives as a buzz at
the carrier pitch, which is the oldest complaint there is about vocoders.

Nearly every constant in it is a measurement rather than a choice. Three sections per
channel rather than two, because a channel's filter skirts are the steepest spectrum it
can report and a shallower one reproduces something brighter than it was given — 4-pole
came out +1.9 dB/octave wrong, 6-pole +0.7, and 8-pole also +0.7 for a third more
arithmetic. The carrier is a differentiated sawtooth with a per-band weight of
`sqrt(f_low/fc)`, because measured through the bank a sawtooth arrives 17 dB down across
the range and its derivative 16 dB up, and a carrier has to sit at the geometric middle of
those. Both halves of the carrier are held to unit RMS and the output is scaled by the
square root of the sample rate, without which a pitch control was also a volume control
and a 300 Hz tone peaked at 1.28 at 8 kHz against 0.55 at 44.1.

It costs 96 biquads a sample — by a distance the most expensive thing in the library,
against five for the entire vocal tract — and 272 floats of state, with no delay line at
all. Like the rest of the stage it disappears under `-DBM_WITH_EFFECTS=0`, and an all-zero
chain is still a bit-for-bit bypass, so `BENCmouth Retro` has not moved.

Known and recorded rather than hidden: on high-pitched voices it runs 1 to 3 dB hot and
its peaks reach the limiter. A high fundamental puts at most one harmonic in each low
channel, so more of the bank is driven independently and more of it lines up. Correcting
that would need a function of the input's pitch, which is exactly the thing a vocoder is
built not to know; `level` is the control for it.

**Fixed: saving a song dropped six of its effect settings.** `ring_drift`, `echo`,
`echo_ms`, `reverb`, `reverb_size` and `level` were never written to a `.bmsong` — the
list was fixed in place before those parameters existed and nothing went back to it — so a
song saved with a room on it loaded back without one, silently. The same fault the voice
file had, found the same way and only once something saved a file and read it back field
by field. The song tests do that now, so the next parameter added cannot go quiet there
either.

## New in 0.2.2

**Windows has an installer.** `bencmouth-VERSION-windows-setup.exe` puts BENCmouth in
Program Files for everyone on the machine, adds a Start Menu entry and, if you tick the
box, a desktop shortcut, and uninstalls from Apps & Features like anything else. Run it
over an older BENCmouth and it replaces that install rather than sitting beside it —
including the voices and songs folders, so a voice withdrawn in a later release actually
goes away instead of surviving forever because nothing overwrote it.

The shortcuts start in your Documents rather than in Program Files. Loading looks beside
the executable either way, but Save Voice and Save WAV open wherever the program started,
and Program Files is not a folder you can write to. It does not touch your PATH: a stock
NSIS holds a string in 1024 characters, and on a machine with a lot of software installed
what would get written back is a *truncated* PATH. The portable `.zip` is still published
for anyone who would rather not install anything.

**Fixed: a formant above what the sample rate can represent is now bypassed rather than
folded down onto the top of the band.** These sections are normalised to unity gain at
DC, which is what lets five of them chain without a makeup stage — and that is only
harmless while the poles stay away from Nyquist. Swept, it was a slope and not a cliff:
peak 0.6 at 22050 and 11025, 2.1 at 8000, 458 at 7000, 1968 at 6000. Finite, and all of
it wrong. The threshold is a measured 0.40 × the sample rate rather than a round number
that looks like one.

Inert at every rate the project ships — the highest formant in the table is about
3020 Hz against a limit of 8820 at 22050 — so 22050 and 16000 render bit-identically to
before and the pinned `BENCmouth Retro` reference does not move. Every rate from 4 kHz up
now lands between 0.55 and 0.64.

**Fixed: the GUI's ⓘ panel was reporting the wrong version.** It said 0.1.3 in both 0.2.0
and 0.2.1 — the constant in the public header and the release tag were two independent
facts, and nothing compared them. Now something does, and a tag that disagrees with the
source fails the release before anything is published.

## New in 0.2.1

**Song mode's tempo is a control, and it now does something.** It was displayed and
inert — nothing outside the readout read it. It means one thing now: the tempo the
score's `[hold]` values are written at. Change it and every `[hold]` is rewritten by the
same ratio, so the song genuinely speeds up or slows down and the file stays honest — its
header and its holds always agree. There is a slider for the quarter note in milliseconds
too, since that is the number you actually type into a `[hold]`; it and the BPM are one
value in two units.

Songs do not scale exactly with it, because consonants keep their own length. Daisy Bell
runs 11.9 s at 116 BPM and 9.9 s at 160. That is what singing does as well — a note's
duration lives in its vowel, which is why `[hold]` only touches vowels.

**`make all` builds everything**: dictionary, live audio, GUI and wasm, each in its best
configuration, testing for raylib, clang and the ALSA headers rather than assuming them
and reporting whatever it skips. A plain `make` is unchanged and still needs nothing but
a C compiler.

**Fixed:** the minimum window size had been about twelve pixels shorter than the layout
needed, so at the smallest size the status line — the one row that says what the program
is doing — was cut off the bottom.

## New in 0.2.0

It sings, it has an effects rack, and there are twenty-five voices instead of ten.

**Song mode.** A second tab in the GUI: a score of phonemes with `[note]` and `[hold]`
in it, a word-to-phoneme translator with an INSERT button so you do not have to know
ARPABET by heart, a reference for the notation, and save/load of `.bmsong` files. Each
tab keeps its own voice, because singing wants prosody off and a little vibrato and
speech wants the opposite. On the command line, `bm -S songs/daisy.bmsong -a`.

**A post-synthesis effects stage**, kept in its own struct beside the voice rather than
bolted onto it — an effect is not a property of a speaker, and keeping them apart is what
lets any chain go on any voice. Nine controls: ring modulation with a slowly drifting
carrier, a resonant comb, a three-tap chorus, drive, sample-rate crush, echo, reverb, and
an output level. Fifteen presets from `Metal` to `Hall`. All of it compiles away under
`-DBM_WITH_EFFECTS=0`, and an all-zero chain is a bit-for-bit bypass, which is what lets
the pinned voice survive a new stage in the signal path.

**Twenty-five voices.** The classic-era set — deep, professional, child, whispering,
unstable — plus ten that carry an effects chain and arrive with it: `Zarvek`, `Sentry`,
`Aggressor`, `Carillon`, `Harmonium`, `Diver`, `Foreman`, `Emissary`, `Trinode`, `Gravel`.
`CLASSIC-VOICES.md` covers what the engine can reach, what it cannot, and why.

**The excitation is selectable.** `source` crossfades the vocal folds into a harmonic
pipe or the measured partial series of a church bell. The vocal tract is untouched, so a
bell-sourced voice still has formants and still says the words — which is exactly what
the classic instrument voices were.

**New voice parameters**, all defaulting to off: `whisper` (voicing traded for
turbulence), `vibrato` / `vibrato_rate`, `flatten` (one pitch for the whole utterance —
`BENCmouth Monotone` was not actually monotone before this), and `source`.

**The GUI grew up.** Sliders whose readouts you can type into, with arrows that step by
the precision shown. A voice list that is sorted and scrolls. An effects column that
scrolls. Selectable, copyable read-only boxes. And a meter that shows RMS as well as
peak, because this synthesizer routinely makes them disagree — `Aggressor` peaks four
times lower than `Gravel` and is a decibel *quieter*, since drive collapses the crest
factor. `bm` reports both figures too.

**Voice files are now `.bmvoice`**, matching `.bmsong`. Contents are unchanged and the
loader never looked at the extension, so anything you saved under the old name still
loads with `-f`.

## What is in the box

Two archives per desktop platform. The CLI on its own is small and has no icon,
because nobody double-clicks a console program. The GUI is a separate download.

| Archive | Contains |
|---|---|
| `bencmouth-VERSION-PLATFORM` | the `bm` CLI, with live audio and the 124,910-word CMU dictionary |
| `bencmouth-gui-VERSION-PLATFORM` | `bencmouth-gui` and the CLI beside it (macOS: a `.dmg`, drag to Applications) |
| `bencmouth-VERSION-windows-setup.exe` | the same GUI and CLI, installed |
| `bencmouth-VERSION-wasm` | `bencmouth.wasm` and its JavaScript wrapper |

**On Windows there is now an installer.** It puts BENCmouth in Program Files for every
user on the machine, adds a Start Menu entry and, if you want one, a desktop shortcut,
and uninstalls from Apps & Features like anything else. Run it over an older BENCmouth
and it replaces that install rather than sitting beside it — including the voices and
songs folders, so a voice withdrawn in a later release actually goes away.

There is a tickbox for putting `bm` on your PATH, off by default, and the uninstaller
takes the entry out again. It edits your account's PATH rather than the machine's, and
`setup.exe /S /PATH` does the same unattended. The `.zip` is still published for anyone
who would rather unpack a folder than install anything.

```
./bm "Hello world" -o hello.wav
./bm -a "straight out of the speakers"
./bm -v retro -s 0.8 "I am sorry Dave"
./bm -m -P "[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0"   # it sings
```

**On macOS the GUI is a `.dmg`.** Mount it and drag BENCmouth to Applications. Inside is
`BENCmouth.app` — a bundle, because that is the only way a macOS program gets an icon:
a bare Mach-O executable has nothing to attach one to and GLFW's Cocoa backend ignores
the request. The bundle is a thin wrapper; `Contents/MacOS` holds one file.

macOS binaries here are unsigned in the sense that matters to Apple — there is no
Developer ID and no notarization — so Gatekeeper will stop the first launch. The app *is*
ad-hoc signed, which is what keeps the message honest: you should see "Apple could not
verify..." with an Open Anyway button under System Settings → Privacy & Security, not
"damaged and can't be opened".

If you do see **damaged**, the signature failed to validate rather than merely being
untrusted, and either of these fixes it:

```
xattr -dr com.apple.quarantine /Applications/BENCmouth.app   # drop the download flag
codesign --force --sign - /Applications/BENCmouth.app        # or re-sign it yourself
```

Both work on the CLI too (`./bm`). Nothing here can remove the prompt entirely — that
needs a paid Developer ID and notarization through Apple.

**The GUI is a single self-contained executable.** The font, the window icon, the
wordmark and the licence texts are compiled into it, so it can be put anywhere and
started from anywhere and still look right. There is no assets folder to keep beside
it. The ⓘ button in the top right shows the copyright and every licence, read out of
the binary.

## Notable

- **No dynamic allocation, no libm in the core, no I/O below the host layer.** The
  engine holds all its state in caller-supplied storage: 19 KB without the effects
  stage, 75 KB with it, since the echo and the reverb are delay lines.
- **BENCmouth Retro is pinned.** Every naturalness feature is a voice parameter whose
  off setting reproduces the older behaviour, and a golden-reference test holds the
  original voice to it.
- **The dictionary is a switch, not a rebuild.** The DICT button turns it off so you
  can hear the letter-to-sound rules alone — which is what a build for a
  microcontroller has, and how you find the words worth an exception.
- **`-DBM_FIXED_POINT=1`** runs the sample loop in Q18 integer arithmetic for targets
  without an FPU — 52.9 dB SNR against the float reference.
- The `wasm32` build is bare: no libc, no Emscripten runtime.

## Licensing

MIT for the source. `NOTICE` covers the third-party material and must ship with any
redistribution: the CMU Pronouncing Dictionary is 2-clause BSD and requires its notice
in binary form, the NRL letter-to-sound rules are US federal government work and public
domain, the GUI embeds Terminus (TTF) under the SIL Open Font License, and it links
raylib under the zlib licence. The GUI carries all of these inside the executable and
displays them, which is what makes shipping it as a lone file permissible.
