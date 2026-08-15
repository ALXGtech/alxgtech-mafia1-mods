/* version.h - the one place the product version is written down.
 *
 * Alex, 2026-08-12, translated: "when the version is not visible it is very inconvenient". It is
 * now in the bottom right corner of the window, in the small grey face, on every tab.
 *
 * ONE definition, and tools\make-release.ps1 REFUSES to build an archive whose -Version does not
 * match this string. A label that says 1.0.9 on a 1.1.0 archive is worse than no label: it is a
 * number somebody will quote back when reporting a fault.
 */
#ifndef ALXG_VERSION_H
#define ALXG_VERSION_H

#define ALXG_VERSION "1.4.3"

/* THE NAME, in ONE place. Alex settled it on 2026-08-12 in two steps: first "ALXG Tech Mafia
   Mods", then - having thought about it - a single-word nickname. His reason, translated:
   "written ALXGtech, you can see which part is the name and which part is the industry", to be
   used for everything, including the YouTube channels when he rebrands them.
   Written with that exact case everywhere it is shown. It is never upper-cased in the UI: the
   capitals are what separate the name from the trade, and ALXGTECH throws that away. */
/* THE GAME'S NUMBER IS PART OF THE NAME. Alex, 2026-08-12, translated: "I may potentially work on
   other Mafia games in the future, and I would like to avoid confusion" - so this is Mafia 1's
   mod suite, said in the name, not "the Mafia mods" that a Mafia II suite would then have to
   argue with. */
#define BRAND_NAME  "ALXGtech Mafia 1 Mods"
#define BRAND_OWNER "ALXGtech"
/* The repository, in full: github.com/ALXGtech/alxgtech-mafia1-mods. Two decisions in that string,
   both his: the long form over my shorter ALXGtech/mafia-mods, because a repository name travels
   without its owner - and the GAME NUMBER, so a future Mafia II suite is a different repository
   rather than a source of confusion with this one. */
#define BRAND_SLUG  "alxgtech-mafia1-mods"

#endif
