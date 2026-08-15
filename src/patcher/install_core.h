/* install_core.h - everything mafia-gog-patch DOES, with no opinion about how it was asked.
 *
 * Split out of patcher.c on 2026-08-01, before the launcher window exists rather than after.
 * The four-tab application has to install, uninstall and read state; if that logic stayed
 * welded to a console `main`, the window would either duplicate it or shell out to the exe it
 * is part of. Both are worse than a header.
 *
 * What is NOT here: argument parsing, usage text, and the console's own lifetime. Those are
 * patcher.c, and a GUI entry point will have its own.
 *
 * The consumer must provide nothing. The logger writes to stdout and, if `g_log` has been
 * opened, to that file as well - so a window can leave `g_log` NULL and read the same lines
 * off its own stdout capture, or open a log of its own.
 */
#ifndef ALXG_INSTALL_CORE_H
#define ALXG_INSTALL_CORE_H

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ md5 and the journal */
/*
 * The md5 moved out to md5_tiny.h on 2026-08-01 unchanged, because journal.h needs the same
 * hash to decide whether a file is still the one we wrote, and two copies is how two answers
 * to that question end up in one program.
 */
#include "md5_tiny.h"

static int JMd5File(const char *path, char out[33]) { return Md5OfFile(path, out); }

#include "journal.h"


/* ------------------------------------------------------------------ known builds */
/*
 * Every build this patcher will touch, and nothing else. A row is only ever added after its
 * addresses have been DERIVED from that exact exe - see docs/FOV-PORT.md for how the GOG row
 * was produced. Adding a hash without doing the derivation would let the patcher write
 * confidently into a build it has never seen.
 *
 * `patched_md5` is what our own work turns `md5` into. It is used to recognise a folder we
 * already did, never as a gate: a user may legitimately carry other exe patches, which is
 * why the FOV state is also measured from the bytes rather than inferred from the hash.
 *
 * `fov` says whether the three FOV signatures are known to apply to this build. A build can
 * be supported for the force feedback and NOT for the FOV - if an exe ships wrapped by a
 * store's DRM, its .text is encrypted on disk and there are no float constants to find,
 * while the .asi is unaffected because it works on the image the wrapper already unpacked.
 * That split is the expected shape of a Steam port, if one ever happens.
 */

typedef struct {
    const char *name;         /* what to call it in a message */
    const char *shortname;    /* what a window has room for: "GOG 1.3" */
    const char *md5;          /* stock, untouched by us */
    unsigned    size;
    const char *patched_md5;  /* what our install produces, or NULL if not measured yet */
    int         fov;          /* 1 = the three FOV signatures are verified on this build */
} build_info;

static const build_info BUILDS[] = {
    { "GOG (English, buildId 50649417607880051)", "GOG 1.3",
      "b500437f340b8a2f1e847e10bb974a06", 2355200,
      "310e8e122bbfe074214075461bf3b72e", 1 },
    /* Steam: unexamined. No copy has ever been in front of this project, so there is no row.
       To add one, hash its Game.exe, run src/fovscan.py over it (three 70.0f hits, or the
       exe is wrapped), then src/fovsig.py to confirm the signatures stay unique. */
};
#define N_BUILDS ((int)(sizeof BUILDS / sizeof BUILDS[0]))

/* The stock build we quote in a refusal message - the one we actually target. */
#define PRIMARY_BUILD 0

#define PAYLOAD_LOADER_MD5 "b8c51891352e3e7bfc2c30f2c903b46c"
#define PAYLOAD_ASI_MD5    "c56669c45adf1b58ef06f8b666f33586"
#define PAYLOAD_FP_MD5     "b34188416eea10f6cb111f298b73d943"
#define PAYLOAD_GEARBOX_MD5 "bb8c01dbabfec6a6ffc4a95de40693d7"
#define PAYLOAD_GBSETUP_MD5 "2567b875d826616b8cec9b10b3d30d29"

/*
 * EVERY .asi WE HAVE EVER SHIPPED, newest first, PAYLOAD_ASI_MD5 included.
 *
 * The moment a second version exists this list is not optional. Without it an older build of
 * our own mod is indistinguishable from a stranger's file, and the patcher does the wrong
 * thing twice: install backs it up as if it belonged to somebody else, and uninstall refuses
 * to delete it - leaving a mod loaded in a folder the user believes is clean. The .asi is not
 * even reproducible (the PE header carries a timestamp), so the same source rebuilt is a new
 * hash and has to be added here, not assumed.
 */
static const char *OUR_ASI_MD5[] = {
    "c56669c45adf1b58ef06f8b666f33586",  /* final 1.2.0 build: digest build-signature, no game bytes in the source */
    "e56788a46f09c1adfc7f067d13eb92a2",  /* build signature stored as md5 instead of 40 bytes of the game's code */
    "fc36ce1ad3a316ccd70337b5334d486a",  /* gunfire ships at 0 */
    "8de0b09d0dd888cdbad62603651d11a3",  /* v7.70: 0xA2/0xA3 name the struck I3D frame; the 0xD8 vtable names nothing */
    "a10b8fae4afb5aaa415360ea55bda840",  /* rebuilt 2026-08-11 */
    "3d184c35d15d4a061c6dfa3984985d88",  /* v7.69 - diagnostic logs behind diag=1, off for a player; forces unchanged */
    "a6366388001e428f4a8d9b45c0ba7635",  /* v7.68 - settings moved under ALXG mods\, forces bit-identical to v7.67 */
    "0ce97158bf2b7b44e17ac38c951f85f9",  /* v7.67 - THE REFERENCE BUILD, the one that ships */
    "f4340290f73c3d0e4a24b3cd6031495f",  /* v7.58 - GS_ARM_K, KickSigned, the entry gate  */
    "cd1968016dc7a0cedbc0188daac22d2a",  /* v7.52 */
    "7761d1a7a7d254ce0845bf43b355a1ae",  /* v7.51 */
    "71459ee143a0d94857e852a07ff69531",  /* v7.31 port - the reference build           */
    "19ed0cce827dffe8c7f7aca2096b49bc",  /* v7.29 port - never left this machine       */
    "21361d27b22eb622de1d2dd0e77730e2",  /* v7.21 port - the first one shipped         */
};
#define N_OUR_ASI ((int)(sizeof OUR_ASI_MD5 / sizeof OUR_ASI_MD5[0]))

static int is_our_asi(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_ASI; i++)
        if (strcmp(md5, OUR_ASI_MD5[i]) == 0) return 1;
    return 0;
}

/*
 * The same list for the first-person camera, and for exactly the same reason. Kept separate
 * from OUR_ASI_MD5[] rather than merged into one pool: the two mods are installed and removed
 * independently, and a pooled list would let an FFB build's hash authorise deleting a file
 * named mafia_fp.asi.
 */
static const char *OUR_FP_MD5[] = {
    "b34188416eea10f6cb111f298b73d943",  /* rebuilt 2026-08-15 */
    "170f286bb2b7c11ddafafbc396e86b07",  /* rebuilt 2026-08-14 */
    "70e7d966be1d9e3102091f7f89f0080e",  /* wide-screen interface fix: zones, HUD-presence gate, render targets excluded */
    "f67847efbea9ef3fecdd0e4f2452d7ae",  /* prologue guard stored as md5 instead of the game's six bytes */
    "1f1366dc2febefbc7e6a0c235ce0262e",  /* rebuilt 2026-08-13 */
    "b02b4495a346f1bbf5ee82d97f18af6e",  /* rebuilt 2026-08-12 */
    "2c37a6ee1808a1d6ad9e4e686d4a4d30",  /* rebuilt 2026-08-12 */
    "cdf45fc9ee28884d7be7c7bddcf1f520",  /* rebuilt 2026-08-12 */
    "80bb38eec1eabcec93eedfe9bb99d7db",  /* rebuilt 2026-08-12 */
    "8436c4c78529686b81956fad380edf6d",  /* rebuilt 2026-08-12 */
    "2ee095400ab693b09c07b88565601847",  /* rebuilt 2026-08-11 */
    "3d1e2e10bf84b3f5700a46fed32d0127",  /* rebuilt 2026-08-11 */
    "6f1038bbe1fbc7d84361c214b4d8c7f7",  /* rebuilt 2026-08-07 - the build that ships */
    "aeeb8b6bba9a0480632f39c084a7c973",  /* the camera Alex approved 2026-07-31 - basis fix,
                                            near plane, level horizon, live seat keys */
};
#define N_OUR_FP ((int)(sizeof OUR_FP_MD5 / sizeof OUR_FP_MD5[0]))

static int is_our_fp(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_FP; i++)
        if (strcmp(md5, OUR_FP_MD5[i]) == 0) return 1;
    return 0;
}

/*
 * mafia_fp.ini is NOT a binary we own - the mod rewrites it every time the user nudges the
 * seat with F1..F6, so the copy in a game folder is the user's tuned position within minutes
 * of playing. Hence: install writes it only when it is ABSENT, and uninstall deletes it only
 * when it is still byte-for-byte one we shipped, i.e. never touched. Every version we have
 * ever shipped has to be listed for that second test to keep working after the defaults move.
 */
static const char *OUR_FP_INI_MD5[] = {
    "74691d0915db6a2108925d3ec9fc0da8",  /* 1.4.0: the interface correction ships ON (-1)         */
    "8fda36132990f7eb7391284bfd5585f9",  /* 1.2.1 to 1.3.0: that correction shipped OFF           */
    "8bb4df980f1bca8bb193e4adb62868d3",  /* 1.1.1 and 1.2.0 - the correction was on by default    */
    "6b7eb4cdba02d287d6d2b056e8aeceaf",  /* the seat that ships: 150 / -3 / -31, near 42, view_mode 13 */
    "4cad19eefcd28ccd3cef103cf851df71",  /* the settled seat: 150 / 3 / -25, near 36, roll lock */
};
/* THE TWO ROWS AT THE TOP WERE MISSING AND THAT WAS A REAL DEFECT, found 2026-08-14 while
   shipping 1.2.1. The file this patcher INSTALLS has to be recognisable as ours or an uninstall
   leaves it behind - and the shipped mafia_fp.ini had drifted away from every hash in this list
   over the releases that changed it. Anyone who installed 1.1.1 or 1.2.0 has 8bb4df98 in their
   game folder, which is why that row stays even though nothing ships it any more.
   `tests\run_all.py` now checks this list against payload\ on every run, so it cannot drift
   again in silence. */
#define N_OUR_FP_INI ((int)(sizeof OUR_FP_INI_MD5 / sizeof OUR_FP_INI_MD5[0]))

static int is_our_fp_ini(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_FP_INI; i++)
        if (strcmp(md5, OUR_FP_INI_MD5[i]) == 0) return 1;
    return 0;
}

/*
 * And the gearbox module. Three files rather than one, and the only payload that does not
 * live in the game root: everything but the .asi goes into `gearbox hshifter setup\`, because
 * that is what a PLAYER sees when they open the folder. The .asi has to stay in the root -
 * Ultimate ASI Loader scans the game directory, `scripts\` and `plugins\`, and nothing else.
 */
static const char *OUR_GEARBOX_MD5[] = {
    "bb8c01dbabfec6a6ffc4a95de40693d7",  /* rebuilt 2026-08-12 */
    "62d736ea26e8e2948dae2fe1785a9e35",  /* settings folder moved under ALXG mods\, mode key M */
    "463cbcf594dc24b9d4ea11b6e88a6f76",  /* the build that ships - freeze detector, GOG table */
    "740dfee1d50f3d19c006baaa3dbed4ea",  /* instrumented: device health 0x95, chain break 0x96 */
    "b34568245e51bce14bdc6aa02af43d52",  /* the build the first eight drives were taken on     */
};
#define N_OUR_GEARBOX ((int)(sizeof OUR_GEARBOX_MD5 / sizeof OUR_GEARBOX_MD5[0]))

static int is_our_gearbox(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_GEARBOX; i++)
        if (strcmp(md5, OUR_GEARBOX_MD5[i]) == 0) return 1;
    return 0;
}

/* Only builds whose FULL md5 was measured go in here. Earlier ones exist (the project's own
   notes record `4bff2487...` and `00b7318f...`), but only their first four bytes were ever
   written down, and a hash reconstructed from a prefix is a hash that silently never matches -
   worse than an absent row, because it reads as coverage. */
static const char *OUR_GBSETUP_MD5[] = {
    "2567b875d826616b8cec9b10b3d30d29",  /* tabs, the scroll viewport fix, the binder harvest */
};
#define N_OUR_GBSETUP ((int)(sizeof OUR_GBSETUP_MD5 / sizeof OUR_GBSETUP_MD5[0]))

static int is_our_gbsetup(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_GBSETUP; i++)
        if (strcmp(md5, OUR_GBSETUP_MD5[i]) == 0) return 1;
    return 0;
}

/* gearbox.ini follows mafia_fp.ini's rule for a stronger reason: it holds the user's own
   button bindings, and gearbox-setup.exe rewrites it every time they bind a lever. */
/* THIS LIST IS MAINTAINED BY HAND while the payload hashes beside it are registered by the
   build scripts, so it is the one that goes stale. It went stale on 2026-08-12: gearbox.ini
   changed to enable=1 and nothing added the new hash, which would have left a freshly installed,
   untouched file behind on uninstall as if the user had edited it. Add the hash in the same
   change that changes the file. */
static const char *OUR_GEARBOX_INI_MD5[] = {
    "3f7c9012c3178078ff88a0335c8c04eb",  /* enable=1 - the module actually runs when installed */
    "f2b9eea1d1d3803d3c853959d9e38ca0",  /* mode key M (0x32), which is the game's own */
    "51c91a8685897e282293160cd01e140c",  /* the inert default that ships: nothing bound, module off */
    "0f03e284c47b74f83d1cf539337fa753",  /* the inert default: nothing bound, module off */
};
#define N_OUR_GEARBOX_INI ((int)(sizeof OUR_GEARBOX_INI_MD5 / sizeof OUR_GEARBOX_INI_MD5[0]))

static int is_our_gearbox_ini(const char *md5)
{
    int i;
    for (i = 0; i < N_OUR_GEARBOX_INI; i++)
        if (strcmp(md5, OUR_GEARBOX_INI_MD5[i]) == 0) return 1;
    return 0;
}

static int is_our_loader(const char *md5)
{
    return strcmp(md5, PAYLOAD_LOADER_MD5) == 0;
}

/*
 * Which mods an install is being asked for. One bit per J_MOD_* tag, because the launcher's
 * per-tab toggle installs exactly ONE of them and the old pair of --no-x flags could not say
 * that: they could only take things away from "all of it".
 *
 * M_LOADER is implied by any of the three .asi mods. Without dinput8.dll beside Game.exe not
 * one of them is ever loaded, so letting a caller ask for the camera alone and silently get a
 * folder where nothing runs would be the worst kind of success.
 */
#define M_LOADER  0x01
#define M_FFB     0x02
#define M_FPV     0x04
#define M_SHIFTER 0x08
#define M_FOV     0x10
#define M_ALL     0x1F
#define M_NEEDS_LOADER (M_FFB | M_FPV | M_SHIFTER)

/*
 * The five mods, in the order a person should meet them. ONE table: the tag the journal
 * stores, the bit an install asks for, and the name a human reads. The launcher's tabs, the
 * command line and the status report all walk this rather than each carrying their own list,
 * which is how the three would otherwise drift into disagreeing about what exists.
 */
typedef struct { const char *tag; int bit; const char *name; } mod_row;

static const mod_row MODS[] = {
    { J_MOD_LOADER,  M_LOADER,  "ASI loader (every mod below needs it)" },
    { J_MOD_FFB,     M_FFB,     "Force feedback" },
    { J_MOD_FPV,     M_FPV,     "First-person driving camera" },
    { J_MOD_SHIFTER, M_SHIFTER, "H-shifter gearbox" },
    { J_MOD_FOV,     M_FOV,     "Field of view 70 -> 86" },
};
#define N_MODS ((int)(sizeof MODS / sizeof MODS[0]))

static const mod_row *mod_by_tag(const char *tag)
{
    int i;
    for (i = 0; i < N_MODS; i++)
        if (strcmp(tag, MODS[i].tag) == 0) return &MODS[i];
    return NULL;
}

/* The one subfolder we create. Named for the player, not for us - it is what they open. */
/*
 * EVERYTHING PROJECT-RELATED LIVES UNDER "ALXG mods\", since 2026-08-07. Once a fresh
 * build was installed, the game folder held several folders instead of one, and tucking
 * Gearbox and Mafia FFB away inside ALXG mods was judged correct - a game folder that grows three
 * directories beside each other says nothing about which of them belongs to what, and the one
 * called "ALXG mods" makes the other two look like somebody else's.
 *
 * The name of the leaf folder does not change, only where it hangs. `mafia ffb setup\` is the
 * FFB mod's own and moves in the same commit; both of them are read by an .asi as well as by
 * this program, so all four spellings have to move together:
 *   install_core.h (here) - what the installer creates
 *   gearbox_hook8.c GBDIR - what the gearbox module reads
 *   ffb_settings.c / ffb_status.c - what the FFB module reads and writes
 *   page_ffb.h FFBDIR - what the window writes
 */
#define ALXG_DIR    "ALXG mods"
#define GEARBOX_DIR ALXG_DIR "\\gearbox hshifter setup"

#define RES_LOADER  100
#define RES_ASI     101
#define RES_FP      102
#define RES_FP_INI  103
#define RES_GEARBOX 104
#define RES_GBSETUP 105
#define RES_GB_INI  106


/* ------------------------------------------------------------------ logging */

static FILE *g_log = NULL;
static int   g_errors = 0;

/* Where a line goes. NULL = stdout, which is the console patcher and every test that has ever
   read this program's output. The launcher sets it to its own log box: a -mwindows program has
   no stdout at all, so without this every line do_install() produces would vanish, and an
   install that silently did nothing would look exactly like one that worked. */
static void (*g_say_sink)(const char *line) = NULL;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (g_say_sink) g_say_sink(buf);
    else { fputs(buf, stdout); fputc('\n', stdout); }
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
    }
}

/* The journal prints through the patcher's own logger rather than owning a second output
   path, so an undo lands in mafia-gog-patch.log beside everything else. */
static void PatchSay(const char *msg) { logf_("%s", msg); }

static void fail(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    logf_("ERROR: %s", buf);
    g_errors++;
}

/* ------------------------------------------------------------------ files */

static unsigned char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    buf = (unsigned char *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); return NULL; }
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t w;
    if (!f) return 0;
    w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int file_exists_dir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Make sure a folder is there. Already existing is success - a user who made it by hand, or a
   re-run, is not an error.
   Returns 2 when WE created it and 1 when it was already there, because only the first case
   gets a journal line: undoing a folder the player made themselves is not our business. */
static int ensure_dir(const char *path)
{
    if (file_exists_dir(path)) return 1;
    if (CreateDirectoryA(path, NULL)) return 2;
    return file_exists_dir(path) ? 1 : 0;
}

/* md5 of a file; returns 0 if unreadable */
static int file_md5(const char *path, char out[33])
{
    size_t n; unsigned char *d = read_file(path, &n);
    if (!d) return 0;
    md5_bytes(d, n, out);
    free(d);
    return 1;
}

/* ------------------------------------------------------------------ the FOV patch */
/*
 * Three sites, each 4 bytes. Located by an 8-byte prefix and an 8-byte suffix, both proven
 * unique in the GOG image (src/fovsig.py). Offsets are recorded only so the log can say
 * where the hit landed - they are never used to seek.
 */

typedef struct { const char *name; unsigned char pre[8]; unsigned char post[8]; } fov_site;

/* The angle the game ships with, and the one this project recommends. Numbers rather than the
   two byte runs they used to be: since the slider exists the patch writes whatever angle it is
   given, and only these two need names. 86 is what fits 16:9 and is the angle used on
   both benches since the first-person camera existed. */
#define FOV_STOCK        70
#define FOV_RECOMMENDED  86
#define FOV_MIN          60
#define FOV_MAX         120

/* GAME-BYTES (deliberate): 3 x 16 bytes of the stock image, used only to LOCATE the three FOV
   floats. Reviewed 2026-08-13 and kept as bytes on purpose, unlike the build signature in
   src\ffb\mafia_ffb_v6.c and the prologue guard in src\fp\fp_wmatrix.h, which both became digests
   the next day. The difference is what the bytes are for: those two are compared for equality at
   one known address, so a digest answers exactly the same question. These are a SEARCH pattern
   swept over a 2.3 MB image, and hashing them would mean hashing every 8-byte window of somebody
   else's exe - more code, and more of it, in the one function that decides where to write into
   their file. That is the last place in this project worth complicating for a cosmetic gain.
   Bounded: 48 bytes total, no code of ours copied from anywhere, and the search refuses on
   anything other than exactly one hit per site.
   This annotation exists because our own publishing gate flags a long hex run and asks whether it
   is our replacement bytes or the game's. For these three it is the game's, deliberately, and now
   the answer is written down instead of being re-asked at every audit. */
static const fov_site FOV_SITES[3] = {
    { "player-fov",
      { 0x75,0x05,0x5F,0x5E,0xC2,0x04,0x00,0xB8 }, { 0x89,0x7E,0x70,0x89,0x46,0x74,0x89,0x46 } },
    { "setup-block",
      { 0x42,0xC7,0x84,0x24,0x2C,0x07,0x00,0x00 }, { 0xC7,0x84,0x24,0x30,0x07,0x00,0x00,0x00 } },
    { "rdata-table",
      { 0x00,0x00,0xB8,0x42,0x00,0x00,0xA8,0x43 }, { 0x00,0x00,0x9E,0x42,0x00,0x00,0x9A,0x42 } },
};

/*
 * Match pre + <any four bytes> + post, and report the offset of the float between them.
 *
 * It used to search for pre + THE VALUE WE EXPECT + post, which is the stronger search and was
 * right while the only two angles were 70 and 86. The First person tab's field-of-view slider
 * ended that on 2026-08-07: an arbitrary angle can only be sought by the code around it.
 *
 * Whether that is still unique was MEASURED before this was written, not assumed - on the stock
 * exe and on one already patched to 86: exactly one hit at each of the three sites in both, at
 * the same three offsets (0x196275, 0x0C0C10, 0x227974). Every caller below still refuses on
 * anything other than exactly one hit, which is what makes the weaker pattern safe.
 */
static int fov_find_any(const unsigned char *img, size_t len, const fov_site *s, size_t *off_out)
{
    size_t i;
    int hits = 0;
    if (len < 20) return 0;
    for (i = 0; i + 20 <= len; i++) {
        if (img[i] == s->pre[0] && memcmp(img + i, s->pre, 8) == 0 &&
            memcmp(img + i + 12, s->post, 8) == 0) {
            hits++;
            if (off_out) *off_out = i + 8;
        }
    }
    return hits;
}

/*
 * What angle is in the exe right now: 1 and *deg is set, or 0 for a build whose sites do not
 * read as one consistent value. "Consistent" means all three found exactly once AND holding
 * the same four bytes - a mixed exe is one somebody else has been in, and this program will
 * not write into it.
 *
 * Whole degrees. Every value this project has ever written is one, the slider only offers
 * whole numbers, and rounding here keeps the box, the slider and the file saying the same
 * thing rather than 85.99999.
 */
static int fov_read(const unsigned char *img, size_t len, int *deg)
{
    size_t off[3];
    float  f;
    int i;
    for (i = 0; i < 3; i++)
        if (fov_find_any(img, len, &FOV_SITES[i], &off[i]) != 1) return 0;
    if (memcmp(img + off[0], img + off[1], 4) != 0 ||
        memcmp(img + off[0], img + off[2], 4) != 0) return 0;
    memcpy(&f, img + off[0], 4);
    if (!(f > 1.0f && f < 200.0f)) return 0;
    if (deg) *deg = (int)(f + 0.5f);
    return 1;
}

/* Rewrite all three sites to `deg`, whatever they hold now. Refuses unless each matches
   exactly once. The angle each site held is recorded per site, so an exe that was already at
   86 when we arrived goes back to 86 and not to 70. */
static int fov_write(const char *gamedir, unsigned char *img, size_t len, int deg)
{
    size_t off[3];
    unsigned char to[4], from[4];
    float f = (float)deg;
    int i;
    memcpy(to, &f, 4);
    for (i = 0; i < 3; i++) {
        int hits = fov_find_any(img, len, &FOV_SITES[i], &off[i]);
        if (hits != 1) {
            fail("FOV site '%s' matched %d times, expected exactly 1 - refusing to patch",
                 FOV_SITES[i].name, hits);
            return 0;
        }
    }
    for (i = 0; i < 3; i++) {
        memcpy(from, img + off[i], 4);
        if (memcmp(from, to, 4) == 0) continue;      /* already there - nothing to record */
        memcpy(img + off[i], to, 4);
        logf_("    %-12s at file offset 0x%06X -> %02X %02X %02X %02X",
              FOV_SITES[i].name, (unsigned)off[i], to[0], to[1], to[2], to[3]);
        /* Recorded with BOTH byte runs, so the undo is a write of four known bytes at a known
           offset and needs no signature search. That is what makes it work on a build the
           signatures would never have matched in the first place. */
        JPatch(gamedir, J_MOD_FOV, "Game.exe", (unsigned)off[i], from, to, 4);
    }
    return 1;
}

/* ------------------------------------------------------------------ embedded payload */

static const unsigned char *payload(int id, size_t *len)
{
    HRSRC   h = FindResourceA(NULL, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    HGLOBAL g;
    if (!h) return NULL;
    g = LoadResource(NULL, h);
    if (!g) return NULL;
    *len = (size_t)SizeofResource(NULL, h);
    return (const unsigned char *)LockResource(g);
}

/*
 * Drop an embedded file into the target folder.
 *   - if it is already there with OUR md5, say so and do nothing (re-run safe);
 *   - if it is an OLDER BUILD OF OURS (`ours` recognises it), overwrite it in place and say
 *     so: an upgrade is not somebody else's file and must not leave a .bak behind;
 *   - if it is already there with a DIFFERENT md5, that is somebody else's file: back it up
 *     before overwriting, because we do not know what it is.
 */
/*
 * `reldir` is "" for the game root or a subfolder name; the journal stores the path relative
 * to the game folder so a player who moves their install can still roll back.
 */
static void rel_join(char *out, size_t n, const char *reldir, const char *name)
{
    if (reldir && reldir[0]) snprintf(out, n, "%s\\%s", reldir, name);
    else                     snprintf(out, n, "%s", name);
}

static int install_payload(const char *gamedir, const char *reldir, const char *name,
                           int res_id, const char *want_md5, int (*ours)(const char *),
                           const char *mod)
{
    char path[MAX_PATH], rel[MAX_PATH], bak[MAX_PATH], have[33], wrote[33];
    size_t len = 0;
    const unsigned char *data = payload(res_id, &len);
    int displaced = 0;
    if (!data) { fail("embedded payload '%s' is missing from this exe", name); return 0; }

    rel_join(rel, sizeof rel, reldir, name);
    snprintf(path, sizeof path, "%s\\%s", gamedir, rel);
    md5_bytes(data, len, wrote);

    if (file_exists(path) && file_md5(path, have)) {
        if (strcmp(have, want_md5) == 0) {
            logf_("    %s already installed (md5 matches)", name);
            /* AND THE RECORD IS BROUGHT UP TO DATE IF IT IS BEHIND. This is the one narrow case
               where a journal line may be rewritten: the file on disk is already exactly what we
               were about to write, so the corrected record is the record this line WOULD have had.
               Without it a mod whose file was replaced by another build of ours - which is what
               tests/run_all.py does to the sandbox by design - reads BROKEN for ever, because undo
               refuses the stale line and keeps it while install stays silent. Only the "what we
               wrote" field moves; the original and its stash are untouched, so undo still puts back
               exactly what was there before us. See JCorrect. */
            JCorrect(gamedir, mod, rel, have, PatchSay);
            return 1;
        }
        if (ours && ours(have)) {
            if (!write_file(path, data, len)) { fail("could not write %s", path); return 0; }
            logf_("    %s upgraded from an earlier build of ours (%u bytes)", name, (unsigned)len);
            JAdd(gamedir, mod, rel, wrote);
            return 1;
        }
        snprintf(bak, sizeof bak, "%s.bak", path);
        if (!file_exists(bak)) {
            if (CopyFileA(path, bak, TRUE)) logf_("    %s exists and is NOT ours - backed up to %s.bak", name, name);
            else { fail("could not back up the existing %s", name); return 0; }
        } else {
            logf_("    %s exists and is not ours; a .bak is already there, leaving it", name);
        }
        /* Stash and record BEFORE the overwrite. The .bak beside the file stays as well: the
           stash is what the program looks for, the .bak is what a person looks for. */
        displaced = JReplace(gamedir, mod, rel, have, wrote);
    }
    if (!write_file(path, data, len)) { fail("could not write %s", path); return 0; }
    logf_("    %s installed (%u bytes)", name, (unsigned)len);
    if (!displaced) JAdd(gamedir, mod, rel, wrote);
    return 1;
}

/*
 * Drop an embedded SETTINGS file, and only if there is nothing there.
 *
 * The opposite rule to install_payload above, deliberately. A settings file in a game folder
 * is the user's, not ours, from the first time they change anything - and mafia_fp.ini is
 * rewritten by the mod itself on every seat nudge, so "the file we shipped" stops being true
 * within a minute of play. Overwriting it on a re-run would throw away a seat position they
 * spent a drive finding, and would do it silently, which is the worst shape of all.
 */
static int install_settings(const char *gamedir, const char *reldir, const char *name,
                            int res_id, const char *mod)
{
    char path[MAX_PATH], rel[MAX_PATH], wrote[33];
    size_t len = 0;
    const unsigned char *data = payload(res_id, &len);
    if (!data) { fail("embedded payload '%s' is missing from this exe", name); return 0; }

    rel_join(rel, sizeof rel, reldir, name);
    snprintf(path, sizeof path, "%s\\%s", gamedir, rel);
    if (file_exists(path)) {
        /* Nothing recorded either. We did not write this one, so it is not ours to undo. */
        logf_("    %s already there - LEFT ALONE (it holds your own settings)", name);
        return 1;
    }
    if (!write_file(path, data, len)) { fail("could not write %s", path); return 0; }
    md5_bytes(data, len, wrote);
    JAdd(gamedir, mod, rel, wrote);
    logf_("    %s written (%u bytes, defaults)", name, (unsigned)len);
    return 1;
}

/*
 * Remove one file of ours, and give back whatever it displaced.
 *
 * One helper rather than a copy per file: this block was written out four times over three
 * sessions and the copies had already drifted apart in what they said. `ours` is the whole
 * safety rule - a file we do not recognise is somebody else's mod and is not ours to delete.
 */
static void uninstall_mod(const char *dir, const char *name, int (*ours)(const char *))
{
    char path[MAX_PATH], bak[MAX_PATH], m[33];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    if (!file_exists(path)) { logf_("    %s absent", name); return; }
    if (!file_md5(path, m) || !ours(m)) {
        logf_("    %s is not one of ours - LEFT IN PLACE, remove it yourself if you want it gone", name);
        return;
    }
    if (!DeleteFileA(path)) { logf_("    could not remove %s", name); return; }
    logf_("    %s removed", name);
    /* Symmetric with install, and it was missing until 2026-07-23: install backs up a file
       that is not ours, so uninstall has to give it back. Without this we quietly keep
       somebody else's mod disabled forever. */
    snprintf(bak, sizeof bak, "%s\\%s.bak", dir, name);
    if (file_exists(bak) && MoveFileA(bak, path))
        logf_("    restored the %s that was there before us", name);
}

/* The settings counterpart: gone only while it is still exactly what we wrote. */
static void uninstall_settings(const char *dir, const char *name, int (*ours)(const char *),
                               const char *whose)
{
    char path[MAX_PATH], m[33];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    if (!file_exists(path)) { logf_("    %s absent", name); return; }
    if (file_md5(path, m) && ours(m)) {
        if (DeleteFileA(path)) logf_("    %s removed (never edited)", name);
        else                   logf_("    could not remove %s", name);
        return;
    }
    logf_("    %s has your own %s in it - LEFT IN PLACE", name, whose);
}

/* ------------------------------------------------------------------ resolution */
/*
 * HKCU\SOFTWARE\Illusion Softworks\Mafia -> LS3D_setup, REG_BINARY, 63 bytes.
 * Only 8 of them are touched: width at 2..5, height at 6..9 (int32 LE). bpp at 10..13 and
 * everything undecoded is preserved byte for byte.
 *
 * This matters more than it looks: a blob that disagrees with the desktop mode kills the
 * game in IGraph::Init() before the menu, with an error that names the back buffer and not
 * the resolution. An hour went into that once.
 *
 * HKLM carries a copy and needs admin. HKCU alone has proved sufficient, so a failure there
 * is reported and not treated as fatal.
 */
static int set_resolution(int w, int h)
{
    static const char *KEY = "SOFTWARE\\Illusion Softworks\\Mafia";
    HKEY  k;
    BYTE  blob[256];
    DWORD type = 0, len = sizeof blob;
    LONG  rc;

    rc = RegOpenKeyExA(HKEY_CURRENT_USER, KEY, 0, KEY_READ | KEY_WRITE, &k);
    if (rc != ERROR_SUCCESS) {
        logf_("    resolution: HKCU key not found (the game writes it on first run) - skipped");
        return 1;
    }
    rc = RegQueryValueExA(k, "LS3D_setup", NULL, &type, blob, &len);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || len < 14) {
        logf_("    resolution: LS3D_setup missing or not the expected shape - skipped");
        RegCloseKey(k);
        return 1;
    }
    {
        int oldw = *(int *)(blob + 2), oldh = *(int *)(blob + 6);
        if (oldw == w && oldh == h) {
            logf_("    resolution: already %dx%d - unchanged", w, h);
            RegCloseKey(k);
            return 1;
        }
        *(int *)(blob + 2) = w;
        *(int *)(blob + 6) = h;
        rc = RegSetValueExA(k, "LS3D_setup", 0, REG_BINARY, blob, len);
        if (rc != ERROR_SUCCESS) { fail("could not write LS3D_setup (rc=%ld)", rc); RegCloseKey(k); return 0; }
        logf_("    resolution: %dx%d -> %dx%d (bpp and everything undecoded preserved)", oldw, oldh, w, h);
    }
    RegCloseKey(k);
    return 1;
}

/* ------------------------------------------------------------------ actions */

typedef struct {
    char dir[MAX_PATH];
    char exe[MAX_PATH];
    char bak[MAX_PATH];
    char md5[33];
    size_t size;
    const build_info *build;   /* NULL when the exe matches no known build */
    int already_patched;       /* the hash is this build's patched result */
} target;

/* Which known build is this, and is it stock or our own output? */
static const build_info *identify(const char *md5, int *already)
{
    int i;
    *already = 0;
    for (i = 0; i < N_BUILDS; i++) {
        if (strcmp(md5, BUILDS[i].md5) == 0) return &BUILDS[i];
        if (BUILDS[i].patched_md5 && strcmp(md5, BUILDS[i].patched_md5) == 0) {
            *already = 1;
            return &BUILDS[i];
        }
    }
    return NULL;
}

static int locate(const char *dir, target *t)
{
    size_t n;
    unsigned char *d;
    snprintf(t->dir, sizeof t->dir, "%s", dir);
    snprintf(t->exe, sizeof t->exe, "%s\\Game.exe", dir);
    snprintf(t->bak, sizeof t->bak, "%s\\Game.exe.bak", dir);
    if (!file_exists(t->exe)) { fail("no Game.exe in %s - point me at a Mafia folder", dir); return 0; }
    d = read_file(t->exe, &n);
    if (!d) { fail("cannot read %s", t->exe); return 0; }
    md5_bytes(d, n, t->md5);
    t->size = n;
    free(d);
    t->build = identify(t->md5, &t->already_patched);
    return 1;
}

static void report(target *t)
{
    size_t n;
    unsigned char *d;
    char m[33];
    char path[MAX_PATH];
    int st;

    logf_("target folder : %s", t->dir);
    logf_("Game.exe      : %u bytes, md5 %s", (unsigned)t->size, t->md5);

    if (!t->build)              logf_("                = UNRECOGNISED build");
    else if (t->already_patched) logf_("                = %s, with our patch already applied", t->build->name);
    else                         logf_("                = %s, recognised", t->build->name);

    d = read_file(t->exe, &n);
    if (d) {
        if (!fov_read(d, n, &st))
            logf_("FOV           : MIXED / not found - do not trust this exe");
        else if (st == FOV_STOCK)
            logf_("FOV           : %d (stock)", st);
        else
            logf_("FOV           : %d (patched)", st);
        free(d);
    }
    logf_("Game.exe.bak  : %s", file_exists(t->bak) ? "present" : "absent");

    snprintf(path, sizeof path, "%s\\dinput8.dll", t->dir);
    if (!file_exists(path)) logf_("dinput8.dll   : absent");
    else if (file_md5(path, m) && strcmp(m, PAYLOAD_LOADER_MD5) == 0) logf_("dinput8.dll   : ours (ASI loader)");
    else logf_("dinput8.dll   : present but NOT ours (md5 %s)", m);

    snprintf(path, sizeof path, "%s\\mafia_ffb.asi", t->dir);
    if (!file_exists(path)) logf_("mafia_ffb.asi : absent");
    else if (file_md5(path, m) && strcmp(m, PAYLOAD_ASI_MD5) == 0) logf_("mafia_ffb.asi : ours");
    else if (is_our_asi(m)) logf_("mafia_ffb.asi : ours, but an EARLIER build (md5 %s) - install upgrades it", m);
    else logf_("mafia_ffb.asi : present but a different build (md5 %s)", m);

    snprintf(path, sizeof path, "%s\\mafia_fp.asi", t->dir);
    if (!file_exists(path)) logf_("mafia_fp.asi  : absent");
    else if (file_md5(path, m) && strcmp(m, PAYLOAD_FP_MD5) == 0) logf_("mafia_fp.asi  : ours");
    else if (is_our_fp(m)) logf_("mafia_fp.asi  : ours, but an EARLIER build (md5 %s) - install upgrades it", m);
    else logf_("mafia_fp.asi  : present but a different build (md5 %s)", m);

    /* Reported separately from the .asi because the two states are independent and the
       difference matters to anyone reading this log: an .asi with no ini runs on its built-in
       defaults, and those have `enabled` off and `route` 1 - i.e. a camera that does nothing. */
    snprintf(path, sizeof path, "%s\\mafia_fp.ini", t->dir);
    if (!file_exists(path)) logf_("mafia_fp.ini  : absent - install writes the defaults");
    else if (file_md5(path, m) && is_our_fp_ini(m)) logf_("mafia_fp.ini  : ours, untouched");
    else logf_("mafia_fp.ini  : present and EDITED (md5 %s) - your settings, left alone", m);

    snprintf(path, sizeof path, "%s\\gearbox_hook.asi", t->dir);
    if (!file_exists(path)) logf_("gearbox_hook  : absent");
    else if (file_md5(path, m) && strcmp(m, PAYLOAD_GEARBOX_MD5) == 0) logf_("gearbox_hook  : ours");
    else if (is_our_gearbox(m)) logf_("gearbox_hook  : ours, but an EARLIER build (md5 %s) - install upgrades it", m);
    else logf_("gearbox_hook  : present but a different build (md5 %s)", m);

    /* Retired 2026-08-04. Nothing installs one; an older install of ours may still have one, and
       saying so is the point - "absent" is now the healthy answer, and a leftover is a tool that
       writes the same ini as the H-shifter tab and can drift from the mod behind our back. */
    snprintf(path, sizeof path, "%s\\%s\\gearbox-setup.exe", t->dir, GEARBOX_DIR);
    if (!file_exists(path)) logf_("gearbox-setup : absent - retired, the H-shifter tab replaced it");
    else if (file_md5(path, m) && is_our_gbsetup(m))
        logf_("gearbox-setup : ours, left by an earlier install - uninstall removes it");
    else logf_("gearbox-setup : present but not one of ours (md5 %s) - left alone", m);

    /* Worth reporting the two states apart, because "edited" is the NORMAL and wanted state
       here - it means the user has bound their own shifter, which is the only way the module
       does anything at all. */
    snprintf(path, sizeof path, "%s\\%s\\gearbox.ini", t->dir, GEARBOX_DIR);
    if (!file_exists(path)) logf_("gearbox.ini   : absent - install writes the defaults");
    else if (file_md5(path, m) && is_our_gearbox_ini(m)) logf_("gearbox.ini   : ours, nothing bound yet");
    else logf_("gearbox.ini   : your own bindings (md5 %s) - left alone", m);
}

/*
 * `fov_deg` is the angle the M_FOV step writes into Game.exe. It is a parameter rather than a
 * constant because the First person tab now carries a slider for it (added 2026-08-07;
 * the recommended value is 86). Callers that have no opinion pass
 * FOV_RECOMMENDED; it is ignored entirely unless M_FOV is in `mods`.
 */
static int do_install(target *t, int res_w, int res_h, int mods, int refuse_unknown, int fov_deg)
{
    size_t n;
    unsigned char *d;
    int st;

    /*
     * THE REFUSAL IS GONE, 2026-08-01, and this is a reversal rather than a relaxation.
     * Until today an unrecognised Game.exe meant zero writes and exit 1. A person on an
     * odd build may know exactly what they are doing, and may want only one of the three mods.
     * What makes it safe to say yes is the journal - everything below is recorded, so there is
     * a way back out of a folder we do not recognise.
     * --refuse-unknown keeps the old behaviour for a caller that wants a gate.
     */
    if (!t->build) {
        int i;
        if (refuse_unknown) {
            fail("this Game.exe is not a build this patcher knows, and --refuse-unknown was "
                 "given.\n         found md5 %s (%u bytes). Nothing has been written.",
                 t->md5, (unsigned)t->size);
            return 0;
        }
        logf_("");
        logf_("  ---------------------------------------------------------------------");
        logf_("  This Game.exe is not the build these mods were made and tested on.");
        logf_("    found    md5 %s (%u bytes)", t->md5, (unsigned)t->size);
        for (i = 0; i < N_BUILDS; i++)
            logf_("    expected md5 %s  %s", BUILDS[i].md5, BUILDS[i].name);
        logf_("");
        logf_("  Installing anyway, because you may know exactly what you are doing.");
        logf_("  What that means in practice:");
        logf_("    - copying files is safe. Nothing of yours is overwritten without a copy");
        logf_("      of it being kept.");
        logf_("    - the FOV patch is SKIPPED unless its signature matches exactly once.");
        logf_("    - the force feedback, the gearbox and the camera read addresses that");
        logf_("      belong to GOG 1.3. On another build they will most likely find no car");
        logf_("      and do nothing at all.");
        logf_("  Everything written here is recorded and can be undone with --uninstall.");
        logf_("  BACK UP YOUR GAME FOLDER AND YOUR SAVES FIRST.");
        logf_("  ---------------------------------------------------------------------");
    } else if (!t->already_patched) {
        logf_("");
        logf_("  Game.exe matches %s", t->build->name);
        logf_("  - the build everything here was tuned and tested on.");
    }

    /* Before anything is written, because a folder we cannot journal into is one we should not
       be putting mods into either: there would be no way back out of it. */
    if (!JInit(t->dir)) {
        fail("could not create the \"%s\" folder in %s.\n"
             "         That folder is where the record of everything we change is kept, and\n"
             "         without it an install could not be undone. Nothing has been written.",
             J_DIR, t->dir);
        return 0;
    }

    logf_("");
    logf_("[1/6] backup");
    /* The FOV patch is the only thing in this program that writes Game.exe, so a run that is
       not applying it has nothing to back up. Skipping the copy also keeps the state honest:
       the backup is journalled under `fov`, and making it anyway would leave --status
       reporting the FOV as installed after an install that deliberately skipped it. Found by
       running --status after --install-mod fpv. */
    if (!(mods & M_FOV)) {
        logf_("    nothing to back up - Game.exe is not being touched on this run");
    } else if (file_exists(t->bak)) {
        logf_("    Game.exe.bak already exists - left untouched (it holds the original)");
    } else if (CopyFileA(t->exe, t->bak, TRUE)) {
        char bm[33];
        logf_("    Game.exe -> Game.exe.bak");
        /* Recorded, because we made it. After a full undo Game.exe is byte-identical to what
           this copy holds, so leaving 2.3 MB of it behind would be litter rather than safety.
           Tagged `fov` because the patch is the only reason it exists, and the undo order
           takes care of itself: newest first means the patch is reverted before the backup
           that covered it goes. */
        if (file_md5(t->bak, bm)) JAdd(t->dir, J_MOD_FOV, "Game.exe.bak", bm);
    } else {
        fail("could not write Game.exe.bak - is the folder read-only?");
        return 0;
    }

    logf_("");
    logf_("[2/6] field of view -> %d", fov_deg);
    if (!(mods & M_FOV)) {
        logf_("    skipped on request");
        goto payload_step;
    }
    if (fov_deg < FOV_MIN || fov_deg > FOV_MAX) {
        fail("field of view %d is outside %d..%d - refusing", fov_deg, FOV_MIN, FOV_MAX);
        return 0;
    }
    if (!t->build || !t->build->fov) {
        /* Supported for the mod, not for the exe patch. Two reasons reach here now: a store
         * wrapper, whose encrypted .text has no float constants in it to find, and a build we
         * simply do not know. The signature search would very likely fail on the second, but
         * "very likely" is not a thing to patch an exe on. */
        logf_("    %s - skipped, everything else still installs",
              t->build ? "not supported on this build"
                       : "not attempted on a build we do not know");
        goto payload_step;
    }
    d = read_file(t->exe, &n);
    if (!d) { fail("cannot re-read Game.exe"); return 0; }
    if (!fov_read(d, n, &st)) {
        fail("the three FOV sites do not read as one consistent angle; refusing to touch the exe");
        free(d);
        return 0;
    } else if (st == fov_deg) {
        logf_("    already %d - nothing to do", fov_deg);
    } else if (fov_write(t->dir, d, n, fov_deg)) {
        if (!write_file(t->exe, d, n)) { fail("could not write Game.exe"); free(d); return 0; }
        logf_("    written, size unchanged (%u bytes)", (unsigned)n);
    } else {
        free(d);
        return 0;
    }
    free(d);

payload_step:
    logf_("");
    logf_("[3/6] loader and force feedback");
    if (mods & M_LOADER)
        install_payload(t->dir, "", "dinput8.dll",   RES_LOADER, PAYLOAD_LOADER_MD5, NULL,
                        J_MOD_LOADER);
    else
        logf_("    dinput8.dll skipped on request");
    if (mods & M_FFB)
        install_payload(t->dir, "", "mafia_ffb.asi", RES_ASI,    PAYLOAD_ASI_MD5,    is_our_asi,
                        J_MOD_FFB);
    else
        logf_("    mafia_ffb.asi skipped on request");

    logf_("");
    logf_("[4/6] first-person driving camera");
    if (!(mods & M_FPV)) {
        /* Skipped on request, and it takes the ini with it. An .asi installed without its
           settings would run on built-in defaults - `enabled` off, `route` 1 - which is a
           camera that does nothing, reported as installed. Both or neither. */
        logf_("    skipped on request");
    } else {
        install_payload(t->dir, "", "mafia_fp.asi", RES_FP, PAYLOAD_FP_MD5, is_our_fp,
                        J_MOD_FPV);
        install_settings(t->dir, "", "mafia_fp.ini", RES_FP_INI, J_MOD_FPV);
    }

    logf_("");
    logf_("[5/6] H-shifter gearbox module");
    if (!(mods & M_SHIFTER)) {
        logf_("    skipped on request");
    } else {
        char gb[MAX_PATH];
        int made;
        /* The parent first: ensure_dir creates ONE level, and GEARBOX_DIR now hangs under
           "ALXG mods\". The journal creates that folder too, but only when it writes its first
           line - and `--install-mod shifter` reaches here before any line has been written.
           Not journalled: do_uninstall already removes "ALXG mods\" once the journal is empty,
           and a second mechanism removing the same folder is how one of them stops working. */
        snprintf(gb, sizeof gb, "%s\\%s", t->dir, ALXG_DIR);
        ensure_dir(gb);
        snprintf(gb, sizeof gb, "%s\\%s", t->dir, GEARBOX_DIR);
        made = ensure_dir(gb);
        if (!made) {
            fail("could not create %s", gb);
        } else {
            /* Only a folder WE created is recorded. Undoing one the player made themselves is
               not our business, and RemoveDirectory on it would be a surprise. */
            if (made == 2) JMkdir(t->dir, J_MOD_SHIFTER, GEARBOX_DIR);
            /* The .asi in the game ROOT and everything else in the subfolder. Not tidiness:
               Ultimate ASI Loader scans the game directory, `scripts\` and `plugins\` and
               nothing else, so a mod filed anywhere prettier is simply never loaded. */
            install_payload(t->dir, "", "gearbox_hook.asi", RES_GEARBOX,
                            PAYLOAD_GEARBOX_MD5, is_our_gearbox, J_MOD_SHIFTER);
            /* gearbox-setup.exe is NOT installed any more, as of 2026-08-04: there is one
               application now and it has an H-shifter tab, so a second binder in the game folder
               is a second thing to keep in step with the mod. An older install of ours may still
               have one, and the undo below still knows how to take it away; nothing puts one
               there. */
            install_settings(t->dir, GEARBOX_DIR, "gearbox.ini", RES_GB_INI, J_MOD_SHIFTER);
            logf_("    the module ships OFF and unbound - bind it on the H-shifter tab of");
            logf_("    the mods window, which writes %s\\gearbox.ini", GEARBOX_DIR);
        }
    }

    logf_("");
    logf_("[6/6] resolution");
    if (res_w > 0 && res_h > 0) set_resolution(res_w, res_h);
    else logf_("    skipped on request");

    return g_errors == 0;
}

/*
 * `mod` is NULL for "everything", or one of the J_MOD_* tags for one mod at a time. The
 * per-mod form is what the launcher's tab toggles call; the command line always passes NULL.
 */
static int do_uninstall(target *t, const char *mod)
{
    char gb[MAX_PATH], m[33];
    int  dummy, journalled;

    journalled = JCount(t->dir) > 0;

    logf_("");
    logf_("[1/2] Game.exe");
    if (journalled) {
        /* The journal holds the FOV sites with their original bytes, so the exe is repaired
           four bytes at a time in step 2 and the whole-file restore would only undo other
           people's patches along with ours. */
        logf_("    the FOV patch is recorded and is undone below, byte for byte");
        logf_("    Game.exe.bak is left where it is - it is your copy, not ours to spend");
    } else if (!file_exists(t->bak)) {
        logf_("    no Game.exe.bak and no journal - nothing to restore");
    } else if (file_md5(t->bak, m) && (dummy = 0, identify(m, &dummy)) == NULL) {
        /* The backup must itself be a build we recognise. Anything else and we would be
         * installing an unknown exe over the user's game in the name of undoing our work. */
        fail("Game.exe.bak is not a build this patcher knows (md5 %s).\n"
             "         Restoring it would replace your Game.exe with something unknown.\n"
             "         Left alone - check it by hand.", m);
    } else if (CopyFileA(t->bak, t->exe, FALSE)) {
        /* The pre-journal path, kept for a folder an older build of this program installed
           into. Nothing writes that shape any more, but a user's folder can still be in it. */
        logf_("    no journal (installed by an older build) - restored from Game.exe.bak");
        if (DeleteFileA(t->bak)) logf_("    Game.exe.bak removed (it is now identical to Game.exe)");
    } else {
        fail("could not restore Game.exe from the backup");
    }

    logf_("");
    logf_("[2/2] undoing what we recorded");
    if (journalled) {
        int n = JUndo(t->dir, mod, PatchSay);
        logf_("    %d operation%s undone", n, n == 1 ? "" : "s");
        if (JCount(t->dir) == 0) {
            /* Our own folder goes when the journal came out EMPTY, and the test is the count
               rather than whether this was a full uninstall. Undoing the last mod one at a time has
               to leave the folder exactly as undoing them all at once does - otherwise the
               launcher's toggles can return every file and still leave an "ALXG mods" folder
               behind, which is the promise "back the way it was" broken by a directory.
               Measured as a launcher self-test failure on 2026-08-01.
               If anything was left in place the count is not zero, and the record of it has to
               survive. */
            char p[MAX_PATH];
            JPath(p, sizeof p, t->dir, J_STASH);
            RemoveDirectoryA(p);
            JPath(p, sizeof p, t->dir, J_LOG);
            DeleteFileA(p);
            snprintf(p, sizeof p, "%s\\%s", t->dir, J_DIR);
            if (RemoveDirectoryA(p)) logf_("    %s\\ removed - this folder is back to yours", J_DIR);
        }
    } else {
        /* No journal: an older build put this here. Fall back on the md5 lists, which is
           exactly what they are still for. */
        logf_("    no journal - falling back on recognising files by md5");
        uninstall_mod(t->dir, "mafia_ffb.asi", is_our_asi);
        uninstall_mod(t->dir, "mafia_fp.asi",  is_our_fp);
        uninstall_settings(t->dir, "mafia_fp.ini", is_our_fp_ini, "camera settings");

        snprintf(gb, sizeof gb, "%s\\%s", t->dir, GEARBOX_DIR);
        uninstall_mod(t->dir, "gearbox_hook.asi", is_our_gearbox);
        uninstall_mod(gb, "gearbox-setup.exe", is_our_gbsetup);
        uninstall_settings(gb, "gearbox.ini", is_our_gearbox_ini, "shifter bindings");
        if (RemoveDirectoryA(gb)) logf_("    %s\\ removed (empty)", GEARBOX_DIR);
        else if (file_exists_dir(gb)) logf_("    %s\\ kept - it still has files in it", GEARBOX_DIR);

        uninstall_mod(t->dir, "dinput8.dll", is_our_loader);
    }

    logf_("");
    logf_("The resolution in the registry is left as it is: it is your setting, and putting");
    logf_("back an old value could leave the game asking for a mode your monitor has not got.");
    return g_errors == 0;
}

/*
 * One line per mod, machine-readable, for whatever draws the toggles.
 *
 *   <tag> <absent|on|broken> <name>
 *
 * It reads the JOURNAL, not the folder. Asking the filesystem instead would let a switch show
 * ON for a file we never wrote, and then do nothing when it is clicked.
 */
static void do_status(target *t)
{
    /* Deliberately printf and not logf_: these lines are a contract with a parser, not a
       message to a person, and routing them through a sink would let a GUI quietly change what
       patcher_roundtrip.ps1 reads over a pipe. The launcher does not use this at all - it calls
       JModState directly. */
    int i;
    printf("build\t%s\t%s\n", t->build ? "known" : "unknown",
           t->build ? t->build->name : t->md5);
    for (i = 0; i < N_MODS; i++) {
        int st = JModState(t->dir, MODS[i].tag);
        printf("%s\t%s\t%s\n", MODS[i].tag,
               st == JMOD_ON ? "on" : st == JMOD_BROKEN ? "broken" : "absent",
               MODS[i].name);
    }
}

#endif /* ALXG_INSTALL_CORE_H */
