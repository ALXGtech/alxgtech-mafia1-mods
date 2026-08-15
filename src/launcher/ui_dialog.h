/* ui_dialog.h - the modal box this program asks its two questions with.
 *
 * Not MessageBox: the enable warning needs a do-not-show-again checkbox, and a system
 * box in the middle of a Mafia-skinned window reads as another program's dialog. Not
 * DialogBoxIndirect either - a template in memory would still give system-drawn controls.
 *
 * It is an ordinary window made modal the honest way: disable the owner, run a message loop
 * until the box closes, enable the owner again. Both dialogs default to the SAFE answer - Escape
 * and the close box are Cancel, and the default button is Cancel - because a modal box whose
 * default installs something is a box people dismiss without reading.
 *
 * Requires: the skin (PaperBlit, DrawPressable, fonts, brushes) and g_frame.
 */
#ifndef ALXG_UI_DIALOG_H
#define ALXG_UI_DIALOG_H

#define DLG_W        560
#define DLG_PAD      22
#define DLG_BTN_W    150
#define DLG_BTN_H    30
#define DLG_ID_OK    1
#define DLG_ID_CANCEL 2
#define DLG_ID_CHECK 3

static const char *g_dlgBody;
static const char *g_dlgOk;
static const char *g_dlgCancel;
static const char *g_dlgCheck;      /* NULL = no checkbox */
static int   g_dlgChecked;
static int   g_dlgResult;
static int   g_dlgDone;
static HWND  g_dlgWnd;

static LRESULT CALLBACK DlgProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_ERASEBKGND: {
        HDC dc=(HDC)w; RECT rc; GetClientRect(h,&rc);
        FillRect(dc,&rc,g_brPaper);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,INK);
        HGDIOBJ of=SelectObject(dc,g_fBody);
        RECT t={DLG_PAD,DLG_PAD,rc.right-DLG_PAD,rc.bottom-DLG_PAD};
        DrawTextA(dc,g_dlgBody,-1,&t,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,of);
        return 1; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l;
        char txt[400]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
        RECT r=d->rcItem;
        int id=(int)w;
        int pressed=(d->itemState&ODS_SELECTED)!=0;
        SetBkMode(d->hDC,TRANSPARENT);
        FillRect(d->hDC,&r,g_brPaper);
        if(id==DLG_ID_CHECK){
            /* drawn rather than a BS_AUTOCHECKBOX, so the tick is the same ink as everything
               else on the sheet instead of a Windows blue box */
            RECT bx={r.left,(r.top+r.bottom)/2-8,r.left+16,(r.top+r.bottom)/2+8};
            HPEN pen=CreatePen(PS_SOLID,1,INK); HGDIOBJ op=SelectObject(d->hDC,pen);
            HGDIOBJ ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
            Rectangle(d->hDC,bx.left,bx.top,bx.right,bx.bottom);
            if(g_dlgChecked){
                HPEN p2=CreatePen(PS_SOLID,2,INK); HGDIOBJ o2=SelectObject(d->hDC,p2);
                MoveToEx(d->hDC,bx.left+3,(bx.top+bx.bottom)/2,NULL);
                LineTo(d->hDC,bx.left+7,bx.bottom-4);
                LineTo(d->hDC,bx.right-3,bx.top+3);
                SelectObject(d->hDC,o2); DeleteObject(p2);
            }
            SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
            HGDIOBJ of=SelectObject(d->hDC,g_fField);
            SetTextColor(d->hDC,INK);
            RECT t={r.left+24,r.top,r.right,r.bottom};
            DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(d->hDC,of);
            return TRUE;
        }
        HGDIOBJ save=SelectObject(d->hDC,id==DLG_ID_OK?g_fAction:g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,0,id==DLG_ID_CANCEL,0);
        SelectObject(d->hDC,save);
        return TRUE; }
    case WM_COMMAND: {
        int id=LOWORD(w);
        if(id==DLG_ID_CHECK){ g_dlgChecked=!g_dlgChecked; InvalidateRect((HWND)l,NULL,TRUE); return 0; }
        if(id==DLG_ID_OK){ g_dlgResult=IDOK; g_dlgDone=1; return 0; }
        if(id==DLG_ID_CANCEL){ g_dlgResult=IDCANCEL; g_dlgDone=1; return 0; }
        return 0; }
    case WM_CLOSE: g_dlgResult=IDCANCEL; g_dlgDone=1; return 0;
    }
    return DefWindowProcA(h,m,w,l);
}

/* Returns IDOK or IDCANCEL. `check` NULL means no checkbox; otherwise *checked comes back with
   what the box was left at. The CALLER decides what a tick means - AskDisable passes NULL, so
   undoing an install has no skippable path at all. */
static int AlxgBox(HWND parent,const char *title,const char *body,const char *ok,
                   const char *cancel,const char *check,int *checked)
{
    MSG msg; RECT pr,want; HDC dc; RECT meas; int bodyH,h,x,y;

    g_dlgBody=body; g_dlgOk=ok; g_dlgCancel=cancel; g_dlgCheck=check;
    g_dlgChecked=0; g_dlgResult=IDCANCEL; g_dlgDone=0;

    /* measure the body first: a box sized to a guess either clips the text or leaves a hole */
    dc=GetDC(parent);
    { HGDIOBJ of=SelectObject(dc,g_fBody);
      meas.left=0; meas.top=0; meas.right=DLG_W-2*DLG_PAD; meas.bottom=0;
      DrawTextA(dc,body,-1,&meas,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
      SelectObject(dc,of); }
    ReleaseDC(parent,dc);
    bodyH=meas.bottom;
    h=DLG_PAD+bodyH+18+(check?30:0)+DLG_BTN_H+DLG_PAD;

    want.left=0; want.top=0; want.right=DLG_W; want.bottom=h;
    AdjustWindowRect(&want,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE);
    GetWindowRect(parent,&pr);
    x=pr.left+((pr.right-pr.left)-(want.right-want.left))/2;
    y=pr.top+((pr.bottom-pr.top)-(want.bottom-want.top))/3;

    g_dlgWnd=CreateWindowExA(WS_EX_DLGMODALFRAME,"MafiaDialog",title,
        WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,want.right-want.left,want.bottom-want.top,
        parent,NULL,NULL,NULL);
    if(!g_dlgWnd) return IDCANCEL;

    { int by=DLG_PAD+bodyH+18;
      if(check){
          MkBtn(g_dlgWnd,check,DLG_PAD,by,DLG_W-2*DLG_PAD,24,DLG_ID_CHECK);
          by+=30;
      }
      /* Cancel on the right, in the corner, and it is the DEFAULT - the safe answer is the one
         the finger lands on. */
      MkBtn(g_dlgWnd,ok,DLG_W-DLG_PAD-2*DLG_BTN_W-12,by,DLG_BTN_W,DLG_BTN_H,DLG_ID_OK);
      MkBtn(g_dlgWnd,cancel,DLG_W-DLG_PAD-DLG_BTN_W,by,DLG_BTN_W,DLG_BTN_H,DLG_ID_CANCEL);
    }

    EnableWindow(parent,FALSE);
    ShowWindow(g_dlgWnd,SW_SHOW);
    UpdateWindow(g_dlgWnd);
    while(!g_dlgDone&&GetMessageA(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN){
            if(msg.wParam==VK_ESCAPE){ g_dlgResult=IDCANCEL; break; }
            if(msg.wParam==VK_RETURN){ g_dlgResult=IDCANCEL; break; }   /* the safe default */
        }
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
    /* the owner is enabled BEFORE the box is destroyed, or Windows hands the foreground to
       another application on the way out */
    EnableWindow(parent,TRUE);
    DestroyWindow(g_dlgWnd);
    g_dlgWnd=NULL;
    SetActiveWindow(parent);

    if(checked) *checked=g_dlgChecked;
    return g_dlgResult;
}

#endif /* ALXG_UI_DIALOG_H */
