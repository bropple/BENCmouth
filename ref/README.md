# Reference material

Source material for BENCmouth, with provenance. **Read this before adding anything here.**

BENCmouth is an original work. It is not a port, decompilation, or transliteration of
S.A.M. (Software Automatic Mouth) or of any other existing synthesizer. Everything in
this directory is either public domain, permissively licensed, or a citation-only pointer.

## What's here

| File | What it is | Provenance |
| --- | --- | --- |
| `NRL-7948-TRANS.SNO` | Original SNOBOL4 source of `TRANS`, containing the complete 329-rule English letter-to-sound rule set from NRL Report 7948 | US Naval Research Laboratory, 1976. Work of the US federal government — **public domain** |
| `cmudict-0.7b.txt` | CMU Pronouncing Dictionary, ~134k words with ARPABET pronunciations and stress marks | Carnegie Mellon University — **2-clause BSD** |
| `cmudict-LICENSE.txt` | The CMUdict license text | Must be retained; see below |

### Attribution obligations

CMUdict's BSD license requires that we reproduce its copyright notice and disclaimer in
both source and binary distributions. Concretely: keep `cmudict-LICENSE.txt` in the repo,
and reproduce it in `NOTICE` (or equivalent) in any binary release. The NRL material
carries no obligation, but cite it anyway — it's good manners and it documents our
clean-room story.

## Cited but deliberately not vendored

**Klatt, D. H. (1980).** "Software for a cascade/parallel formant synthesizer."
*Journal of the Acoustical Society of America* 67(3), 971–995.

The canonical description of the cascade/parallel formant topology, including the full
block diagram and parameter set. This is the paper BENCmouth's synthesis core is designed
from. It is **copyrighted by the Acoustical Society of America** and is not redistributable,
so it is not checked in — obtain it through a library or the ASA. Implement from the block
diagrams and parameter tables; that is a legitimate independent implementation.

**Klatt, D. H. & Klatt, L. C. (1990).** "Analysis, synthesis, and perception of voice
quality variations among female and male talkers." *JASA* 87(2), 820–857.
Extends the 1980 synthesizer with the voice-quality parameters (open quotient, spectral
tilt, breathiness, flutter) that make a voice sound like a *particular* voice rather than
a generic buzz. Also copyrighted.

**Holmes, J. N. (1983).** "Formant synthesizers: cascade or parallel?" *Speech
Communication* 2(4), 251–273. The other major lineage — pure-parallel synthesis. Worth
reading for the argument against the cascade branch.

**Elovitz, H. S., Johnson, R. W., McHugh, A., & Shore, J. E. (1976).** "Automatic
Translation of English Text to Phonetics by Means of Letter-to-Sound Rules."
NRL Report 7948, Naval Research Laboratory, Washington, D.C. (AD/A021 929).
The prose report accompanying `NRL-7948-TRANS.SNO`. Public domain, but DTIC blocks
non-browser downloads, so fetch it by hand if you want it:
<https://apps.dtic.mil/sti/pdfs/ADA021929.pdf>

## Explicitly off-limits

Do not add, read, or consult while implementing:

- The S.A.M. 6502 binary, its disassembly, or any annotated disassembly of it.
- `s-macke/SAM` or any other decompilation-derived C port of S.A.M.
- Dennis Klatt's `KLSYN` reference implementation, or the widely-circulated `klatt.c`
  descendants (Iles/Ing-Simmons and friends). Their licensing is murky at best, and more
  importantly, reading an implementation is how a clean-room design stops being one.

The distinction that matters: **reading a published description of an algorithm and
implementing it is independent creation; reading someone's code and rewriting it is
derivation.** Papers and rule sets in, source code out.

## The rule-set format

`NRL-7948-TRANS.SNO` encodes each rule as `left [ match ] right = / phonemes /`, where the
bracketed part is consumed on a successful match. The context patterns use these classes
(from the header comment of the file itself):

```
#  one or more vowels          ^  a single consonant
*  one or more consonants      +  a front vowel: E, I, Y
.  a voiced consonant          :  zero or more consonants
$  single consonant + I or E   &  a sibilant
%  a suffix: E, ES, ED, ER, ING, ELY
@  a consonant after which long U is /UW/ (rule) not /YUW/ (mule)
```

Rules are grouped by the first letter of the match (`ARULE.ENG` … `ZRULE.ENG`) plus
`PUNCTRULE.ENG` and `NUMBERRULE.ENG`, and within a group are tried in order, first match
wins. This maps directly onto a table-driven matcher in C — see `src/core/bm_lts.c`.

The rules produce roughly 90% word accuracy on running text, which is why CMUdict is here
too: dictionary lookup first, rules as the fallback for anything not in it.
