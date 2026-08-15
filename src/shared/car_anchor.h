/* car_anchor.h - where the player's car is and which way it faces.
 *
 * The first-person camera anchors on the car rather than on the engine's camera, which is what
 * removes the last reason to touch the camera object and with it the last `LS3DF.dll` address
 * the camera would have needed (per the camera contract with another build, section 3).
 *
 * EVERY OFFSET HERE IS ALREADY DRIVEN. They are the ones `mafia_ffb_v6.c` has used for months
 * to build the roll and pitch channels, confirmed by feel over many sessions. This file
 * is not a new derivation; it is those offsets written down somewhere a second module can read
 * them.
 *
 * NOT YET SHARED WITH THE FFB MODULE, deliberately. The FFB build is frozen pending a
 * reference drive - the whole point of that drive is to confirm a feel against a build he has
 * just approved - so moving it onto this header is queued for afterwards rather than done now.
 * Until then the offsets exist twice, and that is a stated debt, not an oversight.
 *
 * -nostdlib clean: no CRT, no allocation, every read guarded.
 */

#ifndef CAR_ANCHOR_H
#define CAR_ANCHOR_H

#include <windows.h>

/* ---- the two builds -------------------------------------------------------------------------
 * `gamePtr` is a fixed VA because Mafia's exe has no ASLR - verified over every session this
 * project has logged, `Game.exe` at 0x00400000 every time. That is true of the EXE and must
 * never be stretched to `LS3DF.dll`, which relocates (a whole session was lost to exactly
 * that).
 *
 * `mtxOff` differs between the builds and is not a detail: everything lateral hangs off it.
 * Custom uses +0x164; on GOG that offset reads zeros, and a runtime hunt over one drive chose
 * +0x0E4 because it is the block whose atan2(m[6], m[8]) actually SWUNG with the car - 49
 * degrees against exactly 0.0 at the two other orthonormal candidates. Shape alone could not
 * choose; a bounding volume is orthonormal too.
 */
typedef struct {
    const char *name;
    DWORD gamePtr;     /* VA of the pointer to the game global                      */
    DWORD mtxOff;      /* offset of the packed 3x3 world basis inside the LS3D frame */
} CarBuild;

static const CarBuild kCarBuilds[] = {
    { "custom", 0x65115Cu, 0x164u },   /* <your Mafia install> - the reference install */
    { "gog",    0x63788Cu, 0x0E4u },   /* the stock GOG release                  */
};
#define CAR_N_BUILDS 2

#define CAR_PTR_OFF    0x24u     /* glob+0x24 -> the player vehicle                     */
#define CAR_FRAME_OFF  0x58u     /* car+0x58  -> the LS3D frame                         */
#define CAR_SPEED_OFF  0x2A0Cu   /* m/s, and the sanity check that this IS a car        */
/* The world position vec3. 0x2A10 and 0x2A18 have been read for months; 0x2A14 was measured
   from an archived car_snap.bin on 2026-07-28 (`src\carpos_third.py`) - spread 9.0 m against
   172 m and 1984 m for the two horizontals, at a comparable per-tick step. Narrow in range and
   comparable in rate is a height. One log is one map; a second session should confirm it. */
#define CAR_POS_Y_OFF  0x2A10u   /* horizontal                                          */
#define CAR_POS_H_OFF  0x2A14u   /* HEIGHT - measured, see above                        */
#define CAR_POS_X_OFF  0x2A18u   /* horizontal                                          */

static int CarSane(DWORD p)
{
    return p >= 0x00010000u && p < 0x7FFF0000u && !(p & 3u);
}

static int CarReadable(DWORD addr, DWORD bytes)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return addr + bytes <= (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize;
}

static int CarFinite(float f)
{
    DWORD b = *(DWORD *)&f;
    return ((b >> 23) & 0xFFu) != 0xFFu;     /* not inf, not nan */
}

/* Resolve glob -> car for one candidate build, validating the WHOLE chain rather than trusting
   the address. A build is chosen because its chain lands on something that behaves like a car,
   which is a stronger test than the byte signature the FFB module uses at its probe site: a
   signature proves the code is that build, this proves the data actually resolves. */
static DWORD CarPtrFor(const CarBuild *b)
{
    if (!CarReadable(b->gamePtr, 4)) return 0;
    DWORD glob = *(volatile DWORD *)b->gamePtr;
    if (!CarSane(glob) || !CarReadable(glob + CAR_PTR_OFF, 4)) return 0;
    DWORD car = *(volatile DWORD *)(glob + CAR_PTR_OFF);
    if (!CarSane(car) || !CarReadable(car + CAR_POS_X_OFF, 4)) return 0;
    float spd = *(volatile float *)(car + CAR_SPEED_OFF);
    if (!CarFinite(spd) || spd < -5.0f || spd > 150.0f) return 0;
    return car;
}

/* -1 until something resolves; sticky afterwards, because the answer cannot change inside one
   process and re-deciding every frame would let a transient dropout pick the other build. */
static int g_carBuild = -1;

static DWORD CarPtr(void)
{
    if (g_carBuild >= 0) return CarPtrFor(&kCarBuilds[g_carBuild]);
    for (int i = 0; i < CAR_N_BUILDS; i++) {
        DWORD c = CarPtrFor(&kCarBuilds[i]);
        if (c) { g_carBuild = i; return c; }
    }
    return 0;
}

static const char *CarBuildName(void)
{
    return g_carBuild >= 0 ? kCarBuilds[g_carBuild].name : "unknown";
}

/* Is a packed 3x3 a rotation? Three unit rows is the cheap half; it is enough to reject a
   bounding volume, a zeroed block or a wrong offset, which is all this guard is for. The FFB
   module applies the same test and a wrong offset there returns 0 rather than garbage. */
static int CarOrthonormal(const float *m)
{
    for (int r = 0; r < 3; r++) {
        const float *v = m + r * 3;
        float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!CarFinite(len2) || len2 < 0.98f || len2 > 1.02f) return 0;
    }
    return 1;
}

/* The anchor. Fills `basis` with the car's packed 3x3 world rotation and `pos` with its world
   position as {x, height, y} - note the ORDER, which is the order the engine stores it in and
   not the order the offsets are numbered in.
   Returns 0 and touches nothing on any failure: no car, no frame, an unreadable page, a
   non-rotation at the matrix offset, or a non-finite coordinate. A camera that anchors on
   garbage is worse than one that does not move. */
static int CarAnchor(float *pos, float *basis)
{
    DWORD car = CarPtr();
    if (!car) return 0;
    const CarBuild *b = &kCarBuilds[g_carBuild];

    DWORD frame = *(volatile DWORD *)(car + CAR_FRAME_OFF);
    if (!CarSane(frame)) return 0;                    /* on foot, or the node is dead */
    DWORD m = frame + b->mtxOff;
    if (!CarReadable(m, 36)) return 0;

    float tmp[9];
    for (int i = 0; i < 9; i++) tmp[i] = ((const volatile float *)m)[i];
    if (!CarOrthonormal(tmp)) return 0;

    if (!CarReadable(car + CAR_POS_Y_OFF, 12)) return 0;
    float px = *(volatile float *)(car + CAR_POS_X_OFF);
    float ph = *(volatile float *)(car + CAR_POS_H_OFF);
    float py = *(volatile float *)(car + CAR_POS_Y_OFF);
    if (!CarFinite(px) || !CarFinite(ph) || !CarFinite(py)) return 0;

    for (int i = 0; i < 9; i++) basis[i] = tmp[i];
    pos[0] = px; pos[1] = ph; pos[2] = py;
    return 1;
}

/* ---- THE SCENE GRAPH, read out of the listing 2026-08-04 ------------------------------------
 * Everything here was found offline in `private reference material and `private reference material`; none of it has
 * run yet. It exists because the head question has been stuck on ONE missing fact - how to reach
 * the player's own node - and it turns out the scene has always been two dereferences from the
 * global our mods already use.
 *
 * THE CHAIN. `LS3DF+0x65900` is the per-frame scene walk. It takes a scene object and traverses
 * FOUR roots from it, each starting at that root's first child:
 *
 *     (instruction listing removed for publication - the addresses, offsets and the conclusion are stated in prose)
 *     ... the same three lines again for +0x218, +0x214 and +0x21C
 *
 * That scene object is `[[gamePtr]+0x10]`: profiling every `movl 0x63788c, %reg` in Game.exe and
 * tallying what is read off `[glob+0x10]` gives +0x17c (48 sites), +0x210 (11) and +0x214 (4) -
 * the same three fields the LS3DF function touches on the object it was handed. Two independent
 * listings agreeing on three offsets is the evidence; it is still not a measurement, which is
 * why every read below is guarded and the caller is expected to log what it found.
 *
 * THE NODE. From `I3D_frame`'s own vtable (slot 14, `0x1001c330` on GOG, reached through
 * `an instruction not quoted here.exe `0x5068af`):
 *
 *   - `+0x100` is the name and it is a POINTER to a C string, not an inline buffer:
 *     `an instruction not quoted here.
 *     A probe that printed `+0x100` as characters would have printed an address.
 *   - `+0x110` is a small TYPE ENUM, 1..15 - it indexes a jump table (`an instruction not quoted here; cmpl $0xe;
 *     ja ...; an instruction not quoted here.
 *     It is NOT the four-character code; that is `+0x1D4`, and both exist.
 *   - `+0xAC` is the flags dword whose bit `0x1` gates the traversal AND the whole subtree.
 */
#define SCENE_OFF        0x10u    /* [glob+0x10] -> the scene object                       */
#define SCENE_NROOTS     4
static const DWORD kSceneRootOff[SCENE_NROOTS] = { 0x210u, 0x214u, 0x218u, 0x21Cu };

#define FRAME_FLAGS_OFF  0x0ACu   /* bit 0x1 = drawn; clearing it drops the whole subtree   */
#define FRAME_VISIBLE_BIT 0x1u
#define FRAME_NAME_OFF   0x100u   /* POINTER to a C string                                 */
#define FRAME_TYPE_OFF   0x110u   /* enum 1..15                                            */
#define FRAME_CHILD_OFF  0x124u
#define FRAME_SIBLING_OFF 0x12Cu
#define FRAME_TAG_OFF    0x1D4u   /* four-character code: SNGM, MRPH, ...                   */

/* The scene object, or 0. Deliberately does NOT depend on a car being resolved: the player is on
   foot when a map loads, and a probe that needs a car to see the scene cannot answer what the
   scene holds before he gets in one. */
static DWORD SceneObj(void)
{
    for (int i = 0; i < CAR_N_BUILDS; i++) {
        DWORD gp = kCarBuilds[i].gamePtr;
        if (!CarReadable(gp, 4)) continue;
        DWORD glob = *(volatile DWORD *)gp;
        if (!CarSane(glob) || !CarReadable(glob + SCENE_OFF, 4)) continue;
        DWORD scene = *(volatile DWORD *)(glob + SCENE_OFF);
        if (!CarSane(scene)) continue;
        if (!CarReadable(scene + kSceneRootOff[SCENE_NROOTS - 1], 4)) continue;
        /* At least one root has to be a plausible frame, or this is some other object that
           happens to be pointer-shaped at +0x210. */
        for (int r = 0; r < SCENE_NROOTS; r++) {
            DWORD root = *(volatile DWORD *)(scene + kSceneRootOff[r]);
            if (CarSane(root) && CarReadable(root + FRAME_TAG_OFF, 4)) return scene;
        }
    }
    return 0;
}

static DWORD SceneRoot(DWORD scene, int i)
{
    if (!scene || i < 0 || i >= SCENE_NROOTS) return 0;
    if (!CarReadable(scene + kSceneRootOff[i], 4)) return 0;
    DWORD root = *(volatile DWORD *)(scene + kSceneRootOff[i]);
    return CarSane(root) ? root : 0;
}

/* A frame field, guarded. Returns `dflt` rather than reading a page that may not be there - the
   traversal below walks pointers the game is editing on another thread. */
static DWORD FrameField(DWORD frame, DWORD off, DWORD dflt)
{
    if (!CarSane(frame) || !CarReadable(frame + off, 4)) return dflt;
    return *(volatile DWORD *)(frame + off);
}

/* Copies up to `cap-1` characters of the node's name. Empty string when there is no name, which
   is the normal case for most of the graph. */
static void FrameName(DWORD frame, char *out, int cap)
{
    out[0] = 0;
    DWORD p = FrameField(frame, FRAME_NAME_OFF, 0);
    if (!CarSane(p) || !CarReadable(p, 1)) return;
    int i = 0;
    while (i < cap - 1) {
        if (!CarReadable(p + (DWORD)i, 1)) break;
        char c = *(volatile char *)(p + (DWORD)i);
        if (!c) break;
        out[i++] = c;
    }
    out[i] = 0;
}

#endif /* CAR_ANCHOR_H */
