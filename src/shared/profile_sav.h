/* profile_sav.h - read and write Mafia's player profile, savegame\mafiaNNN.sav.
 *
 * WHY. Alex built and tuned the whole force feedback against a specific set of the GAME's own
 * settings - car handling, its built-in force feedback, two sound levels. A player who installs
 * the mod and leaves the game on its factory values is not feeling what was tuned. So installing
 * the mod applies them, and un-applying puts back exactly what that player had.
 *
 * WHERE THEY LIVE. A full tree diff of a configured install against a pristine one - 9526 files
 * against 9523 - found every one of these settings in savegame\mafiaNNN.sav and nowhere else.
 * Not a byte of any .dta, Game.exe or LS3DF.dll differs. Graphics are NOT here; they are global,
 * in the shared LS3D_setup registry blob.
 *
 * THE FORMAT, worked out from the game's own loader and then verified by round trip - every file
 * this code writes is re-read and compared, and a mismatch refuses rather than saves:
 *
 *     0x00  "forP"           a 24-byte plain header the loader writes itself
 *     0x18  encrypted body   1052 bytes = 263 dwords
 *
 * one self-keying stream cipher over DWORDs (the loader routine at 0x0060B2B0 in the GOG build):
 *
 *     plain = cipher ^ key ;  acc += plain ;  key += acc
 *
 * The seeds are not in the file. They came from known plaintext - the loader checks that the
 * first decrypted dword is 'forP' and the second is 1, which is two equations for two unknowns.
 *
 * The settings are dwords 201..223 (struct+0x4C4), all floats. Dword 2 is the slot, 3..8 the
 * name and 21..200 the progress: THOSE ARE NEVER WRITTEN by anything here.
 *
 * This is our own reimplementation of that arithmetic, written from observed behaviour. It
 * protects nothing but the player's own settings file, and it is applied only to a profile on the
 * player's own machine, with the original bytes kept so an uninstall restores them exactly.
 */
#ifndef PROFILE_SAV_H
#define PROFILE_SAV_H

#define PSAV_HEADER   0x18u
#define PSAV_MAGIC    0x50726F66u        /* 'forP' */
#define PSAV_KEY0     0x23101976u
#define PSAV_ACC0     0x10072002u
#define PSAV_DWORDS   263
#define PSAV_BYTES    (PSAV_HEADER + PSAV_DWORDS * 4)   /* 1076 */

/* The eleven fields Alex changed, measured against a profile he created and did not touch.
   Index into the decrypted body; the float is his value. Everything else in the settings block
   was already identical between the two profiles, so writing it would be a no-op with a risk
   attached - a field this project has never seen a reason to touch stays untouched. */
typedef struct { int idx; float val; } PSavField;
static const PSavField PSAV_RECOMMENDED[] = {
    { 201, 1.00000000f },   /* struct+0x4C4, factory 0.2  */
    { 202, 0.22672000f },   /* +0x4C8, factory 0.0        */
    { 206, 0.15625000f },   /* +0x4D8, factory 0.5        */
    { 207, 0.15625000f },   /* +0x4DC, factory 0.5        */
    { 208, 0.15625000f },   /* +0x4E0, factory 0.5        */
    { 209, 0.52083331f },   /* +0x4E4, factory 1.0        */
    { 210, 0.39215687f },   /* +0x4E8, factory 1.0        */
    { 211, 0.65870000f },   /* +0x4EC, factory 1.0        */
    { 212, 0.68627453f },   /* +0x4F0, factory 1.0        */
    { 217, 1.00000000f },   /* +0x504, factory 0.703 - handling, not sound */
    { 219, 1.00000000f },   /* +0x50C, factory 0.117 - handling, not sound */
};
#define PSAV_NRECOMMENDED ((int)(sizeof PSAV_RECOMMENDED / sizeof PSAV_RECOMMENDED[0]))

/* raw -> dwords. Returns 0 unless the file is the right size AND decrypts to 'forP' version 1,
   which is the whole validity check: a wrong seed or a different build fails it immediately
   rather than writing plausible rubbish into somebody's profile. */
static int PSavDecrypt(const unsigned char *raw, unsigned long len, unsigned long *out)
{
    unsigned long key = PSAV_KEY0, acc = PSAV_ACC0;
    int i;
    if (len != PSAV_BYTES) return 0;
    for (i = 0; i < PSAV_DWORDS; i++) {
        const unsigned char *p = raw + PSAV_HEADER + i * 4;
        unsigned long c = (unsigned long)p[0] | ((unsigned long)p[1] << 8)
                        | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
        unsigned long pl = (c ^ key) & 0xFFFFFFFFu;
        out[i] = pl;
        acc = (acc + pl) & 0xFFFFFFFFu;
        key = (key + acc) & 0xFFFFFFFFu;
    }
    return (out[0] == PSAV_MAGIC && out[1] == 1u);
}

/* dwords + the original 24-byte header -> raw. The header is carried over rather than rebuilt:
   it is the loader's, not ours, and nothing here has any business deciding what is in it. */
static void PSavEncrypt(const unsigned long *dw, const unsigned char *hdr, unsigned char *out)
{
    unsigned long key = PSAV_KEY0, acc = PSAV_ACC0;
    int i;
    for (i = 0; i < (int)PSAV_HEADER; i++) out[i] = hdr[i];
    for (i = 0; i < PSAV_DWORDS; i++) {
        unsigned long c = (dw[i] ^ key) & 0xFFFFFFFFu;
        unsigned char *p = out + PSAV_HEADER + i * 4;
        p[0] = (unsigned char)(c & 0xFF);
        p[1] = (unsigned char)((c >> 8) & 0xFF);
        p[2] = (unsigned char)((c >> 16) & 0xFF);
        p[3] = (unsigned char)((c >> 24) & 0xFF);
        acc = (acc + dw[i]) & 0xFFFFFFFFu;
        key = (key + acc) & 0xFFFFFFFFu;
    }
}

static unsigned long PSavFromFloat(float f)
{
    union { float f; unsigned long u; } u; u.f = f; return u.u;
}

#endif /* PROFILE_SAV_H */
