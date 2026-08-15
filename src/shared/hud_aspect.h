/* hud_aspect.h - the one factor that un-stretches Mafia's interface on a wide screen.
 *
 * THE DEFECT. Mafia's interface is authored against a 4:3 screen: its pre-transformed (RHW)
 * vertices carry that layout's coordinates and the engine spreads them across whatever
 * backbuffer it was given. At 16:9 every HORIZONTAL distance is therefore multiplied by
 * (16/9) / (4/3) = 1.3333 while vertical distances are untouched, so a circle comes out an
 * ellipse exactly 1.3333 too wide. Alex, 2026-08-12, translated: "even the round shapes of the
 * radar are not round, they are slightly oval".
 *
 * It is the RADAR, not the compass - the compass only appears when a mission gives somewhere
 * to drive to. Do not call it a compass in code or in a log line.
 *
 * This has nothing to do with stereo or with a headset. It happens on a plain monitor at
 * 1920x1080, which is what this port runs at.
 *
 * WHY IT IS A HEADER AND NOT A COPY. The offline test includes THIS file, so the arithmetic it
 * asserts is the arithmetic that ships. A test that re-types the formula tests the typing.
 *
 * The factor and its eleven assertions came from our own earlier implementation of the same
 * correction, re-derived here rather than re-typed. Nothing in this file touches an engine
 * address, an offset or a signature.
 */
#ifndef HUD_ASPECT_H
#define HUD_ASPECT_H

/* pct: -1 = derive from the surface (the honest default), 0 = off, 25..200 = explicit percent.
   Always returns a POSITIVE factor: a zero would collapse the interface to a vertical line,
   which reads as the mod crashing rather than as a bad number. */
static float hud_aspect_factor(int pct, int surface_w, int surface_h)
{
    if (pct == 0) return 1.0f;
    if (pct > 0)  return (float)pct / 100.0f;
    if (surface_w <= 0 || surface_h <= 0) return 1.0f;

    float have = (float)surface_w / (float)surface_h;
    if (!(have > 0.0f)) return 1.0f;              /* NaN-safe by construction */

    const float authored = 4.0f / 3.0f;
    float k = authored / have;

    if (k > 1.0f) k = 1.0f;    /* a 4:3 or narrower surface needs nothing; never STRETCH */
    if (k < 0.25f) k = 0.25f;  /* a floor, not a cliff */
    return k;
}

/* ---- WHICH ELEMENTS ARE CORRECTED AT ALL -----------------------------------------------------
 *
 * Alex, 2026-08-14, having seen the whole interface corrected at once: the radar and the
 * speedometer came out round, which is what he asked for - and the rest came out wrong. The
 * mission text at the bottom left had moved, and the menu was laid out incorrectly. His decision,
 * in his words: fix only the radar with its compass, and only the speedometer; leave every other
 * piece of the interface alone. And: only while sitting in a car.
 *
 * So this is a positive test - a draw is corrected only if it lies ENTIRELY inside one of two
 * corners - rather than the negative "everything except what looks full-screen" that produced the
 * broken text. Fractions of the surface, not pixels: the same rule then holds at 2560x1440 and on
 * an ultrawide, where a pixel rectangle would silently stop matching.
 *
 * The bounds are deliberately generous. A draw that pokes outside its corner is left untouched,
 * which fails towards "the game as it was" rather than towards a moved element.
 */
#define HUD_ZONE_NONE   0
#define HUD_ZONE_RADAR  1
#define HUD_ZONE_DIAL   2

/* SQUEEZE A ZONE ABOUT ITS OWN CENTRE, NOT ABOUT A SCREEN EDGE.
 *
 * Alex, 2026-08-14, with a frame of the radar: the ring came out round, and the white blips for
 * the traffic sat too far right and spilled over the edge of it. His own suggestion, and it is
 * the right one: *"maybe just shift the radar to the right"* - i.e. keep the radar where
 * it is instead of moving what is drawn inside it.
 *
 * The blips are not ours to move. They are drawn from a vertex BUFFER through
 * DrawIndexedPrimitive, and this module only ever rewrites vertices the game hands over in
 * memory (the ...UP calls). So the ring was being pulled toward the left edge of the screen by
 * `x * k` while the blips stayed exactly where the engine put them - a systematic offset that
 * grows with distance from that edge, which is precisely what the picture shows.
 *
 * Anchoring the correction to the ELEMENT's own centre removes the offset: the ring keeps its
 * position and its centre, only its width changes. Blips near the centre then land right; blips
 * at the rim can still overhang, because nothing has squeezed THEM - that part needs the vertex
 * buffer and is a separate job. This is strictly better than the current picture and it is what
 * he asked for.
 */
static float hud_map_x_about(float x, float cx, float k)
{
    return cx + (x - cx) * k;
}

/* 1 when [x0,x1] x [y0,y1] fits inside the corner AND is no bigger than the element that lives
 * there.
 *
 * THE SIZE LIMIT IS WHAT KEEPS THE PAUSE MENU OUT, and it was earned. Alex, 2026-08-14: pressing
 * Escape squeezed the top three menu entries. Being in a menu does not make the car go away - the
 * engine keeps drawing the scene behind it and our "sitting in a car" test stays true, correctly -
 * so a rule based on game state cannot separate them. A rule based on SIZE can, because the two
 * things are not the same size:
 *
 *     the radar ring, measured on this build   345 px of 1920  = 0.18 w
 *     a pause-menu entry in that same corner   480 px of 1920  = 0.25 w
 *
 * So the corner alone is not enough; the draw also has to be no wider than the instrument that
 * belongs there. Anything larger is something else drawn over it, and it is left alone.
 */
static int hud_zone_of(float x0, float x1, float y0, float y1, int w, int h)
{
    float fw = (float)w, fh = (float)h;
    float dw, dh;
    if (fw <= 0.0f || fh <= 0.0f) return HUD_ZONE_NONE;
    dw = x1 - x0; dh = y1 - y0;
    /* The radar and its compass ring: top left. Measured on this build, 1920x1080:
           the ring            x 69..415, w 346 = 0.18 w
           a pause-menu entry  x 47..527, w 480 = 0.25 w   <- must NOT match
       so the width limit sits between them. */
    if (x0 >= -0.02f * fw && x1 <= 0.32f * fw && y0 >= -0.02f * fh && y1 <= 0.36f * fh &&
        dw <= 0.22f * fw && dh <= 0.34f * fh)
        return HUD_ZONE_RADAR;
    /* The speedometer, bottom right, and it is SEVERAL draws that have to move together:
           the outer blue ring x 1324..1898, w 574 = 0.30 w
           the white dial face x 1390..1719, w 329 = 0.17 w
           the gear box        x 1653..1713, w  60
           the small gauge     x 1819..1896, w  77
       A limit of 0.26 w on 2026-08-14 admitted the face and REFUSED the ring, so the face was
       squeezed inside a ring that was not - which is the broken dial Alex photographed. The
       limit has to clear the widest PART of the instrument, hence 0.32 w. */
    if (x0 >= 0.68f * fw && x1 <= 1.02f * fw && y0 >= 0.62f * fh && y1 <= 1.02f * fh &&
        dw <= 0.32f * fw && dh <= 0.38f * fh)
        return HUD_ZONE_DIAL;
    return HUD_ZONE_NONE;
}

/* ---- WHERE THE SQUEEZED PIXELS GO ------------------------------------------------------------
 *
 * Squeezing everything about the CENTRE fixes every shape and moves every corner inward: the
 * speedometer leaves its corner, and the screen ends up as a 4:3 island with empty margins. Alex,
 * 2026-08-12, translated: "ideally the interface should be cut in half, so that the corners stay
 * where they were and the pixels are simply inserted in the middle".
 *
 * So each draw is squeezed about the EDGE it belongs to:
 *
 *     an element entirely in the left half   ->  x' = x * k          (its left edge is x = 0)
 *     an element entirely in the right half  ->  x' = w - (w-x) * k  (its right edge is x = w)
 *     an element that crosses the middle     ->  about the centre, as before
 *
 * Two properties make this safe, and both are worth stating because they are why it does not need
 * per-element bookkeeping:
 *
 *   - Every element in the same half gets the SAME mapping, so two draws that make up one widget
 *     (a dial and its needle) keep their exact relative positions. The anchor is the edge of the
 *     SCREEN, never the edge of the element.
 *   - Anything spanning the middle - centred subtitles, a mission banner - keeps its own centre,
 *     so nothing is torn in half by the gap. The gap is where the extra pixels go, and Alex's
 *     point is that in play there is nothing in the middle of the screen to displace.
 *
 * Shapes are fixed identically in all three cases: only the anchor differs, and the scale is k
 * everywhere. */
enum { HUD_ANCHOR_CENTRE = 0, HUD_ANCHOR_LEFT = 1, HUD_ANCHOR_RIGHT = 2 };

/* THE EDGE BANDS ARE NARROW ON PURPOSE, and this is the whole safety argument.
 *
 * Alex raised the case that decides it, 2026-08-12: mission text. If the engine draws a line of
 * text as ONE draw, an anchor that splits at the exact middle is harmless - the line crosses the
 * middle and gets the centred treatment. But if it draws text one GLYPH at a time, each glyph is
 * its own draw with its own span, and a split at the middle would send the first half of a
 * sentence left and the second half right - the sentence tears in two, with the inserted pixels
 * landing inside it.
 *
 * So a draw is only anchored to an edge when it lies ENTIRELY in the outer third. Anything
 * touching the middle third - which is where centred text lives - keeps the old centre squeeze
 * and cannot be torn by this at all. The corners, which is what he asked for, are all well inside
 * the outer bands: a speedometer, a health counter, a radar.
 *
 * A line of per-glyph text that runs from the far left to the far right would still be split, and
 * that is why `hud_probe` exists: it logs the span of every screen-space draw so the question
 * "how is text drawn" is answered from a log rather than from this comment.
 */
#define HUD_EDGE_BAND 0.3333f

/* Classify one draw from the horizontal span of its vertices. `minX`/`maxX` are that span. */
static int hud_aspect_anchor(float minX, float maxX, int surface_w)
{
    float w = (float)surface_w;
    if (maxX <= w * HUD_EDGE_BAND)          return HUD_ANCHOR_LEFT;
    if (minX >= w * (1.0f - HUD_EDGE_BAND)) return HUD_ANCHOR_RIGHT;
    return HUD_ANCHOR_CENTRE;
}

/* Map one X. `anchored` 0 reproduces the centre-squeeze exactly, byte for byte, so the old
   behaviour remains reachable from the ini for an A/B rather than living only in git history. */
static float hud_aspect_map_x(float x, int anchor, int anchored, int surface_w, float k)
{
    float w = (float)surface_w;
    if (anchored && anchor == HUD_ANCHOR_LEFT)  return x * k;
    if (anchored && anchor == HUD_ANCHOR_RIGHT) return w - (w - x) * k;
    return w * 0.5f + (x - w * 0.5f) * k;
}

#endif /* HUD_ASPECT_H */
