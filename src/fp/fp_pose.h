/* fp_pose.h - the pose pipeline, and the shape of it is deliberate.
 *
 * THE RULE: do not compute a view matrix, compute a POSE, and derive the matrix once at the very
 * end.
 *
 *     anchor -> seat -> orientation -> view
 *
 * | stage       | what this mod puts in it                        |
 * |-------------|-------------------------------------------------|
 * | anchor      | the car's world position; NO rotation by default |
 * | seat        | offset in metres, from the INI                   |
 * | orientation | the engine's own, taken from the incoming VIEW   |
 *
 * Keeping the stages separate is what makes each of them testable on its own, and it is why a
 * wrong picture can be traced to one stage rather than to "the camera".
 *
 * THE ANCHOR FLAG is the one stage with a choice in it. Making the CAR's rotation the base gives
 * a rigid cockpit mount, which Alex rejected on 2026-07-27 in as many words - he keeps the
 * engine's own rotation because he likes the head-on-a-neck swing when he turns the wheel. So the
 * base is a flag on the anchor stage rather than two code paths, and `CamGetPose` stays the
 * single source no call site can see past.
 *
 * MATRIX CONVENTION: D3D8, row-vector, row-major. `m[0..10]` is the rotation, `m[12..14]` the
 * translation row. Everything here is that layout and nothing converts.
 */

#ifndef FP_POSE_H
#define FP_POSE_H

typedef struct {
    float basis[9];   /* packed 3x3, row-major: the anchor's world rotation */
    float pos[3];     /* world position, {x, height, y} as the engine stores it */
    int   haveBasis;  /* 0 = position only, which is what version one uses */
} CamPose;

/* ---- THE PITCH TRIM, 2026-08-06 ------------------------------------------------------------
 * Stage 3 of the table above, the orientation stage, given a knob. Alex asked for it after
 * driving the camera for the first time on the GOG bench: the engine aims the vehicle camera
 * BELOW the horizon - measured that drive at forward.y = -0.2510, i.e. 14.5 degrees down - which
 * is right for a chase camera looking at the car and wrong from the driver's seat, where it puts
 * the horizon near the top of the frame.
 *
 * Rotate forward and up about the camera's own RIGHT axis; right itself is untouched, so this
 * composes with the level horizon in either order.
 *
 * IT LIVES HERE, NOT IN fp_camera.c, so the project's offline test harness can drive it without
 * the game or a DLL (that harness is not part of this repository). The
 * reason is one instruction: `fsincos` leaves the COSINE in st(0) and the SINE in st(1), the
 * reverse of the order the mnemonic reads. Swap them and the camera tilts by (90 - angle),
 * which is a plausible-looking picture and a silently wrong one - the exact failure this
 * project keeps paying for. The test asserts sin(0)=0, cos(0)=1, sin(90)=1 and then that a
 * known matrix pitched by a known angle lands where trigonometry says it must.
 */
static void FpSinCos(float x, float *sn, float *cs)
{
#if defined(__i386__) || defined(_M_IX86)
    double c, s;
    __asm__ volatile ("fsincos" : "=t"(c), "=u"(s) : "0"((double)x));
    *sn = (float)s; *cs = (float)c;
#else
    /* Only reached by a 64-bit test build. Same values, no x87. */
    double xx = (double)x, t = xx, sacc = 0.0, cacc = 1.0, tc = 1.0;
    for (int n = 1; n <= 15; n += 2) {
        sacc += t;
        t *= -xx * xx / (double)((n + 1) * (n + 2));
    }
    for (int n = 2; n <= 16; n += 2) {
        tc *= -xx * xx / (double)((n - 1) * n);
        cacc += tc;
    }
    *sn = (float)sacc; *cs = (float)cacc;
#endif
}

/* `w` is the camera's WORLD 4x4 at cam+0x10: row 0 right, row 1 up, row 2 forward, stride 4.
   Returns 1 if it wrote, 0 if it refused - and it refuses on a non-finite result rather than
   writing one, because a NaN in the camera basis is a black screen, not a wrong angle. */
static int CamPitch(float *w, int deg)
{
    if (deg == 0) return 0;
    float ux = w[4], uy = w[5], uz = w[6];
    float fx = w[8], fy = w[9], fz = w[10];
    float sn, cs;
    FpSinCos((float)deg * 0.017453292f, &sn, &cs);
    float nfx = fx * cs + ux * sn, nfy = fy * cs + uy * sn, nfz = fz * cs + uz * sn;
    float nux = ux * cs - fx * sn, nuy = uy * cs - fy * sn, nuz = uz * cs - fz * sn;
    float chk = nfx + nfy + nfz + nux + nuy + nuz;
    if (chk != chk) return 0;                      /* NaN: x != x is only true for NaN */
    w[4] = nux; w[5] = nuy; w[6] = nuz;
    w[8] = nfx; w[9] = nfy; w[10] = nfz;
    return 1;
}

/* Stage 2. Move the eye point along the ANCHOR's own axes, never along world axes - or the
   offset points the wrong way the moment the car is not facing north. That is the whole of the
   warning already recorded about "forward", and it applies to sideways and up just as much. */
static void CamSeat(CamPose *p, const float *seatRUF)
{
    if (!p->haveBasis) {
        /* No basis means we cannot rotate the offset into the car's frame, so applying it would
           push the camera in an arbitrary compass direction. Height still works, because the
           world up axis is a world axis. This is a degraded path, not a normal one. */
        p->pos[1] += seatRUF[1];
        return;
    }
    const float *m = p->basis;
    for (int i = 0; i < 3; i++)
        p->pos[i] += m[0 * 3 + i] * seatRUF[0]    /* right   */
                   + m[1 * 3 + i] * seatRUF[1]    /* up      */
                   + m[2 * 3 + i] * seatRUF[2];   /* forward */
}

/* The final step, and it lives in ONE place because LS3DF builds VIEW as an inverse itself -
 * this is the engine's own shape, not our invention.
 *
 * Version one keeps the engine's rotation exactly: `src` is the matrix the engine was about to
 * send, and only its translation row is replaced. For a row-vector view matrix whose rotation
 * part is R, the translation row is the negated eye position rotated by R:
 *
 *     out[12] = -(e . column 0 of R)      and so on
 *
 * which is the same relation verified separately on another build - VIEW's 3x3 is the
 * transpose of WORLD's, and VIEW's translation is the negated world position rotated by it.
 */
static void CamViewFromEye(const float *src, const float *eye, float *out)
{
    for (int i = 0; i < 16; i++) out[i] = src[i];
    out[12] = -(eye[0] * src[0] + eye[1] * src[4] + eye[2] * src[8]);
    out[13] = -(eye[0] * src[1] + eye[1] * src[5] + eye[2] * src[9]);
    out[14] = -(eye[0] * src[2] + eye[1] * src[6] + eye[2] * src[10]);
}

/* The inverse of the relation above: where was the engine's OWN camera?
 *
 * This is not decoration - it is the only cheap sanity check available at the seam. If the eye
 * point we computed from the car sits kilometres from the one the engine was using, then the
 * anchor is wrong, or the matrix offset is wrong, or we are looking at a menu camera. Better to
 * notice that in a log line than to teleport the picture and let a user report "it is broken".
 */
/* OFFSET the view, rather than rebuilding its translation row from scratch.
 *
 * `CamViewFromEye` above REPLACES the row, which is right only when the row is known to hold the
 * whole camera position. Measured 2026-07-28, and it is not: the engine sends VIEW more than
 * once per frame, and the calls sampled every 900th all had a zero translation - which is what a
 * SKYBOX pass looks like, since the sky must stay infinitely far away. The world is drawn by a
 * different call whose row carries the real camera. Replacing that row put the camera at the
 * world origin while the city sat two kilometres away, so the screen kept the sky and lost
 * everything else - with the draw counters completely unaffected, which is exactly what was
 * measured and exactly what made it look impossible.
 *
 * Offsetting is also the honest operation: we want to move the eye BY something, and at zero it
 * reproduces the engine's matrix byte for byte, including which pass it belongs to.
 */
static void CamViewOffset(const float *src, const float *delta, float *out)
{
    for (int i = 0; i < 16; i++) out[i] = src[i];
    out[12] = src[12] - (delta[0] * src[0] + delta[1] * src[4] + delta[2] * src[8]);
    out[13] = src[13] - (delta[0] * src[1] + delta[1] * src[5] + delta[2] * src[9]);
    out[14] = src[14] - (delta[0] * src[2] + delta[1] * src[6] + delta[2] * src[10]);
}

static void CamEyeFromView(const float *v, float *eye)
{
    /* R is orthonormal, so its inverse is its transpose: e = -(t * R^T). */
    eye[0] = -(v[12] * v[0] + v[13] * v[1] + v[14] * v[2]);
    eye[1] = -(v[12] * v[4] + v[13] * v[5] + v[14] * v[6]);
    eye[2] = -(v[12] * v[8] + v[13] * v[9] + v[14] * v[10]);
}

static float CamDist2(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

#endif /* FP_POSE_H */
