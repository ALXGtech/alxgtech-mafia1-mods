/* ui_slider.h - the Mafia slider, a custom window class.
 *
 * A custom class and not a trackbar: comctl32 cannot be skinned, and one grey Windows control on
 * a sheet of Mafia paper reads as a defect.
 *
 * Lifted from src/spike/ffb_gui.c, with one change: it no longer reaches into that tool's row
 * tables. A page hands it a block of callbacks, so the FFB page can serve percentages and plan
 * 3's camera page can serve centimetres out of the same class.
 *
 * Requires from the consumer: PaperBlit, INK / INK_SOFT / FIELD / MAFIA_RED / MAFIA_GREEN, and
 * Clamp.
 */
#ifndef ALXG_UI_SLIDER_H
#define ALXG_UI_SLIDER_H

#define SLIDER_PAD 9

typedef struct {
    int  (*get)(int row);            /* current value, in SLIDER space: 0..travel */
    void (*set)(int row,int v);      /* the page's own setter - it marks dirty, repaints, etc */
    /* NOT called `max`: windows.h defines max(a,b) as a macro, so a struct field of that
       name expands at every use site and the errors point at this header rather than at
       the include that caused them. */
    int  (*travel)(int row);         /* slider travel, which may be less than the box */
    int  (*step)(int row);           /* arrow-key and drag granularity */
    /* THE REFERENCE MARK, in slider space, or -1 for a row that has none.
     *
     * This was the literal 100 until 2026-08-03, which is right for a page of percentages and
     * meaningless on any other page: the camera's rows are centimetres, and its reference is the
     * seat position settled at the wheel - 150 up, 3 forward, -25 across - a different number on every
     * row. A heavy detent drawn at 100 there would mark nothing and read as advice. */
    int  (*ref)(int row);
    /* A SECOND, ordinary mark, or -1. It exists because a recommended value can move away from
       a number that still means something on that row: gunfire is recommended at 0 since
       2026-08-12, and 100 is where it used to be - one step away and worth showing, but no
       longer the advice. Drawn at normal weight, so the heavy tick stays unambiguous. */
    int  (*alt)(int row);
    /* The control id of row 0 on this page, so a window can work out which row it is without the
       caller storing the row anywhere. */
    int  idBase;
} slider_ops;

/* ONE BLOCK PER SLIDER, not one per program. It used to be a single global set at start-up, on the
 * reasoning that only one page is ever being interacted with - true of interaction, false of
 * PAINTING, and the moment a second page grew sliders that global decided how the first page's
 * sliders drew. The ops travel with the window; the row comes from the control id, which is
 * already unique per row, so nothing has to be kept in step by hand. */
/* ---- A DRAG IS ONE CHANGE, NOT FORTY -------------------------------------------------------
 * Design note, 2026-08-03, with a screenshot of the log: slider changes were to be applied and recorded when the drag
 * is released, so they would not end up being overwritten too often.
 * One drag of the strength slider had written the file five times and logged five lines -
 * `43% -> 60%`, `60% -> 74%`, `74% -> 72%`, `72% -> 27%`, `27% -> 65%` - which is a record of the
 * finger's journey, not of a decision.
 *
 * So while a knob is held, the page is marked dirty and NOTHING is written; the write happens once,
 * on release. The 300 ms timer still serves everything else - typing in a box, pressing a button -
 * because those have no held state to wait for.
 *
 * The flag lives here rather than in the page, because the page cannot see a mouse capture. The
 * consumer defines SliderDragEnded() and flushes there. */
static int g_sliderDrag = 0;
static void SliderDragEnded(void);          /* page_common.h - it owns the write */

static const slider_ops *SliderOpsOf(HWND h){
    return (const slider_ops *)(INT_PTR)GetWindowLongPtrA(h,GWLP_USERDATA);
}
static int SliderRowOf(HWND h){
    const slider_ops *o=SliderOpsOf(h);
    return o ? GetDlgCtrlID(h)-o->idBase : 0;
}

/* magnetise: the recommended value and the round numbers, inside a few percent of the travel.
   The TEXT BOX is deliberately not magnetised - that is what a box is for.
   The row's own step is applied AFTER the snaps rather than before, so a row that steps by 5
   still lands exactly on 100 from anywhere inside the tolerance. */
static int SliderMagnetise(const slider_ops *o,int row,int v){
    int span=o->travel(row), tol=span/33; if(tol<2) tol=2;
    int ref=o->ref?o->ref(row):100;
    /* The reference first, then the round numbers. A row with no reference (ref -1) simply has one
       fewer snap - it must not silently fall back to 100, which on a page of centimetres is a
       magnet at an arbitrary place. */
    int snaps[5]={ref,0,span/2,span,(o->alt?o->alt(row):-1)};
    for(int i=0;i<5;i++){
        int s=snaps[i];
        if(s<0||s>span) continue;
        if(v>=s-tol&&v<=s+tol) return s;
    }
    if(o->step(row)>1){
        int st=o->step(row);
        v=((v+st/2)/st)*st;
        if(v>span) v=span;
        if(v<0) v=0;
    }
    return v;
}

static void SliderPaint(HWND h,int row){
    PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
    RECT rc; GetClientRect(h,&rc);
    const slider_ops *o=SliderOpsOf(h);
    int off=!IsWindowEnabled(h);
    PaperBlit(dc,h,&rc);
    if(!o){ EndPaint(h,&ps); return; }
    int max=o->travel(row), pos=Clamp(o->get(row),0,max);
    int ref=o->ref?o->ref(row):100;
    int x0=rc.left+SLIDER_PAD, x1=rc.right-SLIDER_PAD, cy=(rc.top+rc.bottom)/2;
    if(max<=0){ EndPaint(h,&ps); return; }

    /* the track: a thin ruled line, the way the game rules its fields */
    HPEN pen=CreatePen(PS_SOLID,1,off?RGB(180,174,164):INK_SOFT); HGDIOBJ op=SelectObject(dc,pen);
    MoveToEx(dc,x0,cy,NULL); LineTo(dc,x1,cy);
    SelectObject(dc,op); DeleteObject(pen);

    /* Detents at 0 / 50 / 100 / full travel. The 100 mark is TALLER AND HEAVIER than the rest and
       carries no colour: green means "in force right now", so a permanently green mark on a
       slider dragged elsewhere would be a lie. The KNOB goes green instead, and only while it is
       actually sitting on 100. */
    int alt=o->alt?o->alt(row):-1;
    for(int i=0;i<5;i++){
        int v = (i==0?0 : i==1?max/2 : i==2?ref : i==3?max : alt);
        if(v<0||v>max) continue;
        int x=x0+(x1-x0)*v/max;
        int isRef=(v==ref);
        /* the 100 tick runs BELOW the knob's own bottom edge on purpose - drawn any shorter it is
           completely hidden by the knob at exactly the value it marks, which is the one moment it
           has to be visible */
        HPEN p2=CreatePen(PS_SOLID,isRef?2:1,off?RGB(190,184,174):(isRef?INK:INK_SOFT));
        HGDIOBJ o2=SelectObject(dc,p2);
        MoveToEx(dc,x,cy+(isRef?2:3),NULL); LineTo(dc,x,cy+(isRef?17:7));
        SelectObject(dc,o2); DeleteObject(p2);
    }

    int atRef=(pos==ref)&&(ref>=0)&&!off;
    int kx=x0+(x1-x0)*pos/max;
    HBRUSH b=CreateSolidBrush(atRef?RGB(228,238,226):FIELD); HGDIOBJ ob=SelectObject(dc,b);
    COLORREF kc = off ? RGB(180,174,164)
                : GetFocus()==h ? MAFIA_RED : (atRef?MAFIA_GREEN:INK);
    HPEN p3=CreatePen(PS_SOLID,(GetFocus()==h||atRef)?2:1,kc);
    HGDIOBJ o3=SelectObject(dc,p3);
    Rectangle(dc,kx-5,cy-9,kx+6,cy+10);
    SelectObject(dc,o3); DeleteObject(p3); SelectObject(dc,ob); DeleteObject(b);
    EndPaint(h,&ps);
}

static void SliderFromX(HWND h,int row,int x){
    RECT rc; GetClientRect(h,&rc);
    const slider_ops *o=SliderOpsOf(h);
    if(!o) return;
    int x0=rc.left+SLIDER_PAD, x1=rc.right-SLIDER_PAD, max=o->travel(row);
    if(x1<=x0||max<=0) return;
    int v=((x-x0)*max*2+(x1-x0))/(2*(x1-x0));      /* rounded, so the ends are reachable */
    v=Clamp(v,0,max);
    v=SliderMagnetise(o,row,v);
    o->set(row,v);
}

static LRESULT CALLBACK SliderProc(HWND h,UINT m,WPARAM w,LPARAM l){
    int row=SliderRowOf(h);
    const slider_ops *o=SliderOpsOf(h);
    if(!o) return DefWindowProcA(h,m,w,l);
    switch(m){
    case WM_PAINT: SliderPaint(h,row); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
        if(!IsWindowEnabled(h)) return 0;
        g_sliderDrag=1;
        SetFocus(h); SetCapture(h); SliderFromX(h,row,(short)LOWORD(l)); return 0;
    case WM_MOUSEMOVE: if(GetCapture()==h) SliderFromX(h,row,(short)LOWORD(l)); return 0;
    case WM_LBUTTONUP:
        if(GetCapture()==h) ReleaseCapture();
        /* the one write of the whole gesture */
        if(g_sliderDrag){ g_sliderDrag=0; SliderDragEnded(); }
        return 0;
    /* Capture can also be lost without a button-up - a modal dialog, Alt-Tab, another window
       stealing it. Without this the flag would stay set and the page would never write again. */
    case WM_CAPTURECHANGED:
        if(g_sliderDrag){ g_sliderDrag=0; SliderDragEnded(); }
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(h,NULL,TRUE); return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_KEYDOWN: {
        int max=o->travel(row), pos=Clamp(o->get(row),0,max);
        int st=o->step(row);
        if(!IsWindowEnabled(h)) return 0;
        if(w==VK_LEFT||w==VK_DOWN)  o->set(row,Clamp(pos-st,0,max));
        else if(w==VK_RIGHT||w==VK_UP) o->set(row,Clamp(pos+st,0,max));
        else if(w==VK_HOME) o->set(row,0);
        else if(w==VK_END)  o->set(row,max);
        return 0; }
    }
    return DefWindowProcA(h,m,w,l);
}

/* The row is NOT passed in: it is `id - ops->idBase`, so a page cannot create a slider whose id
   and whose row disagree. That pairing was two arguments until 2026-08-03 and nothing checked it. */
static HWND MkSliderOps(HWND p,int x,int y,int w,int h,int id,const slider_ops *ops){
    HWND s=CreateWindowExA(0,"MafiaSlider","",WS_CHILD|WS_VISIBLE|WS_TABSTOP,x,y,w,h,p,
                           (HMENU)(INT_PTR)id,NULL,NULL);
    SetWindowLongPtrA(s,GWLP_USERDATA,(LONG_PTR)(INT_PTR)ops);
    return s;
}

#endif /* ALXG_UI_SLIDER_H */
