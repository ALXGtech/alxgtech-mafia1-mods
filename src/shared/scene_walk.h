/* scene_walk.h - walking LS3D's scene graph without trusting it.
 *
 * The graph belongs to the game and another thread is editing it while we read. So this walk
 * assumes nothing: every pointer is checked before it is followed, the depth and the node count
 * are capped, and the stack is explicit. A recursive walk here is a stack overflow waiting for a
 * cycle - and a cycle in a graph we do not own is indistinguishable from a very deep scene.
 *
 * It lives in its own header for one reason: it is the only part of the head-hiding work that
 * can be tested with no game at all. `tests/offline/scene_walk_verify.c` builds a synthetic
 * graph - a tree, a cycle, a chain deeper than the cap, a node with a dead name pointer - and
 * runs THIS code over it. The dump that runs in the game is then the same walker that has
 * already been proven to terminate.
 *
 * Offsets and the chain they come from: the block at the end of car_anchor.h.
 * -nostdlib clean.
 */

#ifndef SCENE_WALK_H
#define SCENE_WALK_H

#include "car_anchor.h"

#define SCENE_WALK_STACK 256

typedef void (*SceneVisitFn)(DWORD node, int depth, void *user);

/* Returns the number of nodes visited. Stops at `maxNodes`, and never descends past `maxDepth`.
 * A sibling that does not fit on the stack is DROPPED rather than silently walked at the wrong
 * depth - the caller is told by the return value being short, and 256 levels of un-descended
 * siblings is already far past any real scene. */
static int SceneWalk(DWORD root, SceneVisitFn fn, void *user, int maxNodes, int maxDepth)
{
    if (!CarSane(root) || !fn) return 0;

    struct { DWORD node; int depth; } st[SCENE_WALK_STACK];
    int sp = 0, seen = 0;

    st[sp].node = root; st[sp].depth = 0; sp++;
    while (sp > 0 && seen < maxNodes) {
        DWORD node = st[--sp].node;
        int depth  = st[sp].depth;
        while (node && seen < maxNodes) {
            fn(node, depth, user);
            seen++;

            DWORD sib = FrameField(node, FRAME_SIBLING_OFF, 0);
            DWORD ch  = FrameField(node, FRAME_CHILD_OFF, 0);
            if (CarSane(sib) && sp < SCENE_WALK_STACK) {
                st[sp].node = sib; st[sp].depth = depth; sp++;
            }
            if (!CarSane(ch) || depth + 1 >= maxDepth) break;
            node = ch; depth++;
        }
    }
    return seen;
}

/* Every root of the scene, in the order the engine traverses them. */
static int SceneWalkAll(SceneVisitFn fn, void *user, int maxNodes, int maxDepth)
{
    DWORD scene = SceneObj();
    if (!scene) return 0;
    int total = 0;
    for (int r = 0; r < SCENE_NROOTS && total < maxNodes; r++) {
        DWORD root = SceneRoot(scene, r);
        if (root) total += SceneWalk(root, fn, user, maxNodes - total, maxDepth);
    }
    return total;
}

#endif /* SCENE_WALK_H */
