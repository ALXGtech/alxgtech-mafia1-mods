/* ini_carry.h - carry through every key in a section that the page writing it does not own.
 *
 * WHY THIS EXISTS, and it is a defect that was already in the build on 2026-09-14.
 *
 * page_ffb.h builds the whole of mafia_ffb.ini in a buffer and writes it with CREATE_ALWAYS, so
 * any key it does not print is DESTROYED. It knew that - there was a hand-written PASS[] list of
 * seven keys to preserve, added 2026-08-07 when `impulse_kick` would otherwise have been silently
 * switched off by one drag of a slider.
 *
 * The list then stopped being maintained, and the force-feedback rework added fifty-five more
 * keys to the module. The bench ini carries 245 keys today. Seven of them are on the list. So a
 * single slider drag in the utility would have rewritten the file down to nineteen lines,
 * destroying the approved speed curve, every F-key bank, the damper decision and the roll and
 * crash trims - and the file would have looked perfectly normal afterwards, because what it lost
 * was the keys nobody prints.
 *
 * A LIST OF EXCEPTIONS IS THE WRONG SHAPE. It has to be edited in a second program every time the
 * mod learns a key, it fails silently when that is forgotten, and the failure is invisible in
 * both programs. So the rule is inverted here: the page names what it OWNS, which is a fixed set
 * it can see on its own screen, and everything else in the section is carried through verbatim.
 * A new key in the mod then needs no change here at all.
 *
 * Two properties this file has to keep:
 *   - it never writes past `cap`, because SCat in ui_skin.h is unbounded and the section this
 *     copies can be tens of kilobytes;
 *   - the match on an owned key is CASE-INSENSITIVE, because the profile API is, so a file
 *     holding `Master=40` must not come back as a second `master` line.
 */
#ifndef ALXG_INI_CARRY_H
#define ALXG_INI_CARRY_H

#include <windows.h>

/* The whole section, as GetPrivateProfileSection returns it: "key=value\0key=value\0\0".
   32 KB against the 245-key bench file's ~4 KB - the headroom is for a file somebody has been
   tuning for a year, and the API truncates rather than overruns if it is ever not enough. */
#define INI_CARRY_SECTION_MAX 32768

static int IniKeyEqI(const char *a, const char *b)
{
    int i = 0;
    for (;; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

/* Append s to out, never exceeding cap-1 characters plus the terminator.
   Returns 0 if it had to truncate - the caller then knows the file it is about to write is not
   the file it meant to write, which is the one outcome that must not pass silently. */
static int IniCatN(char *out, int cap, const char *s)
{
    int n = 0, i = 0;
    while (out[n]) n++;
    while (s[i]) {
        if (n + 1 >= cap) { out[n] = 0; return 0; }
        out[n++] = s[i++];
    }
    out[n] = 0;
    return 1;
}

/* Does this "key=value" entry name one of the owned keys?
   `owned` is a NULL-terminated array of key names the caller prints itself. */
static int IniEntryOwned(const char *entry, const char *const *owned)
{
    char key[128];
    int i = 0;
    while (entry[i] && entry[i] != '=' && i < (int)sizeof(key) - 1) { key[i] = entry[i]; i++; }
    key[i] = 0;
    /* trailing spaces before the '=' are legal in an ini and not part of the name */
    while (i > 0 && (key[i - 1] == ' ' || key[i - 1] == '\t')) key[--i] = 0;
    for (i = 0; owned[i]; i++)
        if (IniKeyEqI(key, owned[i])) return 1;
    return 0;
}

/* Append to `out` every entry of [section] in `path` whose key is not in `owned`.
 *
 * `header` is printed once, before the first carried line, and not at all when there is nothing
 * to carry - so a fresh install does not grow a paragraph explaining an empty list.
 * Returns the number of lines carried, or -1 if the buffer ran out (see IniCatN).
 *
 * Entries are written back in the order the file held them. Comment lines inside the section are
 * NOT preserved: the profile API does not return them, and this file is machine-written. */
static int IniCarryOthers(char *out, int cap, const char *path, const char *section,
                          const char *const *owned, const char *header)
{
    static char sec[INI_CARRY_SECTION_MAX];
    DWORD n;
    const char *p;
    int carried = 0;

    sec[0] = 0;
    n = GetPrivateProfileSectionA(section, sec, INI_CARRY_SECTION_MAX, path);
    if (n == 0 || n >= INI_CARRY_SECTION_MAX - 2) {
        /* n == 0 is an absent file or an empty section, which is not an error. A full buffer is:
           the section was truncated and carrying a truncated copy would lose the tail. */
        return n ? -1 : 0;
    }
    for (p = sec; *p; p += lstrlenA(p) + 1) {
        if (*p == ';' || *p == '#') continue;        /* the API does not return these; belt and braces */
        if (!lstrlenA(p) || IniEntryOwned(p, owned)) continue;
        if (!carried && header && header[0])
            if (!IniCatN(out, cap, header)) return -1;
        if (!IniCatN(out, cap, p))    return -1;
        if (!IniCatN(out, cap, "\r\n")) return -1;
        carried++;
    }
    return carried;
}

#endif
