/* ui_about.h - the About box: who made this, which version it is, and where to find the rest.
 *
 * Alex asked for it on 2026-08-12, and dictated the content: done by ALXG Tech, aka GanJJ, aka
 * Alex, plus five links in a fixed order - the mod's repository first, then the English YouTube
 * channel, the Russian one, the Telegram tech channel and the Telegram lifestyle channel.
 *
 * THE URLS ARE NOT GUESSED. They were taken from the author's own channel notes, cross-checked
 * against four other files that agree with them. A dead link in a shipped About box is worse than
 * no About box: it is the program telling a stranger the author does not exist. If a channel is
 * renamed, this table is the one place to change.
 *
 * Not AlxgBox: that box draws one paragraph and two buttons. Links have to be clickable, so this
 * is its own small window class in the same skin.
 *
 * Requires: the skin (fonts, brushes, MkBtn, DrawPressable), ALXG_VERSION, IDI_APP, g_frame.
 */
#ifndef ALXG_UI_ABOUT_H
#define ALXG_UI_ABOUT_H

/* Included HERE rather than relied on from launcher.c: that file includes version.h two hundred
   lines below this header, so the string literal would be pasted before the macro existed. */
#include "version.h"

#define AB_W        560
#define AB_PAD      24
#define AB_ICON     64
#define AB_LINKH    26
#define AB_ID_CLOSE 1
#define AB_ID_LINK0 20

typedef struct { const char *label; const char *url; } about_link;

/* HIS ORDER, and the repository is deliberately first. */
static const about_link AB_LINKS[] = {
    /* THE LONG FORM, his decision of 2026-08-12 after I argued for the short one. I suggested
       ALXGtech/mafia-mods because the organisation already spells the nickname; he chose the long
       form, and it is the better answer for the place the name actually travels to - a bare
       repository name is quoted, cloned and listed without its owner, and
       "mafia-mods" alone says nothing about whose it is. Lower case in the path because a URL is
       case-sensitive and this one gets retyped by hand. */
    { BRAND_NAME " on GitHub",          "https://github.com/ALXGtech/" BRAND_SLUG },
    { "GanJJ Games - YouTube (EN)",     "http://youtube.com/@GanJJGames" },
    { "Alex Goncharov - YouTube (RU)",  "http://youtube.com/@AlexGoncharovYT" },
    { "GanJJ Games - Telegram",         "https://t.me/ganjjgames" },
    { "China Life - Telegram",          "https://t.me/china_life" },
};
#define AB_NLINK ((int)(sizeof(AB_LINKS)/sizeof(AB_LINKS[0])))

static HICON g_abIcon;
static int   g_abDone;

static LRESULT CALLBACK AboutProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_ERASEBKGND: {
        HDC dc=(HDC)w; RECT rc; GetClientRect(h,&rc);
        int tx=AB_PAD+AB_ICON+18;
        FillRect(dc,&rc,g_brPaper);
        SetBkMode(dc,TRANSPARENT);
        if(g_abIcon) DrawIconEx(dc,AB_PAD,AB_PAD,g_abIcon,AB_ICON,AB_ICON,0,NULL,DI_NORMAL);

        SelectObject(dc,g_fTitle); SetTextColor(dc,INK);
        { RECT t={tx,AB_PAD-2,rc.right-AB_PAD,AB_PAD+30};
          DrawTextA(dc,BRAND_NAME,-1,&t,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX); }

        SelectObject(dc,g_fSmall); SetTextColor(dc,INK_SOFT);
        { RECT t={tx,AB_PAD+34,rc.right-AB_PAD,AB_PAD+52};
          /* Two version numbers and they are different things: ours, and the GAME build these
             mods are made for. A user reporting a fault needs both, and confusing them is how a
             mod gets blamed for running on a release it was never built against. */
          DrawTextA(dc,"version " ALXG_VERSION "  -  for Mafia: The City of Lost Heaven, "
                       "GOG release v1.3",
                    -1,&t,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX); }

        SelectObject(dc,g_fBody); SetTextColor(dc,INK);
        { RECT t={AB_PAD,AB_PAD+AB_ICON+16,rc.right-AB_PAD,AB_PAD+AB_ICON+70};
          /* One nickname, and the older ones after it. He is rebranding onto ALXGtech - the
             YouTube channels follow later - so the name that leads is the one that will still be
             his in a year, and GanJJ stays because that is what the channels are called TODAY. */
          DrawTextA(dc,"Made by " BRAND_OWNER " - GanJJ - Alex Goncharov.",-1,&t,
                    DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX); }

        SelectObject(dc,g_fSmall); SetTextColor(dc,INK_SOFT);
        { RECT t={AB_PAD,AB_PAD+AB_ICON+40,rc.right-AB_PAD,AB_PAD+AB_ICON+76};
          DrawTextA(dc,"An unofficial modification. Not affiliated with the game's authors "
                       "or its publisher.",-1,&t,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX); }
        return 1; }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l;
        int id=(int)w;
        char txt[160]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
        RECT r=d->rcItem;
        SetBkMode(d->hDC,TRANSPARENT);
        FillRect(d->hDC,&r,g_brPaper);
        if(id>=AB_ID_LINK0){
            /* A link, drawn rather than themed: the underline is a line under the MEASURED text,
               so it stops where the words stop instead of running the width of the control. */
            RECT m=r; int tw;
            HGDIOBJ of=SelectObject(d->hDC,g_fBody);
            SetTextColor(d->hDC,(d->itemState&ODS_SELECTED)?INK:MAFIA_RED);
            DrawTextA(d->hDC,txt,-1,&m,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
            tw=m.right-m.left;
            { RECT t=r; DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
            { HPEN pen=CreatePen(PS_SOLID,1,(d->itemState&ODS_SELECTED)?INK:MAFIA_RED);
              HGDIOBJ op=SelectObject(d->hDC,pen);
              int y=(r.top+r.bottom)/2+9;
              MoveToEx(d->hDC,r.left,y,NULL); LineTo(d->hDC,r.left+tw,y);
              SelectObject(d->hDC,op); DeleteObject(pen); }
            SelectObject(d->hDC,of);
            return TRUE;
        }
        { HGDIOBJ save=SelectObject(d->hDC,g_fAction);
          DrawPressable(d->hDC,&r,txt,(d->itemState&ODS_SELECTED)!=0,0,1,0);
          SelectObject(d->hDC,save); }
        return TRUE; }

    case WM_SETCURSOR:
        /* the hand only over the links, so the box says which words are clickable before they
           are clicked */
        if(GetDlgCtrlID((HWND)w)>=AB_ID_LINK0){ SetCursor(LoadCursorA(NULL,IDC_HAND)); return TRUE; }
        break;

    case WM_COMMAND: {
        int id=LOWORD(w);
        if(id==AB_ID_CLOSE){ g_abDone=1; return 0; }
        if(id>=AB_ID_LINK0&&id<AB_ID_LINK0+AB_NLINK){
            /* ShellExecute, not a hand-rolled browser launch: the user's default browser is the
               only correct answer and Windows already knows it. */
            ShellExecuteA(h,"open",AB_LINKS[id-AB_ID_LINK0].url,NULL,NULL,SW_SHOWNORMAL);
            return 0;
        }
        return 0; }

    case WM_CLOSE: g_abDone=1; return 0;
    }
    return DefWindowProcA(h,m,w,l);
}

/* Modal the same honest way AlxgBox is: disable the owner, pump until closed, enable it again -
   and enable it BEFORE destroying the box, or Windows hands the foreground to another program. */
static void AboutShow(HWND parent)
{
    MSG msg; RECT pr,want; HWND box; int i,y,h;

    if(!g_abIcon)
        g_abIcon=(HICON)LoadImageA(GetModuleHandleA(NULL),MAKEINTRESOURCEA(IDI_APP),IMAGE_ICON,
                                   AB_ICON,AB_ICON,LR_DEFAULTCOLOR);

    y=AB_PAD+AB_ICON+86;                       /* under the credit and the disclaimer */
    h=y+AB_NLINK*AB_LINKH+18+DLG_BTN_H+AB_PAD;

    want.left=0; want.top=0; want.right=AB_W; want.bottom=h;
    AdjustWindowRect(&want,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE);
    GetWindowRect(parent,&pr);
    box=CreateWindowExA(WS_EX_DLGMODALFRAME,"MafiaAbout","About " BRAND_NAME,
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        pr.left+((pr.right-pr.left)-(want.right-want.left))/2,
        pr.top+((pr.bottom-pr.top)-(want.bottom-want.top))/3,
        want.right-want.left,want.bottom-want.top,parent,NULL,NULL,NULL);
    if(!box) return;

    /* Each link is only as WIDE AS ITS WORDS. A control stretched to the margin would take the
       click - and show the hand cursor - over empty paper to the right of the text, which reads
       as a link that is somehow longer than it looks. */
    { HDC dc=GetDC(box); HGDIOBJ of=SelectObject(dc,g_fBody);
      for(i=0;i<AB_NLINK;i++){
          SIZE sz; int w;
          GetTextExtentPoint32A(dc,AB_LINKS[i].label,(int)strlen(AB_LINKS[i].label),&sz);
          w=sz.cx+6; if(w>AB_W-2*AB_PAD) w=AB_W-2*AB_PAD;
          MkBtn(box,AB_LINKS[i].label,AB_PAD,y+i*AB_LINKH,w,AB_LINKH-2,AB_ID_LINK0+i);
      }
      SelectObject(dc,of); ReleaseDC(box,dc); }
    MkBtn(box,"Close",AB_W-AB_PAD-DLG_BTN_W,y+AB_NLINK*AB_LINKH+18,DLG_BTN_W,DLG_BTN_H,AB_ID_CLOSE);

    g_abDone=0;
    EnableWindow(parent,FALSE);
    ShowWindow(box,SW_SHOW);
    UpdateWindow(box);
    while(!g_abDone&&GetMessageA(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE) break;
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
    EnableWindow(parent,TRUE);
    DestroyWindow(box);
    SetActiveWindow(parent);
}

#endif /* ALXG_UI_ABOUT_H */
