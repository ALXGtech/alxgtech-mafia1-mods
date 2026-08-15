/* ffb_ceil.h - v7.63: a MAXIMUM-force kick has to be corroborated by the world.
 *
 * Extracted into a header for ONE reason: so the project's offline test harness can drive the
 * exact arithmetic the mod runs, against the eight ceiling fires Alex labelled at the wheel.
 * (That harness needs the recorded drives to run and is not part of this repository.)
 * The camera's `fp_pose.h` was split out the same way and for the same reason - a guard that
 * has never been watched fail is not a verified guard.
 *
 * No windows.h, no globals from the mod: plain floats in, one float out.
 *
 * WHY THIS EXISTS and what the numbers are: see the CEIL_HIST block in mafia_ffb_v6.c. The
 * short version is that a phantom leaves the world's per-tick step untouched while a real
 * crash collapses it, measured over eight labelled events with a 1.35x margin.
 */

#ifndef FFB_CEIL_H
#define FFB_CEIL_H

#define CEIL_HIST    32u     /* ring depth; needs ticks -16..0, so 16 is not enough */
#define CEIL_BASE_LO 16u     /* baseline window, oldest tick back from the fire */
#define CEIL_BASE_HI  9u     /* baseline window, newest tick back from the fire  */
#define CEIL_NEED    17u     /* ticks of history required before the guard may act */

typedef struct {
    float raw[CEIL_HIST];
    unsigned n;
    float now;
} CeilRing;

static void CeilPush(CeilRing *r, float step)
{
    r->raw[r->n % CEIL_HIST] = step;
    r->n++;
    r->now = step;
}

/* Ratio of the world's step AT the firing tick to its median over ticks -16..-9.
 * Returns -1.0 when there is not enough history or no usable baseline - fail OPEN, because
 * this guard can only ever remove force and a guard that removes force must never act on
 * data it does not have.
 *
 * MEDIAN, not mean, and that one IS load-bearing: measured on the same eight events the mean
 * leaves a 1.28x margin against the median's 1.35x, and the suite's margin assertion fails
 * under it. Re-injectable with -DCEIL_REINJECT_MEAN.
 *
 * The zero skip is DEFENSIVE, not load-bearing, and the difference was checked rather than
 * assumed. The step channel drops the occasional 0.00 sample when two polls land inside one
 * position update; counting them anyway (-DCEIL_REINJECT_ZEROS) moves these eight ratios by
 * at most 0.01 and changes no verdict. It stays because a future baseline window could sit on
 * a run of them, not because it earns its keep today.
 */
static float CeilRatio(const CeilRing *r)
{
    float v[CEIL_BASE_LO - CEIL_BASE_HI + 1];
    unsigned n = 0, k, i, j;
    float med;
    if (r->n < CEIL_NEED) return -1.0f;
    for (k = CEIL_BASE_HI; k <= CEIL_BASE_LO; k++) {
        float s = r->raw[(r->n - 1u - k) % CEIL_HIST];
#ifdef CEIL_REINJECT_ZEROS
        v[n++] = s;              /* the defect: counting dropped samples in the baseline */
#else
        if (s > 0.01f) v[n++] = s;
#endif
    }
    if (n == 0) return -1.0f;
    for (i = 1; i < n; i++) {          /* insertion sort; n <= 8 */
        float t = v[i];
        for (j = i; j > 0 && v[j - 1] > t; j--) v[j] = v[j - 1];
        v[j] = t;
    }
#ifdef CEIL_REINJECT_MEAN
    {   /* the defect: the mean is dragged by the dropped samples and the spikes */
        float s = 0.0f;
        for (i = 0; i < n; i++) s += v[i];
        med = s / (float)n;
    }
#else
    med = (n & 1u) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5f;
#endif
    if (med <= 0.01f) return -1.0f;
    return r->now / med;
}

/* The decision itself. `agree` <= 0 means the guard is off, which is v7.62 behaviour exactly. */
static int CeilVeto(const CeilRing *r, float agree)
{
    float ratio;
    if (agree <= 0.0f) return 0;
    ratio = CeilRatio(r);
    if (ratio < 0.0f) return 0;
    return ratio > agree;
}

#endif
