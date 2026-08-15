/* ui_tabs.h - the tab strip. It SWITCHES PAGES.
 *
 * This replaces src/spike/alxg_tabs.h, whose click handler launched another exe because the
 * mods used to be four separate setup tools. They are one program now (spec section 3), so a
 * click shows a page: same drawing, different meaning.
 *
 * The strip fills the dark band edge to edge and floor to ceiling. The open tab is cut from the
 * same paper as the sheet below and carries no bottom edge, so it runs into the page. The tabs
 * are divided by pixel arithmetic rather than a fixed width, so the last one always reaches the
 * right edge exactly, with no one-pixel sliver of band showing past it.
 *
 * Requires, from the consumer: PAPER / ROOMDARK / INK, g_fAction, g_fTab, LTABS / LNTAB, and a
 * PageShow(int,int) - because what has to be shown and hidden is the VIEWPORT the page is parked
 * in, and this header knows nothing about viewports.
 */
#ifndef ALXG_UI_TABS_H
#define ALXG_UI_TABS_H

/* 28 rather than 24 since 2026-08-12: the strip now carries the application icon and the version
   beside the wordmark, and 24 left the 16 px icon touching both edges of the band. */
#define BRANDSTRIP 28
#define TABH       40
#define TITLEBAND  (BRANDSTRIP+TABH)
#define TABICON    24            /* the icons ship 16/32/48; 24 is drawn from the 32 frame */
#define TABICONGAP 10

static HWND  g_tab[LNTAB];     /* the buttons */
static HICON g_tabIcon[LNTAB];
static int   g_curTab = 0;

static void PageShow(int i,int on);

static void TabCreate(HWND frame,int cw,int idBase){
    HINSTANCE hi=GetModuleHandleA(NULL);
    for(int i=0;i<LNTAB;i++){
        int x0=cw*i/LNTAB, x1=cw*(i+1)/LNTAB;
        /* LoadImage with an explicit size, NOT LoadIcon: LoadIcon hands back the system's large
           frame and stretching 32 down to 24 in the DC blurs strokes the icon was hinted to keep
           crisp. Asking for the size we draw at lets the loader pick the nearest frame itself.
           A tab with no icon (VR) keeps NULL and simply centres its label. */
        g_tabIcon[i]= LTABS[i].icon
                    ? (HICON)LoadImageA(hi,MAKEINTRESOURCEA(LTABS[i].icon),IMAGE_ICON,
                                        TABICON,TABICON,LR_DEFAULTCOLOR)
                    : NULL;
        g_tab[i]=CreateWindowExA(0,"BUTTON",LTABS[i].name,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            x0,BRANDSTRIP,x1-x0,TABH,frame,(HMENU)(INT_PTR)(idBase+i),NULL,NULL);
    }
}

static void TabDraw(DRAWITEMSTRUCT *d,int idx){
    char txt[160]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
    RECT r=d->rcItem;
    int pressed=(d->itemState&ODS_SELECTED)!=0;
    int self=(idx==g_curTab);
    int last=(idx==LNTAB-1);
    SetBkMode(d->hDC,TRANSPARENT);

    HBRUSH b=CreateSolidBrush(self ? PAPER : pressed ? RGB(64,56,46) : ROOMDARK);
    FillRect(d->hDC,&r,b); DeleteObject(b);
    if(self){
        /* a hairline down each side, so the open tab has an edge without being a box */
        HPEN pen=CreatePen(PS_SOLID,1,RGB(96,88,76)); HGDIOBJ op=SelectObject(d->hDC,pen);
        MoveToEx(d->hDC,r.left,r.bottom,NULL);  LineTo(d->hDC,r.left,r.top);
        MoveToEx(d->hDC,r.right-1,r.top,NULL);  LineTo(d->hDC,r.right-1,r.bottom);
        SelectObject(d->hDC,op); DeleteObject(pen);
    } else if(!last){
        HPEN pen=CreatePen(PS_SOLID,1,RGB(58,51,43)); HGDIOBJ op=SelectObject(d->hDC,pen);
        MoveToEx(d->hDC,r.right-1,r.top+8,NULL); LineTo(d->hDC,r.right-1,r.bottom-8);
        SelectObject(d->hDC,op); DeleteObject(pen);
    }
    HGDIOBJ of=SelectObject(d->hDC,self?g_fAction:g_fTab);
    SetTextColor(d->hDC, self ? INK : RGB(212,204,190));
    RECT t=r; if(pressed&&!self) t.top+=1;

    /* ICON FIRST, THEN THE LABEL - settled 2026-08-12. The pair is centred AS ONE BLOCK, which is
       why the text width has to be measured before anything is drawn: centring the label in the
       whole tab and then putting an icon to its left shifts the label off centre by half an icon
       and the four tabs stop lining up with each other. */
    if(g_tabIcon[idx]){
        RECT m=t; int tw,x,iy;
        DrawTextA(d->hDC,txt,-1,&m,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        tw=m.right-m.left;
        x=t.left+((t.right-t.left)-(TABICON+TABICONGAP+tw))/2;
        iy=t.top+((t.bottom-t.top)-TABICON)/2;
        DrawIconEx(d->hDC,x,iy,g_tabIcon[idx],TABICON,TABICON,0,NULL,DI_NORMAL);
        t.left=x+TABICON+TABICONGAP;
        DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    } else {
        DrawTextA(d->hDC,txt,-1,&t,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    SelectObject(d->hDC,of);
}

static void TabSelect(int idx){
    if(idx<0||idx>=LNTAB) return;
    g_curTab=idx;
    for(int i=0;i<LNTAB;i++){
        PageShow(i, i==idx);
        if(g_tab[i]) InvalidateRect(g_tab[i],NULL,TRUE);
    }
}

#endif /* ALXG_UI_TABS_H */
