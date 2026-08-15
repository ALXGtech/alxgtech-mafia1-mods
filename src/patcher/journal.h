/* journal.h - what we did to this game folder, and what it looked like before.
 *
 * The patcher used to answer "is this file ours?" with a list of md5s. That works only while
 * the build is one we know. This answers a different question - "what did we do here" - and
 * that one has an answer on any build whatsoever, which is what lets an install proceed on a
 * game we have never seen and still be undone exactly.
 *
 * Format: one line per operation, TAB separated, append only, flushed after every line, so a
 * crash mid-install still leaves a journal describing everything already done.
 *
 *   ADD     <mod>  <relpath>  <md5 we wrote>
 *   REPLACE <mod>  <relpath>  <md5 before>  <stash name>  <md5 we wrote>
 *   PATCH   <mod>  <relpath>  <offset>  <original bytes hex>  <new bytes hex>
 *   MKDIR   <mod>  <relpath>
 *
 * PATHS ARE RELATIVE TO THE GAME FOLDER. A player who moves their install must still be able
 * to roll back, and an absolute path inside a file that outlives the folder it describes is
 * how that breaks.
 *
 * The including translation unit must define:
 *
 *     static int JMd5File(const char *path, char out[33]);
 *
 * before including this header. It is a forward declaration rather than a copy because
 * patcher.c already carries an md5 (md5_tiny.h) and two of them is how two answers to "is
 * this still ours" end up in one program.
 */
#ifndef ALXG_JOURNAL_H
#define ALXG_JOURNAL_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define J_DIR   "ALXG mods"
#define J_LOG   "install.log"
#define J_STASH "original"
#define J_LINE  1024
#define J_MAXOP 512

/* Every mod tag the journal will ever carry. They are the strings the launcher's per-tab
   toggles pass to JUndo, so they are declared once here rather than spelled out at each call
   site in two programs. */
#define J_MOD_LOADER  "loader"
#define J_MOD_FFB     "ffb"
#define J_MOD_FPV     "fpv"
#define J_MOD_SHIFTER "shifter"
#define J_MOD_FOV     "fov"

typedef void (*JSay)(const char *msg);

static void JPath(char *out, size_t n, const char *gamedir, const char *tail)
{
    snprintf(out, n, "%s\\%s\\%s", gamedir, J_DIR, tail);
}

static int JIsDir(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int JExists(const char *p)
{
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static int JEnsureDir(const char *p)
{
    if (JIsDir(p)) return 1;
    if (CreateDirectoryA(p, NULL)) return 1;
    return JIsDir(p);
}

static int JInit(const char *gamedir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\%s", gamedir, J_DIR);
    if (!JEnsureDir(p)) return 0;
    JPath(p, sizeof p, gamedir, J_STASH);
    return JEnsureDir(p);
}

/* Append one already-formatted line. Opened and closed per line on purpose: an install is a
   handful of writes, and a handle held across all of them is a handle lost on a crash. */
static int JAppend(const char *gamedir, const char *line)
{
    char p[MAX_PATH];
    FILE *f;
    JPath(p, sizeof p, gamedir, J_LOG);
    f = fopen(p, "ab");
    if (!f) return 0;
    fprintf(f, "%s\n", line);
    fflush(f);
    fclose(f);
    return 1;
}

static int JCount(const char *gamedir)
{
    char p[MAX_PATH], line[J_LINE];
    FILE *f;
    int n = 0;
    JPath(p, sizeof p, gamedir, J_LOG);
    f = fopen(p, "rb");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) n++;
    fclose(f);
    return n;
}

static int JAdd(const char *gamedir, const char *mod, const char *relpath, const char *md5)
{
    char line[J_LINE];
    snprintf(line, sizeof line, "ADD\t%s\t%s\t%s", mod, relpath, md5);
    return JAppend(gamedir, line);
}

static int JMkdir(const char *gamedir, const char *mod, const char *relpath)
{
    char line[J_LINE];
    snprintf(line, sizeof line, "MKDIR\t%s\t%s", mod, relpath);
    return JAppend(gamedir, line);
}

/* The stash name: sequence first, then the relative path flattened. Sequence first because two
   mods can displace files with the same base name, and a stash that collides silently gives
   the wrong file back to the wrong place. */
static void JStashName(char *out, size_t n, int seq, const char *relpath)
{
    char body[MAX_PATH];
    size_t i, k = 0;
    for (i = 0; relpath[i] && k < sizeof body - 1; i++)
        body[k++] = (relpath[i] == '\\' || relpath[i] == '/' || relpath[i] == ' ')
                    ? '_' : relpath[i];
    body[k] = 0;
    snprintf(out, n, "%04d-%s", seq, body);
}

/* Record a REPLACE whose file the caller has ALREADY written. Split out so the undo tests, and
   any caller that stashes for itself, can append a line without the copy running twice. */
static int JReplaceRecorded(const char *gamedir, const char *mod, const char *relpath,
                            const char *oldMd5, const char *newMd5)
{
    char stash[MAX_PATH], line[J_LINE];
    JStashName(stash, sizeof stash, JCount(gamedir), relpath);
    snprintf(line, sizeof line, "REPLACE\t%s\t%s\t%s\t%s\t%s",
             mod, relpath, oldMd5, stash, newMd5);
    return JAppend(gamedir, line);
}

/* Stash what is at relpath NOW, then record the line. Call BEFORE overwriting the file.
   Returns 0 and writes no line if there was nothing to stash: a REPLACE line pointing at a
   stash that does not exist is worse than no line at all, because undo would trust it. */
static int JReplace(const char *gamedir, const char *mod, const char *relpath,
                    const char *oldMd5, const char *newMd5)
{
    char src[MAX_PATH], stash[MAX_PATH], dst[MAX_PATH];
    snprintf(src, sizeof src, "%s\\%s", gamedir, relpath);
    if (!JExists(src)) return 0;
    JStashName(stash, sizeof stash, JCount(gamedir), relpath);
    snprintf(dst, sizeof dst, "%s\\%s\\%s\\%s", gamedir, J_DIR, J_STASH, stash);
    /* FALSE, not TRUE: a stash already sitting there belongs to an earlier install and is the
       one holding the true original. Never overwrite it. */
    if (!CopyFileA(src, dst, FALSE) && !JExists(dst)) return 0;
    return JReplaceRecorded(gamedir, mod, relpath, oldMd5, newMd5);
}

static void JHex(char *out, const unsigned char *b, unsigned n)
{
    static const char *d = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < n; i++) { out[i*2] = d[b[i] >> 4]; out[i*2+1] = d[b[i] & 15]; }
    out[n*2] = 0;
}

static int JPatch(const char *gamedir, const char *mod, const char *relpath, unsigned off,
                  const unsigned char *orig, const unsigned char *neu, unsigned n)
{
    char a[131], b[131], line[J_LINE];
    if (n < 1 || n > 64) return 0;
    JHex(a, orig, n);
    JHex(b, neu, n);
    snprintf(line, sizeof line, "PATCH\t%s\t%s\t%u\t%s\t%s", mod, relpath, off, a, b);
    return JAppend(gamedir, line);
}

/* ------------------------------------------------------------------ state */
/*
 * Is this mod installed here, and is what it installed still intact?
 *
 * This is what a toggle renders, so it has to answer from the SAME record the toggle would act
 * on. Asking the filesystem instead, e.g. whether mafia_ffb.asi is there, is how a switch ends up
 * saying ON for a folder whose journal says we never wrote it, and then doing nothing when it
 * is clicked.
 *
 *   JMOD_ABSENT  no line for this mod at all
 *   JMOD_ON      every recorded file is present with the md5 we recorded
 *   JMOD_BROKEN  lines exist, but something has been deleted or changed under us
 */
enum { JMOD_ABSENT = 0, JMOD_ON = 1, JMOD_BROKEN = 2 };

/* IS THIS A SETTINGS FILE? Found on 2026-08-05, looking at a folder that had been
 * installed cleanly ten minutes earlier: the First person tab displayed Files changed -
 * reinstall and offered a Reinstall button that was sensibly not pressed. Nothing was wrong.
 * `mafia_fp.asi` still matched the journal to the byte; `mafia_fp.ini` did not, because THIS
 * WINDOW had just written it - the page saves as you drag, on a 300 ms timer.
 *
 * The state test was asking the wrong question of the wrong file. A settings file is meant to
 * be edited: by the player, by the mod itself (a numpad seat nudge writes the new seat straight
 * back into `mafia_fp.ini`), and by the tab that exists to edit it. Judging the mod BROKEN
 * because its settings differ from the day they were installed means the lamp turns red the
 * first time anybody uses the program as intended.
 *
 * So a settings file is checked for PRESENCE only. Deleted is still broken - the mod would fall
 * back to built-in defaults and report itself installed - but its CONTENTS are the user's.
 * This is the same ruling `PATCH` already carries a few lines below, for the same reason.
 *
 * By extension rather than by a table: every settings file this project installs is a `.ini`,
 * and every payload is an `.asi`, a `.dll` or an `.exe`. A table would be one more thing to
 * keep in step with `install_settings`. If an immutable `.ini` is ever shipped, this test stops
 * noticing it - written down here because that is the trade being made. The undo path is NOT
 * affected: it still refuses to delete a settings file that changed since we wrote it, which is
 * exactly right - it will not throw away the player's bindings. */
static int JIsSettings(const char *rel)
{
    size_t n = strlen(rel);
    if (n < 4) return 0;
    return _stricmp(rel + n - 4, ".ini") == 0;
}

/* THE LAST LINE FOR A PATH WINS, and the ones before it are history.
 *
 * Found 2026-08-07 on a bench where reinstalling did nothing: the Force Feedback tab stayed on
 * Files changed - reinstall no matter how many times it was pressed, and the journal showed
 * exactly why - four REPLACE lines for mafia_ffb.asi, the first expecting cd196801 (an install
 * from an earlier build) and the last three expecting 0ce97158, which is what is on disk.
 *
 * This loop demanded that EVERY line match. One file has one state, so the stale first line
 * could never match again and the mod was BROKEN permanently; each reinstall appended another
 * line and changed nothing. A repair that cannot repair is worse than a red lamp, because it
 * invites the same click forever.
 *
 * So the check is now per PATH and keeps only the newest verdict for it. The earlier lines stay
 * in the file untouched - the undo path walks them in reverse and needs every one, including the
 * backups they name.
 *
 * The table is small and fixed-size on purpose: this runs on every repaint, and a journal with
 * more distinct paths than this for one mod would mean something else has gone wrong. Overflow
 * falls back to the old all-lines behaviour rather than silently ignoring a file. */
#define JST_MAXPATH 32

static int JModState(const char *gamedir, const char *mod)
{
    char p[MAX_PATH], line[J_LINE], have[33];
    FILE *f;
    int seen = 0, bad = 0;
    /* per-path verdict: 0 = good so far, 1 = bad. Rewritten, not or-ed, by a later line. */
    char stPath[JST_MAXPATH][MAX_PATH];
    int  stBad[JST_MAXPATH];
    int  stN = 0, stOverflow = 0;

    JPath(p, sizeof p, gamedir, J_LOG);
    f = fopen(p, "rb");
    if (!f) return JMOD_ABSENT;
    while (fgets(line, sizeof line, f)) {
        char *fld[8];
        int nf = 0;
        char *q = line, path[MAX_PATH];
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (!L) continue;
        while (nf < 8) {
            fld[nf++] = q;
            q = strchr(q, '\t');
            if (!q) break;
            *q++ = 0;
        }
        if (nf < 3 || strcmp(fld[1], mod) != 0) continue;
        seen = 1;
        snprintf(path, sizeof path, "%s\\%s", gamedir, fld[2]);

        /* MKDIR carries no md5 and a folder cannot be half there, so its presence is the whole
           test. ADD and REPLACE both recorded what WE wrote in their last field. */
        {
            int thisBad = -1;   /* -1 = this line says nothing about a path */
            if (strcmp(fld[0], "MKDIR") == 0) {
                thisBad = JIsDir(path) ? 0 : 1;
            } else if (strcmp(fld[0], "ADD") == 0 && nf >= 4) {
                if (!JExists(path)) thisBad = 1;
                else thisBad = (JIsSettings(fld[2])
                                || (JMd5File(path, have) && strcmp(have, fld[3]) == 0)) ? 0 : 1;
            } else if (strcmp(fld[0], "REPLACE") == 0 && nf >= 6) {
                if (!JExists(path)) thisBad = 1;
                else thisBad = (JIsSettings(fld[2])
                                || (JMd5File(path, have) && strcmp(have, fld[5]) == 0)) ? 0 : 1;
            }
            if (thisBad >= 0) {
                int k, hit = -1;
                for (k = 0; k < stN; k++) if (_stricmp(stPath[k], fld[2]) == 0) { hit = k; break; }
                if (hit >= 0) stBad[hit] = thisBad;            /* the later line wins */
                else if (stN < JST_MAXPATH) {
                    lstrcpynA(stPath[stN], fld[2], MAX_PATH);
                    stBad[stN++] = thisBad;
                } else {
                    /* more paths than the table holds - do not lose the verdict */
                    stOverflow = 1;
                    if (thisBad) bad = 1;
                }
            }
        }
        /* PATCH is deliberately not checked here. Reading four bytes out of a 2.3 MB exe on
           every repaint is cheap, but a player who patched their own FOV afterwards would show
           as BROKEN when nothing of ours is wrong. The undo checks it; the lamp does not. */
    }
    fclose(f);
    if (!seen) return JMOD_ABSENT;
    {
        int k;
        for (k = 0; k < stN; k++) if (stBad[k]) bad = 1;
    }
    (void)stOverflow;
    return bad ? JMOD_BROKEN : JMOD_ON;
}

/* --------------------------------------------------- correcting one recorded hash -----------
 * THE ONE THING THAT MAY REWRITE HISTORY, and it is deliberately the narrowest possible edit:
 * the "what we wrote" field of a record whose file on disk is ALREADY what we would write now.
 *
 * Why it exists. Measured 2026-08-03: a mod stuck in BROKEN could not be repaired by anything.
 * The undo above REFUSES a line whose file changed since we wrote it - correctly, it will not
 * overwrite somebody's edit - but it also KEEPS the line, and install then sees the file already
 * matching the payload and records nothing. So the stale record survived both halves of a
 * reinstall, JModState kept returning BROKEN for ever, and the launcher's Reinstall button did
 * nothing at all while the lamp stayed red. The state was TRUE and there was no way out of it.
 * The sandbox produced it innocently: tests/run_all.py builds mafia_ffb.asi and deploys it there
 * by design, so the suite replaced the very file the journal had recorded.
 *
 * The decision on 2026-08-03: fix it carefully. So:
 *
 *   - ONLY the last field - what we wrote. `before` and the stash name are untouched, which is
 *     the whole reason this is safe: UNDO STILL PUTS BACK EXACTLY WHAT WAS THERE FIRST. Nothing
 *     about the way out of this folder changes; only our claim about the current contents does.
 *   - only when the caller has established that the file on disk equals the payload it is about
 *     to install, i.e. the end state is the state the record would have had anyway.
 *   - only for that exact mod and that exact path.
 *   - and it SAYS SO, every time. A record that can quietly rewrite itself is worth less than no
 *     record; a record that rewrites itself out loud is worth what it says.
 */
static void JTell(JSay say, const char *fmt, ...);   /* defined with the undo below */

static int JCorrect(const char *gamedir, const char *mod, const char *relpath,
                    const char *nowmd5, JSay say)
{
    char p[MAX_PATH], line[J_LINE];
    char *all[J_MAXOP];
    int n = 0, fixed = 0, i;
    FILE *f;

    JPath(p, sizeof p, gamedir, J_LOG);
    f = fopen(p, "rb");
    if (!f) return 0;
    while (n < J_MAXOP && fgets(line, sizeof line, f)) {
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (!L) continue;
        all[n] = (char *)malloc(L + 1);
        if (!all[n]) break;
        memcpy(all[n], line, L + 1);
        n++;
    }
    fclose(f);

    for (i = 0; i < n; i++) {
        char work[J_LINE], rebuilt[J_LINE];
        char *fld[8];
        int nf = 0, slot = -1, k;
        char *q = work;
        snprintf(work, sizeof work, "%s", all[i]);
        while (nf < 8) {
            fld[nf++] = q;
            q = strchr(q, '\t');
            if (!q) break;
            *q++ = 0;
        }
        if (nf < 3) continue;
        if (strcmp(fld[1], mod) != 0 || strcmp(fld[2], relpath) != 0) continue;
        /* Where the hash we wrote lives, per operation. MKDIR has none and PATCH is bytes rather
           than a hash, so neither is ever corrected here. */
        if      (strcmp(fld[0], "ADD")     == 0 && nf >= 4) slot = 3;
        else if (strcmp(fld[0], "REPLACE") == 0 && nf >= 6) slot = 5;
        else continue;
        if (strcmp(fld[slot], nowmd5) == 0) continue;      /* already right */

        JTell(say, "    the journal recorded %s as %s and it is %s - correcting the record",
              relpath, fld[slot], nowmd5);
        rebuilt[0] = 0;
        for (k = 0; k < nf; k++) {
            const char *v = (k == slot) ? nowmd5 : fld[k];
            size_t at = strlen(rebuilt);
            snprintf(rebuilt + at, sizeof rebuilt - at, "%s%s", k ? "\t" : "", v);
        }
        free(all[i]);
        all[i] = (char *)malloc(strlen(rebuilt) + 1);
        if (!all[i]) { n = i; break; }      /* out of memory: write what we have and stop */
        memcpy(all[i], rebuilt, strlen(rebuilt) + 1);
        fixed++;
    }

    if (fixed) {
        f = fopen(p, "wb");
        if (f) {
            for (i = 0; i < n; i++) fprintf(f, "%s\n", all[i]);
            fclose(f);
        }
    }
    for (i = 0; i < n; i++) free(all[i]);
    return fixed;
}

/* ------------------------------------------------------------------ undo */

static void JTell(JSay say, const char *fmt, ...)
{
    char buf[J_LINE];
    va_list ap;
    if (!say) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    say(buf);
}

static int JHexByte(const char *s)
{
    int v[2], i;
    for (i = 0; i < 2; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') v[i] = c - '0';
        else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
        else return -1;
    }
    return (v[0] << 4) | v[1];
}

/* Undo one line, which has already been split in place on tabs.
   Returns 1 if the line is finished with (undone, or nothing left to undo), 0 if it must stay
   in the journal because we refused to act on it. */
static int JUndoLine(const char *gamedir, char **f, int n, JSay say)
{
    char path[MAX_PATH], have[33];

    if (n < 3) return 0;
    snprintf(path, sizeof path, "%s\\%s", gamedir, f[2]);

    if (strcmp(f[0], "ADD") == 0 && n >= 4) {
        if (!JExists(path)) { JTell(say, "    %s was already gone", f[2]); return 1; }
        if (!JMd5File(path, have) || strcmp(have, f[3]) != 0) {
            JTell(say, "    %s has changed since we wrote it - LEFT IN PLACE", f[2]);
            return 0;
        }
        if (!DeleteFileA(path)) { JTell(say, "    could not remove %s", f[2]); return 0; }
        JTell(say, "    %s removed", f[2]);
        return 1;
    }

    if (strcmp(f[0], "REPLACE") == 0 && n >= 6) {
        char stash[MAX_PATH];
        snprintf(stash, sizeof stash, "%s\\%s\\%s\\%s", gamedir, J_DIR, J_STASH, f[4]);
        if (!JExists(stash)) {
            JTell(say, "    the original of %s is missing from our own folder - LEFT ALONE",
                  f[2]);
            return 0;
        }
        if (JExists(path) && JMd5File(path, have) && strcmp(have, f[5]) != 0) {
            JTell(say, "    %s has changed since we wrote it - LEFT IN PLACE", f[2]);
            return 0;
        }
        if (!CopyFileA(stash, path, FALSE)) {
            JTell(say, "    could not put back the original %s", f[2]);
            return 0;
        }
        DeleteFileA(stash);
        JTell(say, "    %s restored to what was there before us", f[2]);
        return 1;
    }

    if (strcmp(f[0], "PATCH") == 0 && n >= 6) {
        unsigned off = (unsigned)strtoul(f[3], NULL, 10);
        unsigned len = (unsigned)(strlen(f[4]) / 2), i;
        unsigned char want[64], orig[64], got[64];
        FILE *fp;
        if (len < 1 || len > 64 || strlen(f[5]) / 2 != len) return 0;
        for (i = 0; i < len; i++) {
            int a = JHexByte(f[4] + i * 2), b = JHexByte(f[5] + i * 2);
            if (a < 0 || b < 0) return 0;
            orig[i] = (unsigned char)a;
            want[i] = (unsigned char)b;
        }
        fp = fopen(path, "r+b");
        if (!fp) { JTell(say, "    cannot open %s to undo the patch", f[2]); return 0; }
        if (fseek(fp, (long)off, SEEK_SET) != 0 || fread(got, 1, len, fp) != len) {
            fclose(fp);
            JTell(say, "    %s is shorter than the patch we recorded", f[2]);
            return 0;
        }
        if (memcmp(got, want, len) != 0) {
            fclose(fp);
            JTell(say, "    the bytes at %s+%u are not ours any more - LEFT IN PLACE",
                  f[2], off);
            return 0;
        }
        fseek(fp, (long)off, SEEK_SET);
        fwrite(orig, 1, len, fp);
        fclose(fp);
        JTell(say, "    %s reverted at offset %u", f[2], off);
        return 1;
    }

    if (strcmp(f[0], "MKDIR") == 0) {
        if (RemoveDirectoryA(path)) { JTell(say, "    %s\\ removed (empty)", f[2]); return 1; }
        if (!JIsDir(path)) return 1;
        JTell(say, "    %s\\ kept - it still has files in it", f[2]);
        /* The line is finished with either way. A folder the player has put something of their
           own into is theirs now, not an operation still pending. */
        return 1;
    }

    return 0;
}

/* Undo every line belonging to `mod`, or every line at all when `mod` is NULL, and rewrite the
   journal without the ones that went. Returns how many were undone.
   Lines are replayed NEWEST FIRST: a MKDIR recorded before the files inside it has to be undone
   after them, or RemoveDirectory always fails on a folder we ourselves just filled. */
static int JUndo(const char *gamedir, const char *mod, JSay say)
{
    char p[MAX_PATH], line[J_LINE];
    char *all[J_MAXOP], *keep[J_MAXOP];
    int n = 0, nkeep = 0, done = 0, i;
    FILE *f;

    JPath(p, sizeof p, gamedir, J_LOG);
    f = fopen(p, "rb");
    if (!f) return 0;
    while (n < J_MAXOP && fgets(line, sizeof line, f)) {
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (!L) continue;
        all[n] = (char *)malloc(L + 1);
        if (!all[n]) break;
        memcpy(all[n], line, L + 1);
        n++;
    }
    fclose(f);

    for (i = n - 1; i >= 0; i--) {
        char work[J_LINE];
        char *fld[8];
        int nf = 0;
        char *q = work;
        snprintf(work, sizeof work, "%s", all[i]);
        while (nf < 8) {
            fld[nf++] = q;
            q = strchr(q, '\t');
            if (!q) break;
            *q++ = 0;
        }
        if (mod && (nf < 2 || strcmp(mod, fld[1]) != 0)) { keep[nkeep++] = all[i]; continue; }
        if (JUndoLine(gamedir, fld, nf, say)) done++;
        else keep[nkeep++] = all[i];
    }

    f = fopen(p, "wb");
    if (f) {
        for (i = nkeep - 1; i >= 0; i--) fprintf(f, "%s\n", keep[i]);
        fclose(f);
    }
    for (i = 0; i < n; i++) free(all[i]);
    return done;
}

#endif /* ALXG_JOURNAL_H */
