/*
 * BENCmouth - the note roll
 * See bm_roll.h for what this is and why it only compiles one way.
 */

#include "bm_roll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Semitone offsets of the natural letters from C, indexed A..G. */
static const int SEMITONE[7] = { 9, 11, 0, 2, 4, 5, 7 };

/* The names of the twelve, sharps chosen. */
static const char *NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void bm_roll_init(bm_roll *r)
{
    if (r == 0) return;
    memset(r, 0, sizeof *r);
}

int bm_roll_length(const bm_roll *r)
{
    int i, end = 0;

    if (r == 0) return 0;
    for (i = 0; i < r->count; i++) {
        int e = r->note[i].start + r->note[i].length;
        if (e > end) end = e;
    }
    return end;
}

/* ------------------------------------------------------------------ *
 * Note names
 * ------------------------------------------------------------------ */

void bm_roll_note_name(int midi, char *out, size_t cap)
{
    int octave, pc;

    if (out == 0 || cap == 0) return;

    /* Clamped to the MIDI range rather than trusted. Every note that reaches
     * here has been through bm_roll_note_parse, which refuses anything outside
     * 12..108 - but this writes into an eight-byte buffer, and "the caller
     * checked" is not something a formatter should be relying on. */
    if (midi < 0)   midi = 0;
    if (midi > 127) midi = 127;

    octave = midi / 12 - 1;
    pc = midi % 12;
    snprintf(out, cap, "%s%d", NAMES[pc], octave);
}

int bm_roll_note_parse(const char *name)
{
    int  letter, accidental = 0, octave = 4, midi;
    size_t i = 0;

    if (name == 0) return -1;

    while (name[i] == ' ' || name[i] == '\t') i++;

    letter = name[i];
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    if (letter < 'A' || letter > 'G') return -1;
    i++;

    if (name[i] == '#' || name[i] == 's')      { accidental =  1; i++; }
    else if (name[i] == 'b' || name[i] == 'B') { accidental = -1; i++; }

    if (name[i] >= '0' && name[i] <= '9') {
        octave = name[i] - '0';
        i++;
    }
    while (name[i] == ' ' || name[i] == '\t') i++;
    if (name[i] != '\0') return -1;

    midi = (octave + 1) * 12 + SEMITONE[letter - 'A'] + accidental;
    /* The range [note] takes. Refusing here rather than at playback means a
     * file cannot carry a note the engine will not sing. */
    if (midi < 12 || midi > 108) return -1;
    return midi;
}

/* ------------------------------------------------------------------ *
 * Editing
 * ------------------------------------------------------------------ */

static void copy_field(char *dst, size_t cap, const char *src)
{
    if (src == 0) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

/* Insertion sort by start time, which is the right algorithm here and not a
 * concession: the roll is sorted at all times, so every call has at most one
 * note out of place and this walks it to where it belongs. */
int bm_roll_sort(bm_roll *r, int index)
{
    int i;

    if (r == 0 || index < 0 || index >= r->count) return index;

    i = index;
    while (i > 0 && r->note[i - 1].start > r->note[i].start) {
        bm_note t = r->note[i - 1];
        r->note[i - 1] = r->note[i];
        r->note[i] = t;
        i--;
    }
    while (i + 1 < r->count && r->note[i + 1].start < r->note[i].start) {
        bm_note t = r->note[i + 1];
        r->note[i + 1] = r->note[i];
        r->note[i] = t;
        i++;
    }
    return i;
}

int bm_roll_add(bm_roll *r, int start, int length, int midi,
                const char *phon, const char *lyric)
{
    bm_note *n;

    if (r == 0 || r->count >= BM_ROLL_NOTES_MAX) return -1;
    if (start < 0) start = 0;
    if (length < 1) length = 1;

    n = &r->note[r->count];
    n->start = start;
    n->length = length;
    n->midi = midi;
    /* Explicitly, because the slot may be one a removed note left behind and a
     * new note that arrived already tied would be very hard to account for. */
    n->tie = 0u;
    n->sel = 0u;
    copy_field(n->phon, sizeof n->phon, phon);
    copy_field(n->lyric, sizeof n->lyric, lyric);
    r->count++;

    return bm_roll_sort(r, r->count - 1);
}

void bm_roll_remove(bm_roll *r, int index)
{
    if (r == 0 || index < 0 || index >= r->count) return;
    memmove(&r->note[index], &r->note[index + 1],
            (size_t)(r->count - index - 1) * sizeof r->note[0]);
    r->count--;
}

int bm_roll_deoverlap(bm_roll *r, int keep)
{
    int i, j, moved = 0;

    if (r == 0) return 0;

    /* Leftwards from the note being held. The note in front of it gives way by
     * ending where the held one begins - but never by less than BM_ROLL_MIN_MS,
     * and the held note is stopped rather than the other one crushed.
     *
     * That asymmetry is the whole of the rule. A drag that could shrink a
     * neighbour to nothing would destroy work merely by passing over it, and
     * being unable to drag any further left is a thing you can see happening
     * and simply stop doing.
     *
     * This used to say "and there is no undo", which was the strongest part of
     * the argument and is no longer true - see bm_roll_history. The rest still
     * stands: undo is for the edit you regret, not for the twenty a single
     * careless drag would otherwise make on its way past. */
    for (j = (keep < r->count ? keep : r->count - 1); j > 0; j--) {
        int least = r->note[j - 1].start + BM_ROLL_MIN_MS;

        if (r->note[j].start < least) {
            r->note[j].start = least;
            moved = 1;
        }
        if (r->note[j - 1].start + r->note[j - 1].length > r->note[j].start) {
            r->note[j - 1].length = r->note[j].start - r->note[j - 1].start;
            moved = 1;
        }
    }

    /* Rightwards, pushing whatever the held note now covers along. Lengths are
     * kept: this is how room is made for a note in the middle of a line, and a
     * push that also shortened everything downstream would rewrite the rest of
     * the song to make space for one syllable. */
    for (i = (keep > 0 ? keep + 1 : 1); i < r->count; i++) {
        int prev_end = r->note[i - 1].start + r->note[i - 1].length;
        if (r->note[i].start < prev_end) {
            r->note[i].start = prev_end;
            moved = 1;
        }
    }

    return moved;
}

void bm_roll_retime(bm_roll *r, float from, float to)
{
    float ratio;
    int   i;

    if (r == 0 || from <= 0.0f || to <= 0.0f) return;
    ratio = from / to;

    for (i = 0; i < r->count; i++) {
        r->note[i].start  = (int)((float)r->note[i].start * ratio + 0.5f);
        r->note[i].length = (int)((float)r->note[i].length * ratio + 0.5f);
        if (r->note[i].length < 1) r->note[i].length = 1;
        /* The same clamp [dur] applies, so a tempo change cannot produce a roll
         * that will no longer sing. */
        if (r->note[i].length > 10000) r->note[i].length = 10000;
    }
}

/* ------------------------------------------------------------------ *
 * Selection
 * ------------------------------------------------------------------ */

int bm_roll_selected(const bm_roll *r)
{
    int i, n = 0;

    if (r == 0) return 0;
    for (i = 0; i < r->count; i++) if (r->note[i].sel) n++;
    return n;
}

int bm_roll_first_selected(const bm_roll *r)
{
    int i;

    if (r == 0) return -1;
    for (i = 0; i < r->count; i++) if (r->note[i].sel) return i;
    return -1;
}

int bm_roll_last_selected(const bm_roll *r)
{
    int i;

    if (r == 0) return -1;
    for (i = r->count - 1; i >= 0; i--) if (r->note[i].sel) return i;
    return -1;
}

void bm_roll_select_none(bm_roll *r)
{
    int i;

    if (r == 0) return;
    for (i = 0; i < r->count; i++) r->note[i].sel = 0u;
}

void bm_roll_select_only(bm_roll *r, int index)
{
    bm_roll_select_none(r);
    if (r != 0 && index >= 0 && index < r->count) r->note[index].sel = 1u;
}

void bm_roll_select_toggle(bm_roll *r, int index)
{
    if (r == 0 || index < 0 || index >= r->count) return;
    r->note[index].sel = (unsigned char)(r->note[index].sel ? 0 : 1);
}

int bm_roll_move_selected(bm_roll *r, int dt, int semitones)
{
    int i, moved = 0;
    int room_left = 1 << 30;

    if (r == 0 || bm_roll_selected(r) == 0) return 0;

    /* Leftwards, the group stops at whatever is in front of it. It could
     * shorten that note instead - which is what a single note dragged left
     * does - but a group passing over three notes would shorten three, and a
     * gesture that damages everything it crosses is not one to offer for a
     * whole selection at once. */
    for (i = 0; i < r->count; i++) {
        int gap;

        if (!r->note[i].sel) continue;
        if (i == 0 || !r->note[i - 1].sel) {
            gap = (i == 0) ? r->note[i].start
                           : r->note[i].start - (r->note[i - 1].start +
                                                 r->note[i - 1].length);
            if (gap < room_left) room_left = gap;
        }
    }
    if (dt < -room_left) dt = -room_left;

    /* Rightwards there is no limit, because the notes after it give way - the
     * same way they do for one note, and for the same reason: pushing the rest
     * of the line along is how room is made in the middle of it. A contiguous
     * melody has no gaps at all, so a group that stopped at the next note
     * could never move at all, which is what clamping both ways turned out to
     * mean. */

    {
        int lowest = 127, highest = 0;

        for (i = 0; i < r->count; i++) {
            if (!r->note[i].sel) continue;
            if (r->note[i].midi < lowest)  lowest  = r->note[i].midi;
            if (r->note[i].midi > highest) highest = r->note[i].midi;
        }
        if (lowest + semitones < 12)   semitones = 12 - lowest;
        if (highest + semitones > 108) semitones = 108 - highest;
    }

    if (dt == 0 && semitones == 0) return 0;

    for (i = 0; i < r->count; i++) {
        if (!r->note[i].sel) continue;
        r->note[i].start += dt;
        r->note[i].midi  += semitones;
        moved = 1;
    }

    /* One pass, left to right, pushing anything that is now overlapped. The
     * group cannot have crossed anything - it was clamped on the left and only
     * pushes on the right - so the roll is still in time order and no note has
     * changed index, which is what lets a drag keep hold of what it grabbed. */
    for (i = 1; i < r->count; i++) {
        int prev_end = r->note[i - 1].start + r->note[i - 1].length;
        if (r->note[i].start < prev_end) r->note[i].start = prev_end;
    }

    return moved;
}

int bm_roll_tie_pair(bm_roll *r, int earlier, int later)
{
    bm_note *n;

    if (r == 0) return 0;
    if (earlier < 0 || later >= r->count) return 0;
    /* Next to each other, or there is something sounding in between and
     * nothing for the tie to carry on. */
    if (later != earlier + 1) return 0;

    n = &r->note[later];
    n->start = r->note[earlier].start + r->note[earlier].length;
    n->tie = 1u;
    /* The later note is overwritten by the earlier one: it gives up its own
     * word, because it no longer has one to sing. */
    n->phon[0] = '\0';
    n->lyric[0] = '\0';

    bm_roll_deoverlap(r, later);
    bm_roll_check_ties(r);
    return r->note[later].tie ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 * Compiling
 * ------------------------------------------------------------------ */

static int has_phonemes(const bm_note *n)
{
    const char *p = n->phon;
    while (*p == ' ' || *p == '\t') p++;
    return *p != '\0';
}

/* The last vowel in a phoneme string, as a pointer into it and a length. That
 * is the one a tie carries on: a singer holding "straight" over two notes sings
 * the second on the EY and puts the final T at the end of it. */
static const char *last_vowel(const char *phon, size_t *len_out)
{
    const char *best = 0;
    size_t      best_len = 0;
    size_t      i = 0;

    while (phon[i] != '\0') {
        size_t start;

        while (phon[i] == ' ' || phon[i] == '\t') i++;
        if (phon[i] == '\0') break;

        start = i;
        while (phon[i] != '\0' && phon[i] != ' ' && phon[i] != '\t') i++;

        if (bm_phoneme_is_vowel(phon + start, i - start)) {
            best = phon + start;
            best_len = i - start;
        }
    }

    if (best == 0) return 0;
    if (len_out != 0) *len_out = best_len;
    return best;
}

/* Splits a syllable at its last vowel. `head` is everything up to and including
 * it - the part a tie carries on from - and `coda` is the consonants after it.
 *
 * The split is what makes a slur sound like one. "S T R EY1 T" tied to a second
 * note is not "straight" and then a vowel: the final T would close the tone
 * before the tie began, and the measurement shows exactly that - a gap where
 * the legato was supposed to be. A singer holding a word over two notes puts
 * the consonant at the end of the *last* of them, which is what this allows the
 * compiler to do.
 *
 * Returns 0 if there is no vowel to split at. */
static int split_syllable(const char *phon, size_t *head_len, const char **coda)
{
    size_t i = 0, end_of_vowel = 0;
    int    found = 0;

    while (phon[i] != '\0') {
        size_t start;

        while (phon[i] == ' ' || phon[i] == '\t') i++;
        if (phon[i] == '\0') break;

        start = i;
        while (phon[i] != '\0' && phon[i] != ' ' && phon[i] != '\t') i++;

        if (bm_phoneme_is_vowel(phon + start, i - start)) {
            end_of_vowel = i;
            found = 1;
        }
    }
    if (!found) return 0;

    if (head_len != 0) *head_len = end_of_vowel;
    if (coda != 0) {
        const char *c = phon + end_of_vowel;
        while (*c == ' ' || *c == '\t') c++;
        *coda = c;
    }
    return 1;
}

/* The note whose syllable a tie carries on, or -1. A run of ties is one held
 * vowel however many notes long it is, so this walks back through them. */
static int tie_source(const bm_roll *r, int index)
{
    int i;

    if (r == 0 || index <= 0 || index >= r->count) return -1;
    if (!r->note[index].tie) return -1;

    for (i = index - 1; i >= 0; i--) {
        if (has_phonemes(&r->note[i])) return i;
        /* An untied note with nothing in it breaks the chain: there is no
         * sound to carry on from. */
        if (!r->note[i].tie) return -1;
    }
    return -1;
}

const char *bm_roll_tied_vowel(const bm_roll *r, int index)
{
    /* Static because it is returned: the vowel is a slice of a note's phoneme
     * string and has to be terminated somewhere, and terminating it in place
     * would mean writing into a roll the caller handed over to be read. */
    static char vowel[8];
    const char *v;
    size_t      len = 0;
    int         src = tie_source(r, index);

    if (src < 0) return 0;

    v = last_vowel(r->note[src].phon, &len);
    if (v == 0 || len == 0 || len >= sizeof vowel) return 0;
    memcpy(vowel, v, len);
    vowel[len] = '\0';
    return vowel;
}

int bm_roll_check_ties(bm_roll *r)
{
    int i, cleared = 0;

    if (r == 0) return 0;

    for (i = 0; i < r->count; i++) {
        int ok;

        if (!r->note[i].tie) continue;

        ok = (i > 0) &&
             (r->note[i - 1].start + r->note[i - 1].length == r->note[i].start) &&
             (bm_roll_tied_vowel(r, i) != 0);

        if (!ok) { r->note[i].tie = 0u; cleared++; }
    }
    return cleared;
}

int bm_roll_compile(const bm_roll *r, char *out, size_t cap)
{
    size_t o = 0;
    int    i, cursor = 0, skipped = 0, gliding = 0;

    if (r == 0 || out == 0 || cap == 0) return -1;
    out[0] = '\0';

    for (i = 0; i < r->count; i++) {
        const bm_note *n = &r->note[i];
        const char    *sing = n->phon;
        const char    *coda = "";
        size_t         sing_len;
        char  name[8];
        int   gap, len, tied = 0;
        int   held = (i + 1 < r->count) && r->note[i + 1].tie;

        if (n->tie) {
            /* A tie sings the vowel that is already sounding, so there is no
             * consonant to re-articulate - which is the half of legato that the
             * phonemes decide. The other half is the glide below. */
            int src = tie_source(r, i);
            size_t head = 0;

            if (src < 0 || !split_syllable(r->note[src].phon, &head, &coda)) {
                skipped++;
                continue;
            }
            sing = last_vowel(r->note[src].phon, &sing_len);
            if (sing == 0) { skipped++; continue; }
            /* The consonants that closed the syllable belong at the end of the
             * held note, not at the end of the first one - so they are carried
             * here, and only by the note that ends the chain. */
            if (held) coda = "";
            tied = 1;
        } else if (!has_phonemes(n)) {
            skipped++;
            continue;
        } else {
            size_t head = 0;
            sing_len = strlen(n->phon);
            /* Handing the coda to the tie: sing up to the vowel and let the
             * held note finish the word. */
            if (held && split_syllable(n->phon, &head, &coda)) sing_len = head;
            coda = "";
        }

        /* [glide] applies onward until changed, like every other command, so
         * an ordinary note after a tied one has to be told to stop gliding.
         * Without this a slur would quietly turn the whole rest of the song
         * into a portamento. */
        if (tied != gliding) {
            int w = snprintf(out + o, cap - o, "[glide %d]",
                             tied ? BM_ROLL_GLIDE_MS : 0);
            if (w < 0 || (size_t)w >= cap - o) return -1;
            o += (size_t)w;
            gliding = tied;
        }

        /* A note that starts before the one before it ended cannot be sung as
         * written - there is one voice. It follows on instead, which is what
         * the editor's own de-overlapping already guarantees; this is here for
         * a file that was edited by hand. */
        gap = n->start - cursor;
        while (gap > 0) {
            int chunk = (gap > 10000) ? 10000 : gap;   /* [pause] takes 10 s */
            int w = snprintf(out + o, cap - o, "[pause %d]", chunk);
            if (w < 0 || (size_t)w >= cap - o) return -1;
            o += (size_t)w;
            gap -= chunk;
        }

        len = n->length;
        if (len < 1) len = 1;
        if (len > 10000) len = 10000;

        bm_roll_note_name(n->midi, name, sizeof name);
        {
            int w = snprintf(out + o, cap - o, "[dur %d][note %s] %.*s%s%s\n",
                             len, name, (int)sing_len, sing,
                             coda[0] != '\0' ? " " : "", coda);
            if (w < 0 || (size_t)w >= cap - o) return -1;
            o += (size_t)w;
        }

        cursor = (n->start > cursor ? n->start : cursor) + len;
    }

    return skipped;
}

/* ------------------------------------------------------------------ *
 * The file lines
 * ------------------------------------------------------------------ */

int bm_roll_note_write(const bm_note *n, char *out, size_t cap)
{
    char name[8];
    int  w;

    if (n == 0 || out == 0 || cap == 0) return -1;
    bm_roll_note_name(n->midi, name, sizeof name);

    /* A tie writes "~" where the phonemes would be: it has none of its own, and
     * saying "the same as before" is what it means. The character cannot occur
     * in ARPABET, so there is nothing for it to be mistaken for. */
    if (n->tie) {
        w = snprintf(out, cap, "%s %d %d ~", name, n->start, n->length);
    } else if (n->lyric[0] != '\0') {
        w = snprintf(out, cap, "%s %d %d %s ; %s", name, n->start, n->length,
                     n->phon, n->lyric);
    } else {
        w = snprintf(out, cap, "%s %d %d %s", name, n->start, n->length,
                     n->phon);
    }
    return (w < 0 || (size_t)w >= cap) ? -1 : 0;
}

#define NFAIL(...) \
    do { if (err != 0 && err_cap > 0) snprintf(err, err_cap, __VA_ARGS__); \
         return -1; } while (0)

int bm_roll_note_read(const char *value, bm_note *out, char *err, size_t err_cap)
{
    char        name[16];
    const char *p = value, *semi;
    char       *endp;
    long        start, length;
    size_t      i = 0;

    if (value == 0 || out == 0) return -1;

    memset(out, 0, sizeof *out);

    while (*p == ' ' || *p == '\t') p++;
    while (*p != '\0' && *p != ' ' && *p != '\t' && i + 1 < sizeof name) {
        name[i++] = *p++;
    }
    name[i] = '\0';

    out->midi = bm_roll_note_parse(name);
    if (out->midi < 0) NFAIL("\"%s\" is not a note name", name);

    start = strtol(p, &endp, 10);
    if (endp == p) NFAIL("note %s: expected a start time in milliseconds", name);
    p = endp;
    length = strtol(p, &endp, 10);
    if (endp == p) NFAIL("note %s: expected a length in milliseconds", name);
    p = endp;

    if (start < 0) start = 0;
    if (length < 1) length = 1;
    if (length > 10000) length = 10000;
    out->start = (int)start;
    out->length = (int)length;

    while (*p == ' ' || *p == '\t') p++;

    if (*p == '~') {
        out->tie = 1u;
        return 0;
    }

    /* The lyric, if there is one, and the phonemes are what comes before it. */
    semi = strchr(p, ';');
    if (semi != 0) {
        const char *l = semi + 1;
        size_t      n = (size_t)(semi - p);

        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (n >= sizeof out->phon) NFAIL("note %s: too many phonemes", name);
        memcpy(out->phon, p, n);
        out->phon[n] = '\0';

        while (*l == ' ' || *l == '\t') l++;
        snprintf(out->lyric, sizeof out->lyric, "%s", l);
        {
            /* Trailing whitespace on the last field, which the caller's own
             * trim has already removed from the line but not from inside it. */
            size_t m = strlen(out->lyric);
            while (m > 0 && (out->lyric[m - 1] == ' ' || out->lyric[m - 1] == '\t')) {
                out->lyric[--m] = '\0';
            }
        }
    } else {
        if (strlen(p) >= sizeof out->phon) NFAIL("note %s: too many phonemes", name);
        snprintf(out->phon, sizeof out->phon, "%s", p);
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 * Undo
 * See bm_roll.h for why this keeps whole states rather than commands.
 * ------------------------------------------------------------------ */

void bm_roll_history_init(bm_roll_history *h)
{
    if (h == 0) return;
    h->n_back = 0;
    h->n_fwd = 0;
    h->token = 0;
}

/* Pushes onto a stack, dropping the oldest when it is full.
 *
 * A shift rather than a ring, and the cost is real: twenty kilobytes times
 * thirty-two moved once per edit, which is a fraction of a millisecond at the
 * rate a person edits. A ring would avoid it and would need its wraparound to
 * be right in three places instead of none. */
static void push_state(bm_roll_state *stack, int *n, const bm_roll_state *s)
{
    if (*n >= BM_ROLL_UNDO) {
        memmove(&stack[0], &stack[1],
                (size_t)(BM_ROLL_UNDO - 1) * sizeof *stack);
        *n = BM_ROLL_UNDO - 1;
    }
    stack[*n] = *s;
    (*n)++;
}

int bm_roll_mark(bm_roll_history *h, unsigned token, const bm_roll_state *now)
{
    if (h == 0 || now == 0) return 0;

    /* Already recorded for this run. A drag reports a change every frame and a
     * word reports one every letter; the state worth going back to is the one
     * from before either began. */
    if (token != 0u && token == h->token) return 0;

    push_state(h->back, &h->n_back, now);
    h->token = token;
    /* Editing after undoing throws the future away, which is what every editor
     * does and what anyone expects: the thing you just undid is no longer on
     * the way to anywhere. */
    h->n_fwd = 0;
    return 1;
}

void bm_roll_mark_end(bm_roll_history *h)
{
    if (h != 0) h->token = 0;
}

int bm_roll_undo(bm_roll_history *h, bm_roll_state *now)
{
    if (h == 0 || now == 0 || h->n_back <= 0) return 0;

    push_state(h->fwd, &h->n_fwd, now);
    *now = h->back[--h->n_back];
    h->token = 0;
    return 1;
}

int bm_roll_redo(bm_roll_history *h, bm_roll_state *now)
{
    if (h == 0 || now == 0 || h->n_fwd <= 0) return 0;

    push_state(h->back, &h->n_back, now);
    *now = h->fwd[--h->n_fwd];
    h->token = 0;
    return 1;
}

int bm_roll_can_undo(const bm_roll_history *h) { return h != 0 && h->n_back > 0; }
int bm_roll_can_redo(const bm_roll_history *h) { return h != 0 && h->n_fwd > 0; }
