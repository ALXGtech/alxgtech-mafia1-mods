/* page_vr.h - the VR tab. Present, and honest about why it does nothing.
 *
 * Spec 3.4. The VR mod is a separate piece of work, it installs into its own copy of the game,
 * and no binary of it has ever been handed to this program. A tab that pretends to work is worse
 * than one that says why it does not.
 *
 * The switch is DISABLED rather than absent: the tab has to look like the other three, or the
 * VR mod reads as something this program has never heard of rather than as something it cannot
 * install for you.
 */
#ifndef ALXG_PAGE_VR_H
#define ALXG_PAGE_VR_H

static HWND g_vrReason;

static void PageVrCreate(HWND h){
    int y;
    /* The switch lives on the frame now and RefreshPage disables it for a tab with no mod
       tag, so there is nothing to switch off here - only to explain. */
    PageHeader(h,LTAB_VR);
    y=PageBanner(h,LTAB_VR,PAGE_TOP);
    MkText(h,LTABS[LTAB_VR].blurb,26,y,WINW-52,22,0); y+=30;
    /* COMING SOON, in those words. Design note, 2026-08-07: the VR mod was not ready yet,
       so a coming soon placeholder was added there. The old wording explained an
       ARRANGEMENT - separate project, own copy of the game - which reads as "works, elsewhere"
       to somebody who has just installed this and is looking for the VR switch. What they need
       first is that it is not ready. The rest stays, because it is still true and it is what
       makes the empty tab make sense. */
    g_vrReason=MkTextF(h,
        "COMING SOON - the VR mod is not finished, and this tab installs nothing yet.",
        /* 24 high, not 22: the section face is 21 px and a 22 px static clips its descenders.
           That is trap 1 on this window's list - a control sized to the text it happens to
           hold today. */
        26,y,WINW-52,24,0,g_fSection,0); y+=30;
    MkTextF(h,"It is built in a separate project, into its own copy of the game. It also drives "
              "the same camera the First person tab does, and only one of the two can have it "
              "at a time.",26,y,WINW-52,22,0,g_fField,0);
    y+=40;
    g_contentH[LTAB_VR]=y;
}

#endif /* ALXG_PAGE_VR_H */
