/* ui_skin.h - the Mafia skin: paper, ink, fonts and owner-drawn controls.
 *
 * Lifted from src/spike/ffb_gui.c, which lifted it from gearbox_gui.c. This is the third copy
 * and the LAST one: both spike tools are merged into this program by the end of plan 2 and
 * their copies go away with them.
 *
 * Design request, 2026-07-24: the app's design was to follow the spirit of the menus from Mafia. The reference is the
 * game's Player Controls screen - crumpled off-white paper with torn sooty edges over a dark
 * room. The paper is GENERATED at runtime from a fixed seed, so it is the same sheet on every
 * launch and the exe stays one portable file with no assets beside it.
 *
 * Every comment in here about a drawing trap was paid for once already
 * before this file existed. Read them before simplifying anything.
 *
 * The scroll helpers are NOT here. The spike tools had one canvas each and could keep the
 * scroll position in a global; this program has four pages, so scrolling lives in launcher.c
 * where the per-page windows are.
 */
#ifndef ALXG_UI_SKIN_H
#define ALXG_UI_SKIN_H

#include <windows.h>

/* ---- the tiny string helpers, so the file needs no CRT string.h ------------------------- */
static int  SLen(const char *s){ int n=0; while(s[n])n++; return n; }
static void SCat(char *d,const char *s){ int n=SLen(d),i=0; while(s[i]) d[n+i]=s[i],i++; d[n+i]=0; }
static void SCpy(char *d,const char *s){ int i=0; while(s[i]) d[i]=s[i],i++; d[i]=0; }
static int  SEq(const char *a,const char *b){ int i=0; for(;;i++){ if(a[i]!=b[i]) return 0; if(!a[i]) return 1; } }
static int  Clamp(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int  ParseInt(const char *s,int *out){
    int v=0,any=0,i=0;
    while(s[i]==' ') i++;
    for(;s[i]>='0'&&s[i]<='9';i++){ v=v*10+(s[i]-'0'); any=1; if(v>100000) v=100000; }
    while(s[i]==' ') i++;
    if(!any||s[i]) return 0;
    *out=v; return 1;
}

/* ---- the palette. ONE MEANING PER COLOUR, settled after two correction passes ---------------
 *   green = this is what is in force RIGHT NOW      (Mod is ENABLED, the selected range)
 *   red   = a finger on the button, or a fault      (never a selection)
 *   bold, no colour = this is the RECOMMENDED one
 * The rule is worth more than the pixels: it lets a user answer "am I still on the approved
 * feel" by glancing rather than by comparing numbers. */
#define INK        RGB(24,20,16)
#define INK_SOFT   RGB(78,70,60)
#define PAPER      RGB(232,228,216)
#define FIELD      RGB(246,244,238)
#define ROOMDARK   RGB(30,26,22)
#define MAFIA_RED  RGB(150,22,22)
#define MAFIA_GREEN RGB(32,94,42)
#define MAFIA_AMBER RGB(150,104,20)
#define INK_OFF    RGB(150,144,134)   /* a disabled control: greyed, and it must LOOK greyed */

static HFONT g_fTitle,g_fBody,g_fField,g_fAction,g_fSmall,g_fSmallCaps,g_fBanner,g_fRange,
             g_fTab,g_fSection,g_fSub,g_fBig,g_fBrand;
static HBRUSH g_brPaper,g_brField,g_brRoom,g_brSheet;

static HBITMAP g_paper; static int g_paperW,g_paperH;

static unsigned g_rnd=0x1B3F5D7u;
static unsigned Rnd(void){ g_rnd=g_rnd*1664525u+1013904223u; return (g_rnd>>16)&0x7FFF; }

/* Creases, grain and burnt edges, from a fixed seed - the same sheet every launch.
 *
 * The sheet only ever GROWS, and it is generated to whatever is asked for plus room to spare.
 * It used to be regenerated at exactly the requested size, which meant every resize produced a
 * different sheet - and because the burnt edge is drawn along the sheet's OWN border, dragging
 * the window taller painted rows of scorch marks across the middle of the page. Reported 2026-08-01,
 * with a screenshot of it. */
static void MakePaper(HDC ref,int w,int h){
    if(g_paper&&g_paperW>=w&&g_paperH>=h) return;
    if(g_paper) DeleteObject(g_paper);
    if(w<g_paperW) w=g_paperW;
    if(h<g_paperH) h=g_paperH;
    w+=200; h+=400;                 /* room to grow, so a drag does not regenerate at all */
    g_paperW=w; g_paperH=h;
    HDC mem=CreateCompatibleDC(ref);
    g_paper=CreateCompatibleBitmap(ref,w,h);
    HGDIOBJ old=SelectObject(mem,g_paper);
    RECT all={0,0,w,h}; FillRect(mem,&all,g_brPaper);
    g_rnd=0x1B3F5D7u;
    for(int i=0;i<90;i++){
        int x1=(int)(Rnd()%(unsigned)w), y1=(int)(Rnd()%(unsigned)h);
        int x2=x1+(int)(Rnd()%400)-200, y2=y1+(int)(Rnd()%160)-80;
        int light=(int)(Rnd()&1);
        HPEN pen=CreatePen(PS_SOLID,1,light?RGB(245,243,235):RGB(212,206,192));
        HGDIOBJ op=SelectObject(mem,pen);
        MoveToEx(mem,x1,y1,NULL); LineTo(mem,x2,y2);
        SelectObject(mem,op); DeleteObject(pen);
    }
    for(int i=0;i<w*h/60;i++){
        int x=(int)(Rnd()%(unsigned)w), y=(int)(Rnd()%(unsigned)h);
        SetPixel(mem,x,y,(Rnd()&1)?RGB(216,211,198):RGB(243,240,231));
    }
    /* NO BURNT EDGE ALONG THE TOP. It is the one border a control sits against - the build line
       and the big banner both start there - and scorch marks behind text that has to be read at
       a glance cost more than the decoration is worth. Settled 2026-08-01. The other three keep
       it: nothing is set into them. */
    for(int i=0;i<1400;i++){
        int edge=1+(int)(Rnd()%3), d=(int)(Rnd()%15), r=1+(int)(Rnd()%2);
        int x,y;
        if(edge==1){ x=(int)(Rnd()%(unsigned)w); y=h-1-d; }
        else if(edge==2){ x=d; y=(int)(Rnd()%(unsigned)h); }
        else { x=w-1-d; y=(int)(Rnd()%(unsigned)h); }
        int k=205-d*11; if(k<45) k=45;
        HBRUSH b=CreateSolidBrush(RGB(k+20,k+14,k));
        RECT rr={x-r,y-r,x+r,y+r}; FillRect(mem,&rr,b); DeleteObject(b);
    }
    SelectObject(mem,old); DeleteDC(mem);
    if(g_brSheet) DeleteObject(g_brSheet);
    g_brSheet=CreatePatternBrush(g_paper);
}

static void SkinInit(void){
    g_brPaper=CreateSolidBrush(PAPER);
    g_brField=CreateSolidBrush(FIELD);
    g_brRoom =CreateSolidBrush(ROOMDARK);
    g_fTitle =CreateFontA(27,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe Script");
    g_fBody  =CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Georgia");
    g_fField =CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fAction=CreateFontA(17,0,0,0,FW_BOLD,  0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    /* Arial, not Georgia, and the reason is in the text: Georgia ships OLD-STYLE figures, so its
       zero is x-height and round. On screen "0 silences the tap" read as "o silences the tap" -
       on the one slider whose whole point is that it reaches zero. */
    g_fSmall =CreateFontA(14,0,0,0,FW_NORMAL,1,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fSmallCaps=CreateFontA(12,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    /* the brand strip's wordmark: heavier and a shade larger than the small caps it used to be
       drawn in, to match the weight of the wordmark on the application icon */
    g_fBrand =CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fBanner=CreateFontA(20,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    /* the standing "this is live" sign. Bigger than anything else on the page, because a
       line the same size as the rest is a line nobody notices - reported 2026-08-01. */
    g_fBig   =CreateFontA(25,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fRange =CreateFontA(21,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fTab   =CreateFontA(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    /* a heading has to LOOK like a heading, not like the first sentence of the paragraph */
    g_fSection=CreateFontA(21,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Georgia");
    g_fSub    =CreateFontA(17,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Georgia");
}

/* a page canvas: paper only. The dark band with the tabs is painted by the frame above it. */
static void PaintBackdrop(HWND h,HDC dc){
    RECT rc; GetClientRect(h,&rc);
    MakePaper(dc,rc.right,rc.bottom);
    HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_paper);
    BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);
    SelectObject(mem,old); DeleteDC(mem);
}

/* An owner-drawn control is handed a DC nobody erased, and the parent is WS_CLIPCHILDREN so it
 * never paints underneath one. Whatever the control drew LAST is still there: a label that
 * changes ("Enable mod" -> "Disable mod") lands on top of its predecessor, and a focus underline
 * outlives the focus - both seen on screen. So every owner-drawn branch starts by putting the
 * backdrop back, cut from the same paper bitmap at the same offset, so the grain continues the
 * window instead of restarting inside each button. */
static void PaperBlit(HDC dc,HWND item,const RECT *r){
    POINT o={0,0}; HWND par=GetParent(item);
    if(par) MapWindowPoints(item,par,&o,1);
    RECT rr=*r;
    if(!g_paper){ FillRect(dc,&rr,g_brPaper); return; }
    HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_paper);
    BitBlt(dc,r->left,r->top,r->right-r->left,r->bottom-r->top,
           mem,o.x+r->left,o.y+r->top,SRCCOPY);
    SelectObject(mem,old); DeleteDC(mem);
}

/* ---- how a button says it is being pressed ---------------------------------------------------
 * NOT by turning red. Design note, 2026-07-27: red was reserved for errors, and it was required
 * to hold across every tool this project ships. So a press is drawn the way a physical
 * button behaves: the face darkens, a shadow appears along the top and left edges, and the label
 * shifts one pixel down and right, as if the cap had gone into the panel. It survives being
 * photographed in black and white, and it leaves red free to mean the only thing it should.
 *
 * `off`   = disabled. A greyed page's buttons must LOOK greyed, or the page reads as broken
 *           rather than as switched off.
 * `state` = in force right now, drawn green. */
static void DrawPressable(HDC dc,const RECT *rr,const char *txt,int pressed,int off,int primary,int state)
{
    RECT r=*rr;
    COLORREF edge = off ? RGB(180,174,164) : state ? MAFIA_GREEN
                  : pressed ? RGB(96,88,78) : INK;
    COLORREF ink  = off ? INK_OFF : state ? MAFIA_GREEN : INK;
    HBRUSH b=CreateSolidBrush(pressed?RGB(206,200,188):state?RGB(224,238,222):FIELD);
    FillRect(dc,&r,b); DeleteObject(b);
    HPEN pen=CreatePen(PS_SOLID,(primary||state)?2:1,edge);
    HGDIOBJ op=SelectObject(dc,pen), ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,r.left,r.top,r.right,r.bottom);
    SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(pen);
    if(pressed){
        HPEN p2=CreatePen(PS_SOLID,1,RGB(168,160,148)); HGDIOBJ o2=SelectObject(dc,p2);
        MoveToEx(dc,r.left+1,r.bottom-2,NULL); LineTo(dc,r.left+1,r.top+1); LineTo(dc,r.right-1,r.top+1);
        SelectObject(dc,o2); DeleteObject(p2);
    }
    SetTextColor(dc,ink);
    if(pressed){ r.left+=1; r.top+=1; }
    /* DT_NOPREFIX or "Apply & Launch" draws as "Apply _Launch" - DrawText eats the ampersand as
       a mnemonic marker, and an owner-drawn button has no mnemonics to mark. */
    DrawTextA(dc,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

/* the small red arrow beside the active row, straight off Mafia's own controls screen */
static void RedArrow(HDC dc,int x,int cy){
    POINT p[3]={{x,cy-6},{x+8,cy},{x,cy+6}};
    HBRUSH b=CreateSolidBrush(MAFIA_RED); HGDIOBJ ob=SelectObject(dc,b);
    HPEN pen=CreatePen(PS_SOLID,1,MAFIA_RED); HGDIOBJ op=SelectObject(dc,pen);
    Polygon(dc,p,3);
    SelectObject(dc,op); DeleteObject(pen); SelectObject(dc,ob); DeleteObject(b);
}

/* Fonts go on with WM_SETFONT and NEVER with SelectObject inside WM_CTLCOLOR*: selecting there
   changes what is painted but not the control's internal line metrics, so a multi-line static
   draws its lines ON TOP OF EACH OTHER. It reads as a repaint bug and is not one. */
static HWND MkText(HWND p,const char *s,int x,int y,int w,int h,int id){
    HWND t=CreateWindowExA(0,"STATIC",s,WS_CHILD|WS_VISIBLE,x,y,w,h,p,(HMENU)(INT_PTR)id,NULL,NULL);
    SendMessageA(t,WM_SETFONT,(WPARAM)g_fBody,TRUE);
    return t;
}
static HWND MkTextF(HWND p,const char *s,int x,int y,int w,int h,int id,HFONT f,DWORD extra){
    HWND t=CreateWindowExA(0,"STATIC",s,WS_CHILD|WS_VISIBLE|extra,x,y,w,h,p,(HMENU)(INT_PTR)id,NULL,NULL);
    SendMessageA(t,WM_SETFONT,(WPARAM)f,TRUE);
    return t;
}
static HWND MkBtn(HWND p,const char *s,int x,int y,int w,int h,int id){
    return CreateWindowExA(0,"BUTTON",s,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,x,y,w,h,p,
                           (HMENU)(INT_PTR)id,NULL,NULL);
}

#endif /* ALXG_UI_SKIN_H */
