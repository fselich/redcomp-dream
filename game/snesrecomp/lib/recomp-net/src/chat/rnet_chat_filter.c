/* rnet_chat_filter.c — see include/recomp_net/chat_filter.h.
 *
 * Kept deliberately dependency-free (no regex, no ICU): it runs inside every
 * game runtime on every platform, and the server's Rust port must be able to
 * match it rule for rule. */
#include "recomp_net/chat_filter.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CF_MAX_WORDS 1024
#define CF_MAX_WORD_CP 32
#define CF_MAX_TEXT_CP 1024

/* Word list, baked from data/chat_filter_words.txt. */
static const char k_words_text[] =
#include "rnet_chat_filter_words.inc"
    ;

typedef struct CfWord {
    uint32_t cp[CF_MAX_WORD_CP];
    int len;
    int anywhere; /* '~': match inside other words */
} CfWord;

static CfWord g_words[CF_MAX_WORDS];
static int g_word_count;
static int g_loaded;
static int g_disabled;

/* ---- UTF-8 ---------------------------------------------------------------- */

static size_t utf8_decode(const unsigned char *s, size_t len, uint32_t *cp)
{
    if (len == 0) { *cp = 0; return 0; }
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if ((s[0] & 0xE0) == 0xC0 && len >= 2 && (s[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && len >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((s[0] & 0xF8) == 0xF0 && len >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
              ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return 0; /* malformed */
}

static size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---- Folding --------------------------------------------------------------
 * The Rust port (recomp-net-server/src/chat_filter.rs) mirrors this table
 * exactly; change both or neither. */

static uint32_t fold_latin1(uint32_t c)
{
    /* U+00C0..U+00FF: strip the accent, lower the case. */
    static const char k_map[] =
        "aaaaaaaceeeeiiii" /* C0..CF: À..Ï (Ð -> d below) */
        "dnooooo/ouuuuyts" /* D0..DF: Ð Ñ Ò..Ö × Ø Ù..Ü Ý Þ ß */
        "aaaaaaaceeeeiiii" /* E0..EF */
        "dnooooo/ouuuuyty"; /* F0..FF: ð ñ ò..ö ÷ ø ù..ü ý þ ÿ */
    const unsigned idx = c - 0xC0;
    const char m = k_map[idx];
    if (m == '/') return c; /* × ÷ stay symbols */
    if (c == 0xDE || c == 0xFE) return 't'; /* Þ þ */
    return (uint32_t)m;
}

static uint32_t fold(uint32_t c)
{
    if (c < 0x80) {
        if (c >= 'A' && c <= 'Z') return c + 32;
        switch (c) { /* leetspeak */
            case '0': return 'o';
            case '1': return 'i';
            case '3': return 'e';
            case '4': return 'a';
            case '5': return 's';
            case '7': return 't';
            case '@': return 'a';
            case '$': return 's';
            case '!': return 'i';
            case '|': return 'l';
            case '+': return 't';
            default: return c;
        }
    }
    if (c >= 0xC0 && c <= 0xFF) return fold_latin1(c);
    if (c >= 0x100 && c <= 0x17F) {
        /* Latin Extended-A: pairs of (upper, lower) sharing one base letter. */
        static const char k_base[] =
            "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiiijjjjkkklllllllllllnnnnnnnnnoooooooo"
            "rrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";
        const unsigned idx = c - 0x100;
        if (idx < sizeof(k_base) - 1) return (uint32_t)k_base[idx];
        return c;
    }
    if (c >= 0x410 && c <= 0x42F) return c + 0x20; /* Cyrillic А..Я */
    if (c == 0x401) return 0x435;                 /* Ё -> е */
    if (c == 0x451) return 0x435;                 /* ё -> е */
    if (c == 0x404) return 0x454;                 /* Є */
    if (c == 0x406) return 0x456;                 /* І */
    if (c == 0x407) return 0x457;                 /* Ї */
    if (c == 0x490) return 0x491;                 /* Ґ */
    if (c >= 0x391 && c <= 0x3A9) return c + 0x20; /* Greek Α..Ω */
    if (c == 0x3C2) return 0x3C3;                 /* final sigma */
    if (c >= 0x3AC && c <= 0x3CE) {               /* accented Greek lower */
        switch (c) {
            case 0x3AC: return 0x3B1; case 0x3AD: return 0x3B5; case 0x3AE: return 0x3B7;
            case 0x3AF: return 0x3B9; case 0x3CC: return 0x3BF; case 0x3CD: return 0x3C5;
            case 0x3CE: return 0x3C9; default: return c;
        }
    }
    if (c >= 0x386 && c <= 0x38F) {               /* accented Greek upper */
        switch (c) {
            case 0x386: return 0x3B1; case 0x388: return 0x3B5; case 0x389: return 0x3B7;
            case 0x38A: return 0x3B9; case 0x38C: return 0x3BF; case 0x38E: return 0x3C5;
            case 0x38F: return 0x3C9; default: return c;
        }
    }
    if (c >= 0xFF01 && c <= 0xFF5E) return fold(c - 0xFF01 + 0x21); /* full-width ASCII */
    if (c == 0x20AC) return 'e';                  /* € */
    return c;
}

/* Something a word can be made of, in folded space. Digits that leet did
 * not fold ("2", "6", "9") count as boundaries, as does punctuation. */
static int is_letter(uint32_t c)
{
    if (c < 0x80) return (c >= 'a' && c <= 'z');
    if (c >= 0x2000 && c <= 0x206F) return 0; /* general punctuation */
    if (c >= 0x3000 && c <= 0x303F) return 0; /* CJK punctuation */
    if (c >= 0xFF00 && c <= 0xFF0F) return 0;
    if (c == 0x00A0 || c == 0x00B7 || c == 0x00BF || c == 0x00A1) return 0;
    return 1;
}

static int is_separator(uint32_t c)
{
    return c == ' ' || c == '.' || c == '-' || c == '_' || c == '*' || c == '\'' ||
           c == ',' || c == 0x2019 || c == 0x00A0;
}

/* ---- List ------------------------------------------------------------------- */

static void load_words(void)
{
    const unsigned char *p = (const unsigned char *)k_words_text;
    size_t len = sizeof(k_words_text) - 1;
    size_t i = 0;
    if (g_loaded) return;
    g_loaded = 1;
    {
        const char *env = getenv("RNET_CHAT_FILTER");
        if (env && (env[0] == '0' || env[0] == 'n' || env[0] == 'N' || env[0] == 'f' || env[0] == 'F'))
            g_disabled = 1;
    }
    while (i < len && g_word_count < CF_MAX_WORDS) {
        CfWord *w = &g_words[g_word_count];
        memset(w, 0, sizeof(*w));
        if (p[i] == '~') { w->anywhere = 1; ++i; }
        while (i < len && p[i] != '\n') {
            uint32_t cp;
            size_t n = utf8_decode(p + i, len - i, &cp);
            if (n == 0) { ++i; continue; }
            i += n;
            if (cp == '\r') continue;
            /* Phrases are written without spaces; tolerate a list that
             * kept them by dropping the space, which the separator rule
             * then bridges in the text. */
            if (cp == ' ') continue;
            if (w->len < CF_MAX_WORD_CP) w->cp[w->len++] = fold(cp);
        }
        if (i < len) ++i; /* '\n' */
        if (w->len > 0) ++g_word_count;
    }
}

int rnet_chat_filter_enabled(void)
{
    load_words();
    return !g_disabled && g_word_count > 0;
}

int rnet_chat_filter_word_count(void)
{
    load_words();
    return g_word_count;
}

/* ---- Matching ----------------------------------------------------------- */

/* Try word `w` at folded position `at`. Returns the end position (exclusive)
 * of the match, or 0 for no match. Rules:
 *   - each word letter must appear, in order;
 *   - extra repeats of a letter are absorbed ("fuuuck") -- only repeats
 *     beyond what the word itself asks for, so "ass" still needs both s;
 *   - between letters of a word of 4+ letters, separators are skipped
 *     ("f.u.c.k", "f u c k", "filho da puta");
 *   - a whole-word entry needs a non-letter (or the edge) on both sides. */
static int match_at(const uint32_t *t, const uint32_t *orig, int n, int at, const CfWord *w)
{
    int p = at;
    int j;
    const int allow_sep = w->len >= 4;
    /* Boundaries are judged on the ORIGINAL characters: "asshole!" ends in
     * a '!' that leet-folds to 'i', and that must still count as an edge. */
    if (!w->anywhere && at > 0 && is_letter(orig[at - 1])) return 0;
    for (j = 0; j < w->len; ++j) {
        const uint32_t want = w->cp[j];
        if (j > 0 && allow_sep) {
            while (p < n && is_separator(t[p])) ++p;
        }
        if (p >= n || t[p] != want) return 0;
        ++p;
        /* Absorb repeats of this letter unless the word itself continues
         * with the same letter (then that letter claims the next one). */
        if (j + 1 >= w->len || w->cp[j + 1] != want) {
            while (p < n && t[p] == want) ++p;
        }
    }
    if (!w->anywhere && p < n && is_letter(orig[p])) return 0;
    /* A separator-bridged match must not end on a separator run we
     * skipped: p already points past the last letter, so it does not. */
    return p;
}

static int filter_cps(uint32_t *orig, uint32_t *folded, int n)
{
    int i, hits = 0;
    for (i = 0; i < n;) {
        int best_end = 0;
        int k;
        if (!is_letter(folded[i])) { ++i; continue; }
        for (k = 0; k < g_word_count; ++k) {
            const CfWord *w = &g_words[k];
            int end;
            if (w->cp[0] != folded[i]) continue;
            end = match_at(folded, orig, n, i, w);
            if (end > best_end) best_end = end;
        }
        if (best_end > 0) {
            int m;
            for (m = i; m < best_end; ++m) orig[m] = '*';
            ++hits;
            i = best_end;
        } else {
            ++i;
        }
    }
    return hits;
}

int rnet_chat_filter_apply(char *text, size_t cap)
{
    static uint32_t orig[CF_MAX_TEXT_CP];
    static uint32_t folded[CF_MAX_TEXT_CP];
    size_t len, i, o;
    int n = 0, hits;
    if (!text || cap == 0) return 0;
    load_words();
    if (g_disabled || g_word_count == 0) return 0;
    /* No strnlen: it is POSIX, and this file compiles as C99 everywhere. */
    for (len = 0; len < cap && text[len]; ++len) {}
    for (i = 0; i < len && n < CF_MAX_TEXT_CP;) {
        uint32_t cp;
        size_t k = utf8_decode((const unsigned char *)text + i, len - i, &cp);
        if (k == 0) return 0; /* malformed: leave the line alone */
        orig[n] = cp;
        folded[n] = fold(cp);
        ++n;
        i += k;
    }
    if (i < len) return 0; /* too long to fold safely; leave it */
    hits = filter_cps(orig, folded, n);
    if (hits == 0) return 0;
    /* '*' is one byte for a code point that took 1..4: never longer. */
    for (i = 0, o = 0; (int)i < n; ++i) {
        char buf[4];
        size_t k = utf8_encode(orig[i], buf);
        if (o + k + 1 > cap) break;
        memcpy(text + o, buf, k);
        o += k;
    }
    text[o] = '\0';
    return hits;
}

int rnet_chat_filter_copy(const char *in, char *out, size_t out_cap)
{
    size_t n;
    if (!out || out_cap == 0) return 0;
    if (!in) { out[0] = '\0'; return 0; }
    n = strlen(in);
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, in, n);
    out[n] = '\0';
    return rnet_chat_filter_apply(out, out_cap);
}
