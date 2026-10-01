/*
 * ALXGtech Mafia 1 Mods.exe - one window, four mods, and a toggle that installs.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * The engine is src/patcher/install_core.h, the same code mafia-gog-patch.exe runs from the
 * command line. This program never writes a game file itself: it calls do_install/do_uninstall,
 * so there is exactly one implementation of what does installing mean, journal and all.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Spec: written 2026-08-01, covering the launcher's design.
 * Plan: written 2026-08-01, covering the launcher window.
 * Build: tools\build-launcher.ps1
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char g_gameDir[MAX_PATH];   /* the game root, WITH a trailing backslash */

/* FIRST, and it is not alphabetical: this program stands in the game folder, beside the ASI
   loader we install. Every DLL it loads by hand must come from System32 by absolute path, or
   Windows hands it our own loader and the mods end up running inside this window. See the
   header - it cost a toggle that could not remove its own file. */
#include "sys_dll.h"
#include "mod_state.h"             /* pulls in install_core.h */
#include "ui_skin.h"
#include "ui_slider.h"

#define WINW  1320        /* two columns: the page was 971 px tall in one */
#define WINH  720
#define LOGH  92                   /* the log: four lines and a scrollbar, not a panel */
#define MAXCLIENTH 960             /* the tallest this window opens, however long the page is */
#define HEADH 40                   /* switch + state + game folder, ONE line, shared */
#define PAGE_H 1400                /* the canvas is taller than the view on purpose */
#define ID_GAMEDIR 3400
#define ID_CHOOSE  3401
#define ID_ABOUT   3402
#define WATCH_TIMER 9
#define FLUSH_TIMER 10

static HWND   g_frame, g_logBox;
static HWND   g_dirEdit, g_chooseBtn, g_dirLabel;  /* the shared game-folder row, below */
static void   ChooseGame(void);
static void   RefreshGameDir(void);
static HWND   g_view[LNTAB], g_page[LNTAB];
static int    g_contentH[LNTAB], g_scrollY[LNTAB];
static target g_target;

#include "ui_tabs.h"
#include "ui_dialog.h"
#include "ui_about.h"              /* after ui_dialog.h: it reuses DLG_BTN_W / DLG_BTN_H */

#define ID_TAB0   3130
#define ID_LOG    3200

/* ------------------------------------------------------------------ where things are */

static void ExeDir(void){
    char self[MAX_PATH]; GetModuleFileNameA(NULL,self,MAX_PATH);
    int n=SLen(self); while(n>0&&self[n-1]!='\\') n--; self[n]=0;
    SCpy(g_gameDir,self);
}

/* the folder WITHOUT the trailing backslash, which is what install_core.h wants */
static void GameRoot(char *out){
    SCpy(out,g_gameDir);
    int n=SLen(out); if(n>1&&out[n-1]=='\\') out[n-1]=0;
}

/* ---- the log ---------------------------------------------------------------------------------
 * Two destinations for every line, and the file matters more than the box.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * to say what happened - which key they bound, which slider they moved and from what to what,
 * which preset was in force, what the mod reported. Without it, a bug report is "it did not
 * work" and there is nothing to check it against.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * The file is capped. It rolls at 8 MB: the current file becomes .1 and a fresh one starts, so
 * there is always the recent past and never an unbounded file in somebody's game folder. The box
 * on screen is capped too, at 400 lines - an EDIT that grows all session slows the window down
 * and nobody scrolls back that far anyway.
 */
#define LOG_MAX_BYTES (8*1024*1024)
#define LOG_MAX_LINES 400

static FILE *g_logFile;
static int   g_logLines;

/* ---- THE LOG'S OWN RENAME, 2026-08-12 --------------------------------------------------------
 * The product was "ALXG Mafia Mods" until today, and its log carried that name. On an upgrade the
 * old file just sits there: nothing ever writes to it again and NOTHING EVER REMOVES IT - the
 * uninstall does not touch logs at all (checked in do_uninstall, it only undoes journalled work),
 * so a folder would end up with two of ours, one of them dead.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * So the old one is taken over rather than abandoned: moved to the new name when there is no new
 * file yet, which keeps the history, and deleted when there is, which is the only other honest
 * option. Returns nothing and is safe to call when neither file exists.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * It is a separate function so the self-test can plant the old name and call it - a migration
 * nobody exercised is a migration that runs for the first time on a stranger's machine.
 */
static void LogMigrateOldName(const char *dir){
    static const char *OLD[2]={ "ALXG Mafia Mods.log","ALXG Mafia Mods.log.1" };
    static const char *NEW[2]={ BRAND_NAME ".log", BRAND_NAME ".log.1" };
    char from[MAX_PATH],to[MAX_PATH]; int i;
    for(i=0;i<2;i++){
        SCpy(from,dir); SCat(from,OLD[i]);
        if(GetFileAttributesA(from)==INVALID_FILE_ATTRIBUTES) continue;
        SCpy(to,dir); SCat(to,NEW[i]);
        if(GetFileAttributesA(to)==INVALID_FILE_ATTRIBUTES) MoveFileA(from,to);
        else                                                DeleteFileA(from);
    }
}

static void LogOpen(void){
    char p[MAX_PATH],old[MAX_PATH];
    if(g_logFile) return;
    LogMigrateOldName(g_gameDir);
    SCpy(p,g_gameDir); SCat(p,BRAND_NAME ".log");
    { HANDLE h=CreateFileA(p,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
      if(h!=INVALID_HANDLE_VALUE){
          DWORD sz=GetFileSize(h,NULL);
          CloseHandle(h);
          if(sz>LOG_MAX_BYTES){
              SCpy(old,p); SCat(old,".1");
              DeleteFileA(old);
              MoveFileA(p,old);      /* one generation back, then start clean */
          }
      } }
    g_logFile=fopen(p,"a");
    if(g_logFile){
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_logFile,"\n=== " BRAND_NAME " started %04d-%02d-%02d %02d:%02d:%02d ===\n",
                t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        fflush(g_logFile);
    }
}

static void LogLine(const char *s){
    if(g_logFile){
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_logFile,"%02d:%02d:%02d  %s\n",t.wHour,t.wMinute,t.wSecond,s);
        fflush(g_logFile);      /* flushed per line: a crash is exactly when the tail matters */
    }
    if(!g_logBox) return;
    /* Trim from the TOP when the box is full. Deleting the oldest lines keeps the newest visible
       without the control ever holding a session's worth of text. */
    if(++g_logLines>LOG_MAX_LINES){
        int cut=(int)SendMessageA(g_logBox,EM_LINEINDEX,LOG_MAX_LINES/4,0);
        if(cut>0){
            SendMessageA(g_logBox,EM_SETSEL,0,cut);
            SendMessageA(g_logBox,EM_REPLACESEL,FALSE,(LPARAM)"");
            g_logLines-=LOG_MAX_LINES/4;
        }
    }
    int n=GetWindowTextLengthA(g_logBox);
    SendMessageA(g_logBox,EM_SETSEL,n,n);
    SendMessageA(g_logBox,EM_REPLACESEL,FALSE,(LPARAM)s);
    SendMessageA(g_logBox,EM_REPLACESEL,FALSE,(LPARAM)"\r\n");
}

/* Is the game up right now? By WINDOW rather than by process list: this program is 32-bit and
   the process snapshot API is the heavier answer to the same question, while Mafia's window
   class is stable and belongs to the running game rather than to a launcher or an installer.
   Used only to word the save row honestly - nothing here ever closes the game. */
static int GameIsRunning(void){
    return FindWindowA("ISOFT_LS3D_WCLASS",NULL)!=NULL || FindWindowA(NULL,"Mafia")!=NULL;
}

/* ---- which game folder ---------------------------------------------------------------------
 * It defaults to the folder this exe sits in, which is where it ships. But as decided 2026-08-01, a
 * user has to be able to point it somewhere else, and the choice belongs to the whole program
 * rather than to one tab - all four mods install into the same folder.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Remembered in HKCU, not in a file: a settings file in the game folder would have to be created
 * before anything is installed, kept out of the journal, and then survive an uninstall that
 * promises the folder back the way it was. */
#define ALXG_REGKEY "Software\\ALXG\\MafiaMods"

static void SaveGameDir(const char *dir){
    HKEY k;
    if(RegCreateKeyExA(HKEY_CURRENT_USER,ALXG_REGKEY,0,NULL,0,KEY_WRITE,NULL,&k,NULL)
       !=ERROR_SUCCESS) return;
    RegSetValueExA(k,"game_dir",0,REG_SZ,(const BYTE*)dir,(DWORD)SLen(dir)+1);
    RegCloseKey(k);
}

/* Returns 1 when the remembered folder still holds a Game.exe. A folder that has moved or been
   deleted since is ignored rather than reported: the default - our own folder - is the better
   answer, and a dialog on startup about a path the user has forgotten choosing is noise. */
static int LoadGameDir(char *out,size_t n){
    HKEY k; DWORD sz=(DWORD)n,type=0; char p[MAX_PATH];
    if(RegOpenKeyExA(HKEY_CURRENT_USER,ALXG_REGKEY,0,KEY_READ,&k)!=ERROR_SUCCESS) return 0;
    if(RegQueryValueExA(k,"game_dir",NULL,&type,(BYTE*)out,&sz)!=ERROR_SUCCESS||type!=REG_SZ){
        RegCloseKey(k); return 0;
    }
    RegCloseKey(k);
    SCpy(p,out);
    if(SLen(p)&&p[SLen(p)-1]!='\\') SCat(p,"\\");
    SCat(p,"Game.exe");
    return GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES;
}

static void Relocate(void){
    char root[MAX_PATH]; GameRoot(root);
    if(!locate(root,&g_target))
        LogLine("no Game.exe here - use \"Choose...\" to point at your Mafia folder");
}

/* ------------------------------------------------------------------ pages and scrolling */

static void PageShow(int i,int on){ if(g_view[i]) ShowWindow(g_view[i],on?SW_SHOW:SW_HIDE); }

static void SyncScroll(int tab,int reposition){
    RECT rc; GetClientRect(g_frame,&rc);
    int viewH=rc.bottom-TITLEBAND-HEADH-LOGH; if(viewH<1) viewH=1;
    int maxY=g_contentH[tab]-viewH; if(maxY<0) maxY=0;
    if(g_scrollY[tab]>maxY) g_scrollY[tab]=maxY;
    if(g_scrollY[tab]<0) g_scrollY[tab]=0;
    if(tab==g_curTab){
        SCROLLINFO si; for(int i=0;i<(int)sizeof(si);i++) ((BYTE*)&si)[i]=0;
        si.cbSize=sizeof(si); si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
        si.nMin=0; si.nMax=g_contentH[tab]-1; si.nPage=(UINT)viewH; si.nPos=g_scrollY[tab];
        SetScrollInfo(g_frame,SB_VERT,&si,TRUE);
        /* Settled 2026-08-01: a scrollbar belongs on a window that has something to scroll. When
           the page fits, the bar goes away rather than sitting there greyed - a disabled control
           on a window that needs none reads as something being wrong with the window. */
        ShowScrollBar(g_frame,SB_VERT,maxY>0);
    }
    /* the canvas is never SHORTER than the view, or the paper stops partway down the window and
       the frame's own background shows through as a bare strip */
    int canvasH=g_contentH[tab]>viewH?g_contentH[tab]:viewH;
    if(reposition&&g_view[tab])
        SetWindowPos(g_view[tab],NULL,0,TITLEBAND+HEADH,rc.right,viewH,SWP_NOZORDER);
    if(reposition&&g_page[tab])
        SetWindowPos(g_page[tab],NULL,0,-g_scrollY[tab],rc.right,canvasH,SWP_NOZORDER);
}

static void ScrollBy(int tab,int dy){
    int was=g_scrollY[tab];
    g_scrollY[tab]+=dy;
    SyncScroll(tab,1);
    if(g_scrollY[tab]!=was && g_page[tab]) UpdateWindow(g_page[tab]);
}

/* A page is frame -> viewport -> canvas, and the viewport is not optional. A canvas taller than
 * the window scrolls by MOVING; as a direct child of the frame that makes it a SIBLING of the
 * tab buttons, and the moment it scrolls their rectangles overlap with nothing clipping between
 * them - so SetWindowPos blits the strip's pixels down into the content and the tab strip
 * appears two or three times over the page. WS_CLIPSIBLINGS is NOT the fix: it clips against
 * sibling windows only, and the brand strip is painted by the frame itself. One extra window is
 * the fix, because a parent clips its children unconditionally.
 * Found on screen, 2026-07-27 (trap 5). */
static HWND MakePage(int i,int canvasH){
    RECT rc; GetClientRect(g_frame,&rc);
    int viewH=rc.bottom-TITLEBAND-HEADH-LOGH; if(viewH<1) viewH=1;
    g_contentH[i]=canvasH;
    g_view[i]=CreateWindowExA(0,"MafiaView","",WS_CHILD|WS_CLIPCHILDREN,
        0,TITLEBAND+HEADH,rc.right,viewH,g_frame,NULL,NULL,NULL);
    g_page[i]=CreateWindowExA(0,"MafiaCanvas","",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,rc.right,canvasH>viewH?canvasH:viewH,g_view[i],NULL,NULL,NULL);
    return g_page[i];
}

/* which page owns this canvas - the draw and command routing needs it */
static int TabOfCanvas(HWND canvas){
    for(int i=0;i<LNTAB;i++) if(g_page[i]==canvas) return i;
    return -1;
}

#include "page_common.h"

/* ------------------------------------------------------------------ greying */
  /* Spec 3.1: if the mod is off, the whole rest of the interface just looks all gray
   * and non-interactive.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Two halves and both are needed: EnableWindow stops the clicks, and the owner-drawn controls
 * have to be repainted, or a dead button still looks alive. The header - lamp, switch and build
 * line - stays live whatever the mod is doing, because it is how the mod comes back. */
typedef struct { int tab,on; } grey_ctx;

static BOOL CALLBACK GreyOne(HWND child,LPARAM lp){
    grey_ctx *c=(grey_ctx*)lp;
    int id=GetDlgCtrlID(child);
    /* only the build line lives on a page now - the switch and the lamp are on the frame,
       so greying a page cannot reach them and the mod can always be switched back on */
    if(id==ID_BUILDLINE) return TRUE;
    EnableWindow(child,c->on);
    InvalidateRect(child,NULL,TRUE);
    return TRUE;
}

static void PageGrey(int tab,int on){
    grey_ctx c; c.tab=tab; c.on=on;
    if(!g_page[tab]) return;
    EnumChildWindows(g_page[tab],GreyOne,(LPARAM)&c);
}

/* how many controls a page has, and how many of them are switched off - the self test's way of
   asking whether greying actually happened */
static int g_cntAll,g_cntOff;
static BOOL CALLBACK CountOne(HWND child,LPARAM lp){
    (void)lp;
    g_cntAll++;
    if(!IsWindowEnabled(child)) g_cntOff++;
    return TRUE;
}
static void CountPage(int tab){
    g_cntAll=g_cntOff=0;
    if(g_page[tab]) EnumChildWindows(g_page[tab],CountOne,0);
}
static int CountControls(int tab){ CountPage(tab); return g_cntAll; }
static int CountDisabled(int tab){ CountPage(tab); return g_cntOff; }

#include "page_shifter.h"
#include "version.h"
#include "../shared/profile_sav.h"
#include "page_ffb.h"
#include "page_fpv.h"
#include "page_vr.h"

/* ------------------------------------------------------------------ window procedures */

static LRESULT CALLBACK CanvasProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_ERASEBKGND: PaintBackdrop(h,(HDC)w); return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)w; HWND child=(HWND)l;
        int id=GetDlgCtrlID(child);
        COLORREF c=IsWindowEnabled(child)?INK:INK_OFF;
        /* A DISABLED edit box is coloured through WM_CTLCOLORSTATIC, not WM_CTLCOLOREDIT -
           trap 3, encountered before, bit again here. Without this the value
           boxes on a greyed page get the paper pattern brush and their digits are drawn over
           grain and burn marks. Discriminate by id, exactly as that memory says. */
        if(id>=FF_ID_BOX0&&id<FF_ID_BOX0+NSLIDER){
            SetBkColor(dc,FIELD); SetTextColor(dc,c);
            return (LRESULT)g_brField;
        }
        SetBkMode(dc,TRANSPARENT);
        if(id>=ID_BANNER0&&id<ID_BANNER0+LNTAB&&IsWindowEnabled(child))
            /* green = every change here is already in the game; ORDINARY INK = the mod takes its
               settings at game start, which is a rule and not a fault; red = the mod works but a
               change you just made is waiting for a restart. Each names what is going on rather
               than leaving it to be inferred, and only the last one is loud. */
            c=g_bannerKind[id-ID_BANNER0]==BAN_GOOD?MAFIA_GREEN
             :g_bannerKind[id-ID_BANNER0]==BAN_INFO?INK
                                                   :MAFIA_RED;
        else if(id==ID_BUILDLINE)
            /* green when the build is the one everything was tuned on; ORDINARY INK when it is
               not. Never red: red is a fault, and an unknown build is a caveat (spec 3.2). */
            c=g_target.build?MAFIA_GREEN:INK;
        else if(id==FF_ID_MODETXT&&IsWindowEnabled(child)&&ffb_ModelWarning())
            /* red, because it IS a fault: the road model is not what drives the wheel - an
               earlier module in the folder, or a file that switches it off. See page_ffb.h
               ffb_ModelWarning */
            c=MAFIA_RED;
        SetTextColor(dc,c);
        /* a label needs a BRUSH, not NULL_BRUSH: with WS_CLIPCHILDREN the parent cannot paint
           under a child, so a transparent static shows a blank rectangle where the paper should
           be. The pattern brush is cut from the same generated sheet. */
        return (LRESULT)(g_brSheet?g_brSheet:g_brPaper); }
    case WM_CTLCOLOREDIT: {
        /* An editable EDIT needs the CLEAN field behind it. Without this branch it inherits the
           paper pattern brush and the digits are drawn over grain and burn marks - the value
           boxes came out unreadable, which is the sort of thing only a photograph finds. */
        HDC dc=(HDC)w;
        SetBkColor(dc,FIELD); SetTextColor(dc,INK);
        return (LRESULT)g_brField; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l;
        int id=(int)d->CtlID;
        char txt[320]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
        RECT r=d->rcItem;
        int pressed=(d->itemState&ODS_SELECTED)!=0;
        int off=!IsWindowEnabled(d->hwndItem);
        SetBkMode(d->hDC,TRANSPARENT);
        if(PageShifterDraw(d,id)) return TRUE;
        if(PageFfbDraw(d,id)) return TRUE;
        if(PageFpvDraw(d,id)) return TRUE;
        return DefWindowProcA(h,m,w,l); }
    case WM_TIMER:
        if(w==GB_TIMER&&h==g_page[LTAB_SHIFTER]) PageShifterTimer();
        else if(w==FFB_TIMER&&h==g_page[LTAB_FFB]) PageFfbPoll();
        else if(w==FP_TIMER&&h==g_page[LTAB_FPV]) PageFpvTimer();
        return 0;
    case WM_COMMAND:
        if(PageShifterCommand(LOWORD(w))) return 0;
        if(PageFfbCommand(LOWORD(w),HIWORD(w),(HWND)l)) return 0;
        if(PageFpvCommand(LOWORD(w),HIWORD(w),(HWND)l)) return 0;
        return SendMessageA(g_frame,m,w,l);
    }
    return DefWindowProcA(h,m,w,l);
}

static LRESULT CALLBACK FrameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_ERASEBKGND: {
        HDC dc=(HDC)w; RECT rc; GetClientRect(h,&rc);
        RECT band={0,0,rc.right,TITLEBAND};
        FillRect(dc,&band,g_brRoom);
        SetBkMode(dc,TRANSPARENT);

        /* THE BRAND STRIP: the name and the version, and NO ICON.
           The icon was here for one build and Alex cut it - the title bar directly above already
           shows it, so a second copy fourteen pixels lower is the same picture twice - in the
           bottom strip on black the icon is not needed a second time and looks wrong there.
           The version lives here though. It used to sit bottom right on the paper, where it landed
           in the middle of a page of sliders and read as ungrammatical rendering. */
        { HGDIOBJ of;
          int tx=12;

          of=SelectObject(dc,g_fBrand);
          SetTextColor(dc,RGB(238,234,226));
          /* NOT UPPER CASED. The nickname is "ALXGtech" - Alex, 2026-08-12: one word, with the
             case doing the work of showing where the name ends and the trade begins. Setting it in
             capitals throws away the only thing that distinction has to live in. */
          { RECT b={tx,0,rc.right,BRANDSTRIP};
            DrawTextA(dc,BRAND_NAME,-1,&b,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            /* the version sits directly after the name, measured rather than placed at a guess */
            RECT m={0,0,0,0};
            DrawTextA(dc,BRAND_NAME,-1,&m,DT_CALCRECT|DT_SINGLELINE);
            SelectObject(dc,g_fSmallCaps);
            SetTextColor(dc,RGB(150,140,124));
            RECT v={tx+(m.right-m.left)+14,0,rc.right,BRANDSTRIP};
            DrawTextA(dc,"v" ALXG_VERSION,-1,&v,DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
          SelectObject(dc,of); }

        /* NO SKYLINE HERE. It was the third of the three stitches and it is the one that failed:
           a row of red teeth 1320 px wide reads as a bar chart, not as a city - the shape only
           works inside an icon, where it is small and framed. Decided 2026-08-12: the red
           background looks wrong in the top strip and was removed for this strip specifically,
           leaving a plain black fill. The strip is plain dark; the icon and the wordmark carry
           the brand on their own. */
        RECT below={0,TITLEBAND,rc.right,rc.bottom};
        FillRect(dc,&below,g_brPaper);
        /* a hairline under the header strip, so the switch row reads as belonging to the window
           rather than to whichever page happens to be open under it */
        { HPEN pen=CreatePen(PS_SOLID,1,RGB(176,168,154)); HGDIOBJ op=SelectObject(dc,pen);
          MoveToEx(dc,0,TITLEBAND+HEADH-1,NULL); LineTo(dc,rc.right,TITLEBAND+HEADH-1);
          SelectObject(dc,op); DeleteObject(pen); }
        return 1; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)l;
        int id=(int)w;
        if(id>=ID_TAB0&&id<ID_TAB0+LNTAB){ TabDraw(d,id-ID_TAB0); return TRUE; }
        if(id==ID_ABOUT){
            /* It sits in the BRAND STRIP, which is the dark room colour - so it is painted like
               a line of that strip, not like a button on paper. DrawPressable here would stamp a
               pale plate into the dark band. */
            char txt[32]; RECT r=d->rcItem;
            GetWindowTextA(d->hwndItem,txt,sizeof(txt));
            FillRect(d->hDC,&r,g_brRoom);
            SetBkMode(d->hDC,TRANSPARENT);
            { HGDIOBJ of=SelectObject(d->hDC,g_fSmallCaps);
              SetTextColor(d->hDC,(d->itemState&ODS_SELECTED)?RGB(220,212,198):RGB(150,140,124));
              DrawTextA(d->hDC,txt,-1,&r,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
              SelectObject(d->hDC,of); }
            return TRUE;
        }
        if(id==ID_TOGGLE||id==ID_CHOOSE){
            /* On the frame's footer, not on a page, so there is no generated paper behind it -
               a PaperBlit here would stamp a patch of sheet onto the plain background. */
            char txt[80]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
            RECT r=d->rcItem;
            SetBkMode(d->hDC,TRANSPARENT);
            FillRect(d->hDC,&r,g_brPaper);
            HGDIOBJ save=SelectObject(d->hDC,id==ID_TOGGLE?g_fAction:g_fField);
            DrawPressable(d->hDC,&r,txt,(d->itemState&ODS_SELECTED)!=0,
                          !IsWindowEnabled(d->hwndItem),id==ID_TOGGLE,0);
            SelectObject(d->hDC,save);
            return TRUE;
        }
        return DefWindowProcA(h,m,w,l); }
    case WM_CTLCOLORSTATIC: {
        /* The footer's own controls. A read-only EDIT is coloured through this message too, and
           the path needs the clean field behind it rather than the page's paper. */
        HDC dc=(HDC)w; int cid=GetDlgCtrlID((HWND)l);
        if(cid==ID_GAMEDIR||cid==ID_LOG){
            SetBkColor(dc,FIELD); SetTextColor(dc,INK);
            return (LRESULT)g_brField;
        }
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc, cid==ID_LAMP ? ToggleColour(g_state[g_curTab]) : INK);
        return (LRESULT)g_brPaper; }
    case WM_COMMAND: {
        int id=LOWORD(w);
        if(id>=ID_TAB0&&id<ID_TAB0+LNTAB){
            int tab=id-ID_TAB0;
            TabSelect(tab);
            RefreshPage(tab);      /* the one switch and lamp now show THIS tab's mod */
            SyncScroll(tab,1);
            return 0;
        }
        if(id==ID_TOGGLE){
            int tab=g_curTab;      /* the one switch acts on the tab you are looking at */
            /* Neither direction is one click (spec 3.1). */
            if(g_state[tab]==JMOD_ON){ if(AskDisable(tab)) ToggleOff(tab); }
            else                     { if(AskEnable(tab))  ToggleOn(tab);  }
            return 0;
        }
        if(id==ID_TOGGLE||id==ID_CHOOSE){ ChooseGame(); return 0; }
        if(id==ID_ABOUT){ AboutShow(h); return 0; }
        return DefWindowProcA(h,m,w,l); }
    case WM_MOUSEWHEEL:
        ScrollBy(g_curTab, -GET_WHEEL_DELTA_WPARAM(w)/2);
        return 0;
    /* ---- the window is resizable in HEIGHT ONLY -------------------------------------------
     * Settled 2026-08-01: it should open with no scrollbar whenever the page fits on the screen,
     * and scroll only when the user has shortened it or the screen is too small to begin with.
     * Width stays pinned: every page here is laid out against fixed pixel columns, and a
     * binding list that stretches is a list whose columns stop lining up. */
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm=(MINMAXINFO*)l;
        RECT f={0,0,WINW,600};
        AdjustWindowRect(&f,(DWORD)GetWindowLongA(h,GWL_STYLE),FALSE);
        int fw=(f.right-f.left);
        mm->ptMinTrackSize.x=fw; mm->ptMaxTrackSize.x=fw;
        mm->ptMinTrackSize.y=TITLEBAND+LOGH+140;
        return 0; }
    case WM_SIZE: {
        RECT rc; GetClientRect(h,&rc);
        /* the header strip is pinned to the top, so only its WIDTH follows a resize - and the
           width is pinned too, so this is only the path box stretching if that ever changes */
        if(g_logBox)  SetWindowPos(g_logBox,NULL,0,rc.bottom-LOGH,rc.right,LOGH,SWP_NOZORDER);
        if(g_dirEdit) SetWindowPos(g_dirEdit,NULL,26+HDR_BTN_W+14+HDR_LAMP_W+92,TITLEBAND+HDR_Y-1,
                                   rc.right-(26+HDR_BTN_W+14+HDR_LAMP_W+92)-26-116,24,SWP_NOZORDER);
        if(g_chooseBtn) SetWindowPos(g_chooseBtn,NULL,rc.right-26-110,TITLEBAND+HDR_Y-1,110,24,
                                     SWP_NOZORDER);
        for(int i=0;i<LNTAB;i++){
            SyncScroll(i,1);
            /* the canvas has to repaint WHOLE after a resize, not just the strip Windows
               considers newly exposed - the paper is one blit and a partial one tiles */
            if(g_page[i]) InvalidateRect(g_page[i],NULL,TRUE);
        }
        InvalidateRect(h,NULL,TRUE);
        return 0; }
    /* One slow timer watching for the game coming up. The instant a new Mafia starts it has read
       the current files, so every tab's changed-since flag is cleared and the
       red banner goes away by itself - which is the behaviour that stops it reading as "the mod
       is broken". */
    case WM_TIMER:
        if(w==FLUSH_TIMER){ PageFlush(); return 0; }
        if(w==WATCH_TIMER){
            static int wasRunning=-1;
            int now=GameIsRunning();
            if(now!=wasRunning){
                if(now&&wasRunning==0) GameStarted();
                else for(int i=0;i<LNTAB;i++) RefreshSaved(i);
                wasRunning=now;
            }
            return 0;
        }
        return DefWindowProcA(h,m,w,l);
    case WM_VSCROLL: {
        int code=LOWORD(w);
        if(code==SB_LINEUP)        ScrollBy(g_curTab,-40);
        else if(code==SB_LINEDOWN) ScrollBy(g_curTab, 40);
        else if(code==SB_PAGEUP)   ScrollBy(g_curTab,-240);
        else if(code==SB_PAGEDOWN) ScrollBy(g_curTab, 240);
        else if(code==SB_THUMBTRACK||code==SB_THUMBPOSITION){
            SCROLLINFO si; for(int i=0;i<(int)sizeof(si);i++) ((BYTE*)&si)[i]=0;
            si.cbSize=sizeof(si); si.fMask=SIF_TRACKPOS;
            GetScrollInfo(h,SB_VERT,&si);
            ScrollBy(g_curTab,si.nTrackPos-g_scrollY[g_curTab]);
        }
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h,m,w,l);
}

/* ------------------------------------------------------------------ building the window */

/* `show` is SW_SHOW for a person and SW_HIDE for --selftest. ONE builder, so a check can never
   pass against a window that is not the shipped one. */
/* How tall to open. Settled 2026-08-01: sizing to the content made a window that ran most of the
 * One row at the foot of the WINDOW, not on any page: all four mods install into the same
 * folder, so four copies of this control would be four ways to disagree about where the game is.
 * The user picks Game.exe rather than a folder - it is the file they can see and recognise. */
static void RefreshGameDir(void){
    char root[MAX_PATH];
    if(!g_dirEdit) return;
    GameRoot(root);
    SetWindowTextA(g_dirEdit,root);
}

static void ChooseGame(void){
    OPENFILENAMEA o; char file[MAX_PATH]="Game.exe"; char dir[MAX_PATH]; int n,i;
    for(i=0;i<(int)sizeof(o);i++) ((BYTE*)&o)[i]=0;
    GameRoot(dir);
    o.lStructSize=sizeof(o);
    o.hwndOwner=g_frame;
    o.lpstrFilter="Mafia's Game.exe\0Game.exe\0All files\0*.*\0";
    o.lpstrFile=file; o.nMaxFile=sizeof(file);
    o.lpstrInitialDir=dir;
    o.lpstrTitle="Find Game.exe in your Mafia folder";
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
    if(!GetOpenFileNameA(&o)) return;
    n=SLen(file); while(n>0&&file[n-1]!='\\') n--; file[n]=0;
    SCpy(g_gameDir,file);
    SaveGameDir(file);
    Relocate();
    RefreshGameDir();
    /* Everything on every tab was about the OLD folder: the toggles, the saved-state lines and
       the bindings all have to be re-read from the new one. */
    { char p[MAX_PATH]; gb_IniPath(p); gb_LoadFrom(p); gb_RefreshAll(); gb_RefreshMode(); }
    for(i=0;i<LNTAB;i++){ g_touched[i]=0; RefreshPage(i); RefreshSaved(i); }
    { char m[MAX_PATH+64]; wsprintfA(m,"game folder is now %s",g_gameDir); LogLine(m); }
}

#define FRAMESTYLE (WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN)

/* How tall to open. Settled 2026-08-01: sizing to the content made a window that ran most of the
   way down a 1440p screen, and even on a big screen that would not fit - so there is a ceiling,
   and past it the page scrolls. A short page still opens short: a window with empty paper under
   its last control looks broken in the other direction.
   Called after the pages exist, because until then nobody knows how tall the content is. */
static void SizeToContent(void){
    RECT wa,want; int tallest=0,wantH,availH,i;
    for(i=0;i<LNTAB;i++) if(g_contentH[i]>tallest) tallest=g_contentH[i];
    SystemParametersInfoA(SPI_GETWORKAREA,0,&wa,0);
    availH=(wa.bottom-wa.top)-48; if(availH<320) availH=320;
    if(availH>MAXCLIENTH) availH=MAXCLIENTH;
    wantH=TITLEBAND+HEADH+tallest+LOGH;
    if(wantH>availH) wantH=availH;
    want.left=0; want.top=0; want.right=WINW; want.bottom=wantH;
    AdjustWindowRect(&want,FRAMESTYLE,FALSE);
    SetWindowPos(g_frame,NULL,0,0,want.right-want.left,want.bottom-want.top,
                 SWP_NOZORDER|SWP_NOMOVE);
}

static void BuildUi(int show){
    RECT want={0,0,WINW,WINH};
    AdjustWindowRect(&want,FRAMESTYLE,FALSE);
    /* The TITLE follows the brand; the exe's FILE NAME and the log's do not, and that is
       deliberate. `ALXG Mafia Mods.exe` is written into the binary's own identity checks, the
       install journal and the release archive - renaming the file is a migration, not a caption
       change, and it belongs in the same pass as the repository rename. */
    g_frame=CreateWindowExA(0,"MafiaLauncher",BRAND_NAME,FRAMESTYLE,
        CW_USEDEFAULT,CW_USEDEFAULT,want.right-want.left,want.bottom-want.top,
        NULL,NULL,NULL,NULL);

    /* The 16 px icon for the title bar and the taskbar's small view. The class carries the 32,
       which alt-tab and the large taskbar icons use. */
    { HICON small16=(HICON)LoadImageA(GetModuleHandleA(NULL),MAKEINTRESOURCEA(IDI_APP),
                                      IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
      HICON big32  =(HICON)LoadImageA(GetModuleHandleA(NULL),MAKEINTRESOURCEA(IDI_APP),
                                      IMAGE_ICON,32,32,LR_DEFAULTCOLOR);
      if(small16) SendMessageA(g_frame,WM_SETICON,ICON_SMALL,(LPARAM)small16);
      if(big32)   SendMessageA(g_frame,WM_SETICON,ICON_BIG,(LPARAM)big32); }

    /* ABOUT, in the brand strip's right corner - opposite the wordmark the strip already draws
       on the left. Not a fifth tab: the four tabs are the four mods (spec section 3), and a tab
       that installs nothing sitting beside them is the same confusion the VR placeholder already
       has to explain once. */
    MkBtn(g_frame,"ABOUT",WINW-12-90,0,90,BRANDSTRIP,ID_ABOUT);

    TabCreate(g_frame,WINW,ID_TAB0);
    PageShifterCreate(MakePage(LTAB_SHIFTER,PAGE_H));
    PageFfbCreate    (MakePage(LTAB_FFB,    PAGE_H));
    PageFpvCreate    (MakePage(LTAB_FPV,    PAGE_H));
    PageVrCreate     (MakePage(LTAB_VR,     PAGE_H));

    /* THE HEADER STRIP, one line, in this order: the switch, what it did, then where the game
       is. Settled 2026-08-01. It sits on the FRAME so all four tabs share the folder and so a
       greyed page can never reach the control that ungreys it. */
    g_switch=MkBtn(g_frame,"Enable this mod",26,TITLEBAND+HDR_Y-4,HDR_BTN_W,HDR_H,ID_TOGGLE);
    g_lamp=MkTextF(g_frame,"",26+HDR_BTN_W+14,TITLEBAND+HDR_Y+2,HDR_LAMP_W,22,ID_LAMP,g_fAction,0);
    g_dirLabel=MkTextF(g_frame,"Game folder",26+HDR_BTN_W+14+HDR_LAMP_W,TITLEBAND+HDR_Y+2,
                       88,22,0,g_fField,0);
    g_dirEdit=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_READONLY,
        26+HDR_BTN_W+14+HDR_LAMP_W+92,TITLEBAND+HDR_Y-1,
        WINW-(26+HDR_BTN_W+14+HDR_LAMP_W+92)-26-116,24,
        g_frame,(HMENU)(INT_PTR)ID_GAMEDIR,NULL,NULL);
    SendMessageA(g_dirEdit,WM_SETFONT,(WPARAM)g_fField,TRUE);
    g_chooseBtn=MkBtn(g_frame,"Choose...",WINW-26-110,TITLEBAND+HDR_Y-1,110,24,ID_CHOOSE);
    RefreshGameDir();

    /* THE VERSION IS IN THE BRAND STRIP, drawn by FrameProc beside the wordmark. It used to be a
       static control down here, bottom right on the paper - reported 2026-08-12: it landed in the
       middle of a page of sliders and read as a rendering fault rather than as a label. His fix,
       and it is the right one: put it where the product name is. */

    g_logBox=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        0,WINH-LOGH,WINW,LOGH,g_frame,(HMENU)(INT_PTR)ID_LOG,NULL,NULL);
    SendMessageA(g_logBox,WM_SETFONT,(WPARAM)g_fSmall,TRUE);

    LogOpen();                   /* the file first, so the box's first line is in it too */
    g_say_sink=LogLine;          /* from here on the engine talks into both */

    /* The shifter page's canvas is as tall as its content, not the blanket PAGE_H: a canvas
       longer than what is on it scrolls into empty paper, which reads as a page that lost
       something. */
    g_contentH[LTAB_SHIFTER]=gb_pageH;
    g_contentH[LTAB_FFB]=ffb_pageH;

    /* DirectInput wants an owner window, so the devices open after the frame exists. The timer
       lives on the page, not on the frame: a tab that is off must not be polling. */
    PageShifterOpenDevices(g_frame);
    SetTimer(g_page[LTAB_SHIFTER],GB_TIMER,8,NULL);
    PageFfbStart();
    /* the mod writes its status file about once a second; polling faster only burns cycles */
    SetTimer(g_page[LTAB_FFB],FFB_TIMER,1000,NULL);
    /* 8 ms, the same as the shifter's: a key held for a normal press must not be missed, and the
       handler returns immediately unless a row is actually waiting. */
    SetTimer(g_page[LTAB_FPV],FP_TIMER,8,NULL);

    SetTimer(g_frame,WATCH_TIMER,1000,NULL);   /* watches for the game coming up */
    SetTimer(g_frame,FLUSH_TIMER,300,NULL);    /* writes what changed since the last tick */
    SizeToContent();     /* after the pages, because it measures them */
    for(int i=0;i<LNTAB;i++){ RefreshPage(i); SyncScroll(i,1); }
    TabSelect(0);
    ShowWindow(g_frame,show);
}

static void RegisterClasses(HINSTANCE hi){
    WNDCLASSA c;
    for(int i=0;i<(int)sizeof(c);i++) ((BYTE*)&c)[i]=0;
    /* hbrBackground stays NULL on every class: the procs answer WM_ERASEBKGND themselves, and a
       class brush would flash grey under the paper. */
    c.lpfnWndProc=FrameProc; c.hInstance=hi; c.lpszClassName="MafiaLauncher";
    c.hCursor=LoadCursorA(NULL,IDC_ARROW);
    /* THE WINDOW'S OWN ICON. Having the resource in the .rc is only half of it: Explorer reads the
       FILE, while the title bar, the taskbar button and alt-tab read the WINDOW - and a class with
       hIcon NULL gets the system's blank one. Reported 2026-08-12: the application icon was
       not shown in the bar at the top. Two sizes, because Windows picks the small one for the
       title bar and the large one for alt-tab, and letting it downscale the large one gives a
       blurry 16 next to a crisp 32. WNDCLASSA carries only the large one - the small icon is a
       WNDCLASSEX field - so the 16 is set on the window itself with WM_SETICON in BuildUi. */
    c.hIcon=(HICON)LoadImageA(hi,MAKEINTRESOURCEA(IDI_APP),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);
    RegisterClassA(&c);
    c.hIcon=NULL;                   /* the helper classes are children and popups; no icon */
    c.lpfnWndProc=CanvasProc; c.lpszClassName="MafiaCanvas";
    RegisterClassA(&c);
    c.lpfnWndProc=DefWindowProcA; c.lpszClassName="MafiaView";
    RegisterClassA(&c);
    c.lpfnWndProc=SliderProc; c.lpszClassName="MafiaSlider";
    RegisterClassA(&c);
    c.lpfnWndProc=DlgProc; c.lpszClassName="MafiaDialog";
    RegisterClassA(&c);
    c.lpfnWndProc=AboutProc; c.lpszClassName="MafiaAbout";
    RegisterClassA(&c);
    /* No global slider ops any more: each slider carries its own block, because the camera page
       now has sliders too and a single global decided how BOTH pages drew. */
}

/* ---------------------------------------------------------------- self test ----------------
 * Everything below runs with no game and nothing on screen. It exists because the toggle IS the
 * install: a switch that half-installs is not a cosmetic defect, it is a folder somebody has to
 * repair by hand.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * It builds the REAL window with SW_HIDE rather than skipping it. RefreshPage invalidates its
 * controls, and InvalidateRect(NULL,...) does not do nothing - it invalidates the whole desktop.
 * A check that passes against half-created windows is testing a program nobody ships.
 */
static FILE *g_st;
static int   g_stFails;
static void STCheck(const char *what,int ok){
    fprintf(g_st,"  %s  %s\n",ok?"PASS":"FAIL",what);
    if(!ok) g_stFails++;
}

/* Everything an install can put in a scratch folder, BY NAME. No recursive delete anywhere in
   this program (docs/PATCHER.md) - and a test that was handed a wrong path is exactly when that
   rule earns its keep. A file left behind that is not on this list shows up as the next run's
   the scratch-folder-is-fresh check failing, which is the behaviour wanted from a hand-kept list. */
static int WipeDir(const char *dir){
    static const char *files[]={ "Game.exe","Game.exe.bak","dinput8.dll","mafia_ffb.asi",
        "mafia_fp.asi","mafia_fp.ini","gearbox_hook.asi",
        "gearbox hshifter setup\\gearbox-setup.exe","gearbox hshifter setup\\gearbox.ini",
        "mafia ffb setup\\mafia_ffb.ini",
        /* the presets. Writing is automatic now, so any run that touches the FFB tab leaves
           these behind - and the NEXT run's scratch-folder-is-fresh check is what said so. */
        "mafia ffb setup\\profiles\\p1.ini","mafia ffb setup\\profiles\\p2.ini",
        "mafia ffb setup\\profiles\\p3.ini",
        /* ...and where they have lived since 2026-08-07. The module-in-the-folder test switches the
           FFB mod on and writes the live file, so a run that dies half way must not leave them for
           the next run's "the scratch folder is fresh" to trip over. */
        "ALXG mods\\mafia ffb setup\\mafia_ffb.ini",
        "ALXG mods\\mafia ffb setup\\mafia_ffb_status.ini",
        "ALXG mods\\mafia ffb setup\\profiles\\p1.ini","ALXG mods\\mafia ffb setup\\profiles\\p2.ini",
        "ALXG mods\\mafia ffb setup\\profiles\\p3.ini",
        "ALXG mods\\gearbox hshifter setup\\gearbox.ini",
        /* the camera page's presets - written whenever that page is flushed */
        "mafia_fp_preset1.ini","mafia_fp_preset2.ini","mafia_fp_preset3.ini",
        "ALXG mods\\install.log",
        /* our own log: BuildUi opened it in this folder, because that is where the game is */
        BRAND_NAME ".log", BRAND_NAME ".log.1",
        /* AND THE NAME WE USED BEFORE THE 2026-08-12 RENAME. An uninstall has to clean up after
           the version that did the installing, not only after itself - a machine that ran 1.1.1
           has "ALXG Mafia Mods.log" in the game folder, and a delete list that only knows the new
           name leaves it there forever, with nothing left to remove it. Deleting a file that is
           not there costs one failed DeleteFile; leaving one behind is litter in a stranger's
           game folder. */
        "ALXG Mafia Mods.log","ALXG Mafia Mods.log.1" };
    static const char *dirs[]={ "gearbox hshifter setup","mafia ffb setup\\profiles",
        "mafia ffb setup","ALXG mods\\mafia ffb setup\\profiles","ALXG mods\\mafia ffb setup",
        "ALXG mods\\gearbox hshifter setup","ALXG mods\\original","ALXG mods" };
    char p[MAX_PATH]; int i;
    for(i=0;i<(int)(sizeof files/sizeof files[0]);i++){
        SCpy(p,dir); SCat(p,"\\"); SCat(p,files[i]); DeleteFileA(p);
    }
    for(i=0;i<(int)(sizeof dirs/sizeof dirs[0]);i++){
        SCpy(p,dir); SCat(p,"\\"); SCat(p,dirs[i]); RemoveDirectoryA(p);
    }
    RemoveDirectoryA(dir);
    return GetFileAttributesA(dir)==INVALID_FILE_ATTRIBUTES;
}

/* Is this key in that section at all? A STRING probe with a sentinel default, not
   GetPrivateProfileInt: the int form returns UINT, so a -1 "missing" default comes back as
   4294967295 and a key that is present but not a number reads as absent. */
static int IniHas(const char *path,const char *section,const char *key){
    char v[128];
    GetPrivateProfileStringA(section,key,"\x01",v,sizeof v,path);
    return v[0]!='\x01';
}

/* Does this string fit this many pixels, in the face it is drawn in? GetTextExtentPoint32 finds a
   clipped string in a millisecond - four controls on the FFB page were repaired for exactly that,
   each found on a photograph. */
static int STFits(HFONT f,const char *s,int w){
    HDC dc=CreateCompatibleDC(NULL); SIZE sz; HGDIOBJ o=SelectObject(dc,f);
    sz.cx=0; GetTextExtentPoint32A(dc,s,(int)SLen(s),&sz);
    SelectObject(dc,o); DeleteDC(dc);
    return sz.cx<=w;
}
static int STFfb(const char *path,const char *key){
    return (int)(LONG)GetPrivateProfileIntA("ffb",key,(UINT)-12345,path);
}
static int STVisible(HWND w){ return w&&(GetWindowLongA(w,GWL_STYLE)&WS_VISIBLE)!=0; }
/* Where a control landed, in its parent's coordinates - what the layout DID, not what its table
   says. Valid on the hidden window the self-test builds: a window keeps its rectangle when hidden. */
static POINT STPos(HWND w){
    RECT r; POINT p={-1,-1};
    if(!w) return p;
    GetWindowRect(w,&r); p.x=r.left; p.y=r.top;
    ScreenToClient(GetParent(w),&p);
    return p;
}
static int STTop(HWND w){ return STPos(w).y; }
static int STLeft(HWND w){ return STPos(w).x; }
/* Does any control on this page carry these words? Every descendant, buttons included. */
static const char *g_stFind; static int g_stFound;
static BOOL CALLBACK STFindText(HWND w,LPARAM lp){
    char t[400]; (void)lp;
    t[0]=0; GetWindowTextA(w,t,sizeof t);
    if(StrHas(t,g_stFind)){ g_stFound=1; return FALSE; }
    return TRUE;
}
static int STPageHas(HWND page,const char *s){
    g_stFind=s; g_stFound=0;
    if(page) EnumChildWindows(page,STFindText,0);
    return g_stFound;
}
/* The FFB page's pending write, and ONLY that page's. PageFlush writes every page with something
   pending, and the camera page's earlier checks leave theirs pending - flushing it here dropped a
   mafia_fp_preset1.ini into the scratch folder that the next run then found. */
static void STFlushFfb(void){
    if(!g_pending[LTAB_FFB]) return;
    g_pending[LTAB_FFB]=0;
    g_writing[LTAB_FFB]=1; FfbSaveIni(); g_writing[LTAB_FFB]=0;
}

static int SelfTest(const char *stockExe){
    char scr[MAX_PATH],p[MAX_PATH],before[33],after[33],what[200];
    int tab;

    g_st=fopen("selftest-report.txt","wb");
    if(!g_st) return 1;

    GetTempPathA(sizeof scr,scr); SCat(scr,"mafia-launcher-selftest");
    STCheck("the scratch folder is fresh",WipeDir(scr)&&CreateDirectoryA(scr,NULL));
    SCpy(p,scr); SCat(p,"\\Game.exe");
    STCheck("the stock Game.exe was copied in",CopyFileA(stockExe,p,FALSE));
    file_md5(p,before);

    SCpy(g_gameDir,scr); SCat(g_gameDir,"\\");
    Relocate();
    STCheck("the build is recognised",g_target.build!=NULL);

    BuildUi(SW_HIDE);

    for(tab=0;tab<LNTAB;tab++){
        if(!LTABS[tab].tag) continue;
        wsprintfA(what,"%s starts absent",LTABS[tab].name);
        STCheck(what,TabModState(scr,tab)==JMOD_ABSENT);
        wsprintfA(what,"switching %s on succeeds",LTABS[tab].name);
        STCheck(what,ToggleOn(tab)==1);
        wsprintfA(what,"%s reads ON from the journal afterwards",LTABS[tab].name);
        STCheck(what,TabModState(scr,tab)==JMOD_ON);
        if(mod_by_tag(LTABS[tab].tag)->bit & M_NEEDS_LOADER)
            STCheck("the ASI loader came with it",JModState(scr,J_MOD_LOADER)==JMOD_ON);
        wsprintfA(what,"the lamp says so: %s",ToggleLabel(g_state[tab]));
        STCheck(what,g_state[tab]==JMOD_ON);
        wsprintfA(what,"with %s on, its page is live",LTABS[tab].name);
        STCheck(what,CountDisabled(tab)==0);
        wsprintfA(what,"switching %s off succeeds",LTABS[tab].name);
        STCheck(what,ToggleOff(tab)==1);
        /* Everything but the three header controls, which have to stay live or the mod could
           never be switched back on. */
        wsprintfA(what,"with %s off, everything below the header is disabled",LTABS[tab].name);
        STCheck(what,CountDisabled(tab)==CountControls(tab)-HDR_CONTROLS);
        wsprintfA(what,"%s reads absent again",LTABS[tab].name);
        STCheck(what,TabModState(scr,tab)==JMOD_ABSENT);
    }

    /* ---- THE FIELD OF VIEW RIDES WITH THE CAMERA -------------------------------------------
     * Added 2026-08-07, after the first release archive went out leaving the game at 70 while
     * every bench here has run 86 for weeks. The exe is the only witness there is, so every
     * check below reads it back rather than asking the page what it believes.
     *
     * The last one is the one that matters: an arbitrary angle must undo to the ORIGINAL bytes,
     * not to "70" - the journal records what each site held, and 70 is only what it happened to
     * be this time. */
    {
        unsigned char *img; size_t n; int deg, start=0; char exe[MAX_PATH];
        SCpy(exe,scr); SCat(exe,"\\Game.exe");

        /* WHATEVER THIS EXE STARTS AT, not 70. The suite hands this self-test the Game.exe from
           the bench folder, which has been patched to 86 for weeks - so a check for starting at stock 70
           failed on a machine where nothing was wrong. The interesting property is not the
           number, it is that the undo returns to whatever was there BEFORE us. */
        img=read_file(exe,&n);
        STCheck("the field of view reads as one consistent angle",
                img&&fov_read(img,n,&start));
        if(img) free(img);

        TabSelect(LTAB_FPV); RefreshPage(LTAB_FPV); FpvLoadIni();
        STCheck("with the camera off, the slider offers an angle worth writing",
                fp_val[FP_FOV]==(start==FOV_STOCK?FOV_RECOMMENDED:start));
        /* WITH NO FILE YET, THE PAGE STILL AGREES WITH THE INI IT WOULD INSTALL.
         *
         * The page reads that default out of the embedded payload instead of restating it in C.
         * Which makes the obvious check - page == fp_ShippedIniInt(...) - CIRCULAR: both sides
         * would be the same broken parser, and a parser that always returned its fallback would
         * pass. [[circular-labelling-trap]] is in this project's memory for exactly this shape.
         *
         * So the witness is independent and blunt: the parser must find a key whose value is not
         * its fallback, and the shipped default must be the -1 we mean to ship. Change the payload
         * on purpose and these fail on purpose - which is the point, because the page follows the
         * payload silently and something has to speak up. */
        STCheck("the payload parser reads a real key, not its own fallback",
                fp_ShippedIniInt("route",-999)==2 && fp_ShippedIniInt("no_such_key",-999)==-999);
        STCheck("the ini we ship still turns the interface fix ON",
                fp_ShippedIniInt("hud_aspect_pct",0)==-1);
        STCheck("...and with no ini yet, the button already says so",fp_uifix==1);

        STCheck("switching the camera on succeeds",ToggleOn(LTAB_FPV)==1);
        img=read_file(exe,&n); deg=0;
        STCheck("...and the exe carries the angle the slider offered, without a tab of its own",
                img&&fov_read(img,n,&deg)&&deg==FpvWantedFov());
        if(img) free(img);

        /* THE PAGE MUST SHOW THE FILE THE INSTALL JUST WROTE.
         *
         * Found 2026-08-15 on Alex's own fresh copy of the game: he enabled the mod and the
         * wide-screen button still read OFF, while the mafia_fp.ini the install had just placed
         * beside it said hud_aspect_pct = -1. ToggleOn writes the payload and calls RefreshPage,
         * which repaints the lamp, the switch and the build line and touches no page VALUE - so
         * fp_uifix kept the 0 that fp_LoadFrom chose back when the file did not exist yet.
         *
         * Two defects in one, and the second is the dangerous one: the window reported a state
         * the disk disagreed with, and the next save from that page would have written that 0
         * back over the shipped -1. That is [[unplugged-device-clobber]] exactly - never write a
         * control the page has not loaded.
         *
         * Checked here rather than at a fresh ToggleOn of its own, because THIS is the fresh-copy
         * moment: the block above deliberately never writes mafia_fp.ini, so the file did not
         * exist until the toggle on the line above created it.
         *
         * THE CAPTION IS CHECKED, NOT ONLY fp_uifix. What he saw was a BUTTON that said OFF, and
         * a test that asserted the variable alone would go green the moment the value was
         * reloaded while the window carried on lying - which is the half of this defect that
         * reached him. */
        {
            char ini[MAX_PATH],cap[120]; int onDisk;
            SCpy(ini,scr); SCat(ini,"\\mafia_fp.ini");
            onDisk=(int)GetPrivateProfileIntA("fp","hud_aspect_pct",0,ini);
            STCheck("the install wrote an ini whose interface fix is ON",onDisk!=0);
            STCheck("...and the page shows it ON without being reloaded by hand",fp_uifix==1);
            cap[0]=0;
            if(fp_uifix_btn) GetWindowTextA(fp_uifix_btn,cap,sizeof cap);
            STCheck("...and the BUTTON says so too, which is what he actually looked at",
                    SEq(cap,FP_UIFIX_ON_TEXT));
        }

        /* The slider, driven the way the page drives it - but through fp_ApplyFov rather than
           FpvSaveIni, which would also write mafia_fp.ini. That file is not this check's
           business, and writing it here made the exe test destroy the round-trip test at the
           end of this file: an ini the user has edited is deliberately NOT removed by an
           uninstall, its journal line survives, the mod therefore does not read ABSENT, and the
           ASI loader stays behind with it. All correct behaviour, all triggered by a test
           writing a settings file it had no reason to write. */
        fp_SetRow(FP_FOV,100);
        fp_ApplyFov();
        img=read_file(exe,&n); deg=0;
        STCheck("moving the slider rewrites the exe to 100",
                img&&fov_read(img,n,&deg)&&deg==100);
        if(img) free(img);
        /* Checked HERE and not at the toggle: an exe that already held the angle we were about
           to write gets no journal line, because fov_write records only the sites it changes.
           On a bench exe already at 86 the toggle is therefore a no-op, and the first real
           record is this slider move. */
        STCheck("the FOV patch is journalled under its own tag",
                JModState(scr,J_MOD_FOV)==JMOD_ON);

        /* Reloading the page must show what is in the exe now, not the recommended value. */
        FpvLoadIni();
        STCheck("...and the page reads 100 back out of the exe",fp_val[FP_FOV]==100);

        /* PUT THE PAGE OUT OF STEP WITH THE FOLDER ON PURPOSE, or the check after the switch-off
           cannot fail: the page already holds the value a reload would produce, so a ToggleOff
           that re-reads nothing looks exactly like one that does. A test that cannot fail is not
           a test.
           Flipped in memory only. Saving it would write mafia_fp.ini, and an EDITED ini is
           deliberately NOT removed by an uninstall - which would take the check with it. */
        fp_uifix=!fp_uifix; fp_RefreshUiFix();

        STCheck("switching the camera off succeeds",ToggleOff(LTAB_FPV)==1);
        img=read_file(exe,&n); deg=0;
        STCheck("the exe is back where it started even though it was last written 100",
                img&&fov_read(img,n,&deg)&&deg==start);
        if(img) free(img);
        STCheck("and the FOV tag is gone from the journal",
                JModState(scr,J_MOD_FOV)==JMOD_ABSENT);

        /* SWITCHING OFF REREADS TOO, and this is checked BEFORE the FpvLoadIni below - a hand
           reload here would hide the very thing being tested. ToggleOff had the same hole ToggleOn
           did, in the other direction: it removed the files and left the page describing a folder
           that no longer had them. Milder, because the page is greyed afterwards and nothing can
           be saved from it, but it is still the window disagreeing with the disk. */
        {
            char ini[MAX_PATH],cap[120]; int want;
            SCpy(ini,scr); SCat(ini,"\\mafia_fp.ini");
            STCheck("switching the camera off took its untouched ini with it",
                    GetFileAttributesA(ini)==INVALID_FILE_ATTRIBUTES);
            want=(fp_ShippedIniInt("hud_aspect_pct",0)!=0)?1:0;
            cap[0]=0;
            if(fp_uifix_btn) GetWindowTextA(fp_uifix_btn,cap,sizeof cap);
            STCheck("...and the page went back to describing a folder with nothing in it",
                    fp_uifix==want && SEq(cap,want?FP_UIFIX_ON_TEXT:FP_UIFIX_OFF_TEXT));
        }

        /* A camera switched off must not leave a patched exe behind, and it must not leave the
           page pointing at an angle nobody can reach either. */
        FpvLoadIni();
        STCheck("with the camera off again, the slider offers that angle again",
                fp_val[FP_FOV]==(start==FOV_STOCK?FOV_RECOMMENDED:start));
    }

    /* The dialogs are modal and cannot be driven headlessly, so what is tested is the part that
       DECIDES: the flag that suppresses the enable warning, and that it suppresses only that
       one. The are-you-sure-you-want-to-undo-an-install prompt is never skippable. */
    SetSkipWarning(0);
    STCheck("the enable warning is on by default",!SkipWarning());
    SetSkipWarning(1);
    STCheck("do-not-show-again is remembered",SkipWarning());
    SetSkipWarning(0);
    STCheck("and can be turned back on",!SkipWarning());

    /* The VR tab installs nothing, so the shared switch must go dead when you are looking at
       it. A toggle that looks live and does nothing is worse than one that says why it cannot
       help. Checked by selecting the tab, because there is ONE switch now and it follows the
       tab you are on. */
    TabSelect(LTAB_VR); RefreshPage(LTAB_VR);
    STCheck("on the VR tab the switch is disabled",!IsWindowEnabled(g_switch));
    STCheck("and the tab says why",GetWindowTextLengthA(g_vrReason)>40);
    TabSelect(LTAB_FPV); RefreshPage(LTAB_FPV);
    STCheck("on the First person tab it works again",IsWindowEnabled(g_switch));
    TabSelect(0); RefreshPage(0);

    /* THE PORT'S ONE REAL TRAP. gearbox-setup.exe lived inside gearbox hshifter setup\ and
       built this path from the folder its exe sat in; this program sits in the game ROOT. A
       path built from the wrong base writes a perfectly good ini the mod never opens, and
       nothing reports anything. */
    ShifterSaveIni();
    SCpy(p,scr); SCat(p,"\\" GEARBOX_DIR "\\gearbox.ini");
    STCheck("the bindings go into the gearbox folder",file_exists(p));
    STCheck("with the keys the mod reads",
            IniHas(p,"shift","mode_hold")&&IniHas(p,"shift","gearup_dik")
            &&IniHas(p,"shift","gameptr"));
    SCpy(p,scr); SCat(p,"\\gearbox.ini");
    STCheck("and nothing is written to the game root",!file_exists(p));
    /* ONE FOLDER, NOT THREE - decided 2026-08-07, looking at a fresh install: it would be
       good to tuck both Gearbox and Mafia FFB inside ALXG mods. GEARBOX_DIR above already carries the
       new path, so the check that it lands in the right place passes either way; what needs a
       test of its own is that the OLD place stays empty. A path constant that moves in one of
       the four programs that spell it is exactly the failure this catches. */
    SCpy(p,scr); SCat(p,"\\gearbox hshifter setup");
    STCheck("the gearbox folder is not in the game root any more",!file_exists_dir(p));
    ffb_MkDirs();
    SCpy(p,scr); SCat(p,"\\mafia ffb setup");
    STCheck("...and neither is the force feedback folder",!file_exists_dir(p));
    SCpy(p,scr); SCat(p,"\\" ALXG_DIR "\\mafia ffb setup\\profiles");
    STCheck("the FFB profiles live under ALXG mods too",file_exists_dir(p));

    /* The harvest that fixed binder-clobbers-ini: whatever the file already says about the
       address table survives a save. It is the first thing a port loses. */
    SCpy(p,scr); SCat(p,"\\" GEARBOX_DIR "\\gearbox.ini");
    WritePrivateProfileStringA("shift","gameptr","0xDEADBEEF",p);
    ShifterSaveIni();
    { char v[64]; GetPrivateProfileStringA("shift","gameptr","",v,sizeof v,p);
      STCheck("a hand-edited address survives the next save",SEq(v,"0xDEADBEEF")); }

    /* A BINDING ON A DEVICE THAT IS NOT PLUGGED IN SURVIVES TOO, and this one is not a matter of
       tidiness: found 2026-08-01 by photographing the page with the wheel switched off, every gear
       read "click to set" while the file held gear1=1:0, and the automatic write would then have
       saved that emptiness over eight real bindings on the next click anywhere on the page.
       The device name below cannot be attached to any machine, so the check means the same thing
       whether or not a wheel is plugged into THIS one - which is the whole point, because the
       failing case is precisely "no wheel". */
    WritePrivateProfileStringA("hook","device1","NO SUCH DEVICE 12345",p);
    WritePrivateProfileStringA("shift","gear1","1:3",p);
    WritePrivateProfileStringA("shift","gear2","1:4",p);
    ShifterLoadIni();
    { char t[320];
      GetWindowTextA(gb_btn[0],t,sizeof t);
      STCheck("an unplugged device's binding still reads as bound",
              strstr(t,"button 3")!=NULL&&strstr(t,"NO SUCH DEVICE")!=NULL);
      STCheck("and says why it cannot be used",strstr(t,"not plugged in")!=NULL); }
    ShifterSaveIni();
    { char v[64];
      GetPrivateProfileStringA("shift","gear1","",v,sizeof v,p);
      STCheck("and a save keeps it rather than dropping the row",SEq(v,"1:3"));
      GetPrivateProfileStringA("shift","gear2","",v,sizeof v,p);
      STCheck("every row of it, not just the first",SEq(v,"1:4"));
      GetPrivateProfileStringA("hook","device1","",v,sizeof v,p);
      STCheck("with the device it names kept in slot 1",SEq(v,"NO SUCH DEVICE 12345")); }

    /* EVERY KEY THE MOD READS, PRESENT AND CORRECT, and then the same file read back. Ported
       here on 2026-08-04 from gearbox-setup.exe's own --selftest, which is being retired: this
       window is the only binder now, and a check that leaves with the tool it lived in is a
       check nobody notices the loss of. The failure it exists to catch is a key that quietly
       stops being written - mode_hold once vanished from the file while the checkbox was
       ticked, and only a real drive found out. */
    /* From a FRESH file: the checks above deliberately left a hand-edited address and an
       unpluggable device in this one, and the harvest would - correctly - keep both, so a table
       of expected defaults read against it would be testing the previous check. */
    SCpy(p,scr); SCat(p,"\\" GEARBOX_DIR "\\gearbox.ini");
    DeleteFileA(p);
    ShifterSelfTestRig();
    ShifterSaveIni();
    {
        static const char *K[][3] = {
            {"hook","device1","ALXG-TEST-SHIFTER"}, {"hook","suppress1","0,1,2,3,4,5,7"},
            {"hook","device2","ALXG-TEST-WHEELBASE"}, {"hook","suppress2","38"},
            {"shift","enable","1"},
            {"shift","gear1","1:0"}, {"shift","gear6","1:5"}, {"shift","reverse","1:7"},
            {"shift","neutral","-1"}, {"shift","mode_btn","2:38"},
            {"shift","gearup_dik","0x1E"}, {"shift","geardown_dik","0x2C"},
            /* 0x32 is DIK_M, the key Mafia uses for automatic/manual. It read 0x30 (B) until
               2026-08-07, which is a key the game does not use for the gearbox - so this
               assertion was pinning a default that could not work. */
            {"shift","mode_dik","0x32"}, {"shift","mode_hold","1"},
            {"shift","hold_ms","40"}, {"shift","gap_ms","60"}, {"shift","retry_ms","1200"},
            {"shift","neutral_delay_ms","1100"},
            {"shift","gameptr","0x63788C"}, {"shift","gear_ofs","0x5D0"},
            {"shift","mode_ofs","0x53C"},
            {NULL,NULL,NULL} };
        int i; char v[160];
        for(i=0;K[i][0];i++){
            GetPrivateProfileStringA(K[i][0],K[i][1],"<missing>",v,sizeof v,p);
            wsprintfA(what,"%s=%s (want %s)",K[i][1],v,K[i][2]);
            STCheck(what,SEq(v,K[i][2]));
        }
    }
    ShifterSelfTestClear();
    ShifterLoadIni();
    STCheck("gear 1 comes back on the shifter",ShifterRowIs(0,"ALXG-TEST-SHIFTER",0));
    STCheck("gear 6 comes back",ShifterRowIs(5,"ALXG-TEST-SHIFTER",5));
    STCheck("reverse comes back",ShifterRowIs(6,"ALXG-TEST-SHIFTER",7));
    STCheck("neutral stays unbound (it is the rest position)",ShifterRowIs(7,NULL,-1));
    STCheck("the mode control comes back on the OTHER device",
            ShifterRowIs(ROW_MODEBTN,"ALXG-TEST-WHEELBASE",38));
    STCheck("the game keys come back",ShifterKeysAre(0x1E,0x2C,0x32));
    STCheck("the switch/toggle choice survives the round trip",gb_holdOn==1);

    /* THE WHOLE ADDRESS TABLE a file already carries survives a save, not only the one address
       the older check probed. This is binder-clobbers-ini: a save used to write the built-in GOG
       numbers back over whatever was there and killed a hand-maintained custom-install config
       twice, reporting success both times. */
    {
        static const char *P[][2] = {
            {"gameptr","0x65115C"}, {"gear_ofs","0x648"}, {"mode_ofs","0x5B8"},
            {"closed_loop","0"}, {"neutral_delay_ms","900"}, {NULL,NULL} };
        int i; char v[160];
        for(i=0;P[i][0];i++) WritePrivateProfileStringA("shift",P[i][0],P[i][1],p);
        ShifterSaveIni();
        for(i=0;P[i][0];i++){
            GetPrivateProfileStringA("shift",P[i][0],"<missing>",v,sizeof v,p);
            wsprintfA(what,"%s survives a save: %s (want %s)",P[i][0],v,P[i][1]);
            STCheck(what,SEq(v,P[i][1]));
        }
        /* and it was a real save, not a refusal that happened to leave the file alone */
        GetPrivateProfileStringA("shift","gear1","<missing>",v,sizeof v,p);
        STCheck("the bindings still landed in that same file",v[0]=='1'&&v[1]==':');
    }

    /* ---------------------------------------------------------------------------------------
     * THE FORCE FEEDBACK INI CONTRACT. Ported here 2026-08-04 from mafia-ffb-setup.exe, retired
     * the same day. Every check below guards the agreement between this page and
     * src/ffb/ffb_settings.c - a renamed key, a dropped key or a value the mod refuses is
     * invisible on screen and shows up as a wheel that does not do what the dialog says.
     * ------------------------------------------------------------------------------------ */
    {
        char ini[MAX_PATH],ini2[MAX_PATH],v[96]; int i;
        /* the plain rows, NPLAIN of them: one key each. The road model's rows write keys of their
           own and are checked by value further down. 400 on the crash row is the new top of the
           travel (2026-09-30), 600 the gunfire's. */
        static const int want[NPLAIN]={0,400,300,7,600,250};

        SCpy(ini,scr); SCat(ini,"\\selftest-ffb.ini");
        DeleteFileA(ini);
        ffb_ResetValues();
        for(i=0;i<NPLAIN;i++) ffb_val[i]=100;
        ffb_range=DOR_DEFAULT; ffb_truck=0; ffb_device[0]=0;
        ffb_SaveTo(ini,0);
        STCheck("the FFB settings file is created",file_exists(ini));
        for(i=0;i<NPLAIN;i++){
            wsprintfA(what,"a fresh file: %s=%d (want 100)",SKEY[i],
                      (int)GetPrivateProfileIntA("ffb",SKEY[i],-1,ini));
            STCheck(what,(int)GetPrivateProfileIntA("ffb",SKEY[i],-1,ini)==100);
        }
        STCheck("a fresh file: range=600",(int)GetPrivateProfileIntA("ffb","range",-1,ini)==600);
        STCheck("a fresh file: a truck steers like a car",
                (int)GetPrivateProfileIntA("ffb","truck",-1,ini)==0);

        for(i=0;i<NPLAIN;i++) ffb_val[i]=want[i];
        ffb_range=1440;
        ffb_SaveTo(ini,0);
        for(i=0;i<NPLAIN;i++) ffb_val[i]=-1;
        ffb_range=0;
        ffb_LoadFrom(ini);
        for(i=0;i<NPLAIN;i++){
            wsprintfA(what,"%s came back as %d (want %d)",SKEY[i],ffb_val[i],want[i]);
            STCheck(what,ffb_val[i]==want[i]);
        }
        STCheck("the rotation range came back as 1440",ffb_range==1440);

        /* NO FILE MUST MEAN THE RECOMMENDED SET, NOT WHATEVER THE PAGE LAST HELD.
         *
         * Alex, 2026-08-15: the gunfire row read 100 when he opened the utility and jumped to 0
         * the moment he pressed Reset to Default - two different answers to "what is the default"
         * from one window. FfbLoadIni used to RETURN on a missing file without touching a single
         * slider, so the page went on showing whatever ffb_val[] happened to contain: zeros in a
         * fresh process, and the previous folder's numbers on any later call.
         *
         * Deliberately seeded with the OPPOSITE of the answer first - every row 100, which is the
         * wrong value for exactly the row he reported - so this cannot pass by accident. */
        {
            char noini[MAX_PATH];
            ffb_IniPath(noini);
            DeleteFileA(noini);
            for(i=0;i<NSLIDER;i++) ffb_val[i]=100;
            FfbLoadIni();
            for(i=0;i<NSLIDER;i++){
                wsprintfA(what,"with no file, %s shows the recommended %d",SKEY[i],SREF[i]);
                STCheck(what,ffb_val[i]==SREF[i]);
            }
            STCheck("...and gunfire is the row that proves it - 0, not 100",
                    ffb_val[S_GUN]==0 && SREF[S_GUN]==0);
            /* PUT BACK WHAT THIS BLOCK CONSUMED. The checks below carry on from the values the
               round-trip above left in memory, and a test that quietly changes the state its
               neighbours depend on is a second bug wearing the first one's clothes. */
            ffb_LoadFrom(ini);
        }

        /* a key with no control is PRESERVED, not clobbered - `truck` is a feature that was declined,
           so silently resetting it would be worse than losing a number */
        WritePrivateProfileStringA("ffb","truck","100",ini);
        ffb_truck=0;                                  /* deliberately stale in memory */
        ffb_SaveTo(ini,0);
        STCheck("truck=100 set by hand survives a save",
                (int)GetPrivateProfileIntA("ffb","truck",-1,ini)==100);
        STCheck("...and the sliders still wrote",
                (int)GetPrivateProfileIntA("ffb","master",-1,ini)==0);

        /* the same rule one type wider: `device` is a STRING with no control. Getting this wrong
           drops the user's wheel selection and the symptom appears one launch later, on the
           wrong device - the hardest kind of report to act on. */
        WritePrivateProfileStringA("ffb","device","{6F1D2B60-D5A0-11CF-BFC7-444553540000}",ini);
        ffb_device[0]=0;
        ffb_SaveTo(ini,0);
        v[0]=0; GetPrivateProfileStringA("ffb","device","",v,sizeof v,ini);
        STCheck("device= set by hand survives a save",
                SLen(v)==38&&v[0]=='{'&&v[37]=='}');
        SCpy(ini2,scr); SCat(ini2,"\\selftest-ffb-nodev.ini");
        DeleteFileA(ini2);
        ffb_device[0]=0;
        ffb_SaveTo(ini2,0);
        v[0]=0; GetPrivateProfileStringA("ffb","device","<absent>",v,sizeof v,ini2);
        STCheck("a fresh file gets NO device key at all",v[0]=='<');
        DeleteFileA(ini2);

        /* every range this page can emit is one the mod accepts */
        for(i=0;i<NDOR;i++){
            ffb_range=DORDEG[i]; ffb_SaveTo(ini,0);
            wsprintfA(what,"range %d round-trips",DORDEG[i]);
            STCheck(what,(int)GetPrivateProfileIntA("ffb","range",-1,ini)==DORDEG[i]);
        }
        STCheck("the two ranges added 2026-07-27 are offered",DORDEG[2]==540&&DORDEG[4]==720);
        STCheck("the list is sorted, so the buttons read left to right",
                DORDEG[0]<DORDEG[1]&&DORDEG[1]<DORDEG[2]&&DORDEG[2]<DORDEG[3]&&
                DORDEG[3]<DORDEG[4]&&DORDEG[4]<DORDEG[5]&&DORDEG[5]<DORDEG[6]&&
                DORDEG[6]<DORDEG[7]);
        STCheck("600 is the default stop",DOR_DEFAULT==600);
        /* a value the mod refuses must never become the one on screen: the mod would keep its
           previous range and the page would be showing something that is not in force */
        ffb_range=600; ffb_SaveTo(ini,0); ffb_LoadFrom(ini);
        WritePrivateProfileStringA("ffb","range","1234",ini);
        ffb_LoadFrom(ini);
        STCheck("loading a file with an unaccepted range keeps the previous one",ffb_range==600);

        /* the snaps, on THIS page's ops - a global set of them decided how another page's
           sliders drew, which is why they travel with the window now */
        STCheck("98 on the slider snaps to the reference",
                SliderMagnetise(&FFB_SLIDER_OPS,S_OBJ,98)==100);
        STCheck("102 on the slider snaps to the reference",
                SliderMagnetise(&FFB_SLIDER_OPS,S_OBJ,102)==100);
        STCheck("120 on the slider is left alone",
                SliderMagnetise(&FFB_SLIDER_OPS,S_OBJ,120)==120);
        STCheck("2 on the gunfire slider snaps to zero",
                SliderMagnetise(&FFB_SLIDER_OPS,S_GUN,2)==0);
        STCheck("99 on the master slider snaps to the reference",
                SliderMagnetise(&FFB_SLIDER_OPS,S_MASTER,99)==100);

        /* HEADROOM TO 400%, his decision of 2026-09-30: players on weaker wheelbases must be able
           to crank the force three or four times. Every force row runs 0..400 with the tall mark
           still at 100, gunfire keeps its 600, and the box stops where the slider does - a box
           that went further would write a value the slider cannot show. Above 100 the crash kick
           is still clamped at the module's crash cap until the cap grows with the slider - a
           module need, and the row's hint says so rather than the slider pretending otherwise. */
        {
            static const int FORCE[]={S_MASTER,S_CRASH,S_OBJ,S_PED,S_ROAD,
                                      S_GWEIGHT,S_GPARK,S_GDRIVE,S_GTEXT};
            int k,ok=1;
            for(k=0;k<(int)(sizeof FORCE/sizeof FORCE[0]);k++){
                i=FORCE[k];
                if(SMIN[i]!=0||SMAX[i]!=400||SBOXMAX[i]!=400||SREF[i]!=100){
                    wsprintfA(what,"%s runs 0..400 with its tall mark at 100 (has %d..%d, box %d, "
                              "mark %d)",SNAME[i],SMIN[i],SMAX[i],SBOXMAX[i],SREF[i]);
                    STCheck(what,0); ok=0;
                }
            }
            STCheck("every force row runs 0..400%, the tall mark at 100 - all nine",ok&&k==9);
            ok=1;
            for(i=0;i<NSLIDER;i++) if(SMAX[i]!=SBOXMAX[i]) ok=0;
            STCheck("on every row the box stops where the slider does",ok);
        }
        STCheck("gunfire keeps its six times, recommended 0 with a mark at 100",
                SMIN[S_GUN]==0&&SMAX[S_GUN]==600&&SREF[S_GUN]==0&&SALT[S_GUN]==100);
        STCheck("the two rows that are not forces keep their own range: 5..15 deg and 0..100",
                SMIN[S_GBREAK]==5&&SMAX[S_GBREAK]==15&&SMIN[S_GSLIP]==0&&SMAX[S_GSLIP]==100);
        /* the box, driven the way a player types into it: EN_CHANGE reaches the page through the
           canvas, and a typed 999 must land on the top of the travel, not past it */
        ffb_SetRow(S_CRASH,100);
        SetWindowTextA(ffb_box[S_CRASH],"999");
        STCheck("typing 999 into the crashes box gives 400, the top of its travel",
                ffb_val[S_CRASH]==400);
        SetWindowTextA(ffb_box[S_GUN],"999");
        STCheck("...and into the gunfire box 600",ffb_val[S_GUN]==600);
        ffb_SetRow(S_CRASH,450);
        STCheck("a row set past its top is held at it",ffb_val[S_CRASH]==400);
        ffb_SetRow(S_CRASH,want[S_CRASH]); ffb_SetRow(S_GUN,want[S_GUN]);
        STCheck("above 100 the crash row says the big hits clip",
                StrHas(ffb_HintFor(S_CRASH,120),"clip")&&!ffb_HintFor(S_CRASH,100)[0]);
        STCheck("gunfire reaches zero, so the tap can be silenced",
                ffb_HintFor(S_GUN,0)[0]=='o'&&ffb_HintFor(S_GUN,0)[1]=='f');

        /* THE OLD FEEDBACK'S KEYS ARE CARRIED, NOT EDITED, 2026-09-30. Their rows left with the
           Legacy model - the spring, slide feel, the damper pair and the truck pair - and a key
           this page does not write is a key it must not destroy. The road module still applies
           five of them, so the page NAMES any that is off its reference. The single pre-split
           `damper` is read the way the module reads it (ffb_settings.c: the default of both
           halves) and is otherwise carried like the rest - no longer migrated by a save. */
        DeleteFileA(ini);
        WritePrivateProfileStringA("ffb","master","100",ini);
        WritePrivateProfileStringA("ffb","spring","150",ini);
        WritePrivateProfileStringA("ffb","sat","200",ini);
        WritePrivateProfileStringA("ffb","damper","40",ini);
        WritePrivateProfileStringA("ffb","truck_moving","800",ini);
        ffb_LoadFrom(ini);
        STCheck("the old single damper=40 is read as the module reads it - both halves 40",
                ffb_old[1]==40&&ffb_old[2]==40);
        STCheck("...slide feel and the truck damper as written, the other truck half at 100",
                ffb_old[0]==200&&ffb_old[4]==800&&ffb_old[3]==100);
        {
            char note[400];
            ffb_OldNote(note,sizeof note);
            STCheck("the page names the old values the road model still applies",
                    StrHas(note,"slide feel 200%")&&StrHas(note,"old parking damper 40%")&&
                    StrHas(note,"old driving damper 40%")&&
                    StrHas(note,"truck driving damper 800%")&&!StrHas(note,"truck parking"));
            note[0]=0;
            if(ffb_noteOld) GetWindowTextA(ffb_noteOld,note,sizeof note);
            STCheck("...on the page itself, under the columns",StrHas(note,"slide feel 200%"));
        }
        /* the presence probe itself, because getting it wrong is silent: GetPrivateProfileInt
           returns UINT, so a `< 0` sentinel test compiles, runs, and is never true */
        STCheck("KeyPresent sees a key that is there",ffb_KeyPresent(ini,"damper"));
        STCheck("KeyPresent does not see one that is not",!ffb_KeyPresent(ini,"damper_static"));
        ffb_SetRow(S_MASTER,90);
        ffb_SaveTo(ini,0);
        STCheck("a save carries every old key exactly: spring 150, sat 200, damper 40, "
                "truck_moving 800",
                STFfb(ini,"spring")==150&&STFfb(ini,"sat")==200&&STFfb(ini,"damper")==40&&
                STFfb(ini,"truck_moving")==800);
        STCheck("...and writes none it was not given - no damper halves, no truck_static",
                !IniHas(ini,"ffb","damper_static")&&!IniHas(ini,"ffb","damper_moving")&&
                !IniHas(ini,"ffb","truck_static"));
        STCheck("...while the slider that moved wrote",STFfb(ini,"master")==90);
        {   /* the longest such line, every value three digits, fits the page */
            char note[400]; int k;
            for(k=0;k<NOLD;k++) ffb_old[k]=800;
            ffb_OldNote(note,sizeof note);
            STCheck("the note with all five old values fits one line of the page",
                    StrHas(note,"(mafia_ffb.ini)")&&STFits(g_fSmall,note,W_FULL));
            for(k=0;k<NOLD;k++) ffb_old[k]=100;
            ffb_OldNote(note,sizeof note);
            STCheck("...and says nothing when every one is on its reference",!note[0]);
        }
        DeleteFileA(ini);

        /* A SLIDER MUST NOT BE ABLE TO DELETE THE MODEL, 2026-09-14.
           ini_carry_verify.c proves the helper on its own; this proves the WIRING, through the
           real save the buttons call. Until this date the page preserved a hand-written list of
           seven keys and destroyed the rest of [ffb] - by then the module read fifty-five more
           and the bench file held 245, so one drag would have taken the approved speed curve,
           the F-key banks and the roll and crash trims with it.
           The keys below are the real ones, not invented: ground_spd6_pct is a point on the
           curve he specified band by band, preset7_sat_k a bank his F-keys address, and
           road_roll_gain_pct / crash_trim_pct the two v813 trims awaiting his drive. */
        WritePrivateProfileStringA("ffb","master","100",ini);
        WritePrivateProfileStringA("ffb","ground","1",ini);
        WritePrivateProfileStringA("ffb","ground_spd6_pct","84",ini);
        WritePrivateProfileStringA("ffb","preset7_sat_k","7875",ini);
        WritePrivateProfileStringA("ffb","road_roll_gain_pct","25",ini);
        WritePrivateProfileStringA("ffb","crash_trim_pct","85",ini);
        WritePrivateProfileStringA("ffb","diag","1",ini);
        ffb_LoadFrom(ini);
        ffb_val[S_MASTER]=60;              /* the drag that used to cost him the file */
        ffb_SaveTo(ini,0);
        STCheck("a slider save keeps the ground channel switched on",
                GetPrivateProfileIntA("ffb","ground",0,ini)==1);
        STCheck("...keeps the approved speed curve",
                GetPrivateProfileIntA("ffb","ground_spd6_pct",0,ini)==84);
        STCheck("...keeps the F-key banks",
                GetPrivateProfileIntA("ffb","preset7_sat_k",0,ini)==7875);
        STCheck("...keeps the roll and crash trims",
                GetPrivateProfileIntA("ffb","road_roll_gain_pct",0,ini)==25&&
                GetPrivateProfileIntA("ffb","crash_trim_pct",0,ini)==85);
        STCheck("...keeps diag, so the next drive still writes a log",
                GetPrivateProfileIntA("ffb","diag",0,ini)==1);
        STCheck("...and the slider it was sent to move did move",
                GetPrivateProfileIntA("ffb","master",0,ini)==60);
        DeleteFileA(ini);

        /* ONE FEEDBACK MODEL, 2026-09-30. His corrections of that evening took the selector of
           three down to one - no Legacy ("whoever needs it will install the old 1.4.3 by hand"),
           then no developers' feedback. The page chooses no model, so it must neither write nor
           own the two keys that did: `ffb_mode` and `ground` are carried exactly as a file has
           them, and the module's handling of them is left alone (GroundReadIni: ffb_mode 0 = the
           road model, 1 or 2 = off, anything else or absent = `ground` decides, default on). */
        DeleteFileA(ini);
        ffb_ResetValues();
        ffb_SaveTo(ini,0);
        STCheck("a fresh file names no model - no ffb_mode, no ground",
                !IniHas(ini,"ffb","ffb_mode")&&!IniHas(ini,"ffb","ground"));
        {   int ok=1;
            for(i=0;FFB_OWNED[i];i++)
                if(SEq(FFB_OWNED[i],"ffb_mode")||SEq(FFB_OWNED[i],"ground")) ok=0;
            STCheck("...and the page does not own either",ok); }
        WritePrivateProfileStringA("ffb","ffb_mode","2",ini);
        WritePrivateProfileStringA("ffb","ground","0",ini);
        ffb_LoadFrom(ini);
        ffb_SetRow(S_MASTER,70);
        ffb_SaveTo(ini,0);
        STCheck("a file's own ffb_mode=2 and ground=0 survive a save untouched",
                STFfb(ini,"ffb_mode")==2&&STFfb(ini,"ground")==0&&STFfb(ini,"master")==70);
        /* ...and a file that switches the road model off is SAID, because every slider on the
           page then moves nothing */
        STCheck("the page reads that file as asking for the developers' feedback, and says so",
                ffb_FileModel()==2&&ffb_ModelWarning()&&
                StrHas(ffb_ModelWarning(),"ffb_mode = 2"));
        {   char line[400]; line[0]=0;
            if(ffb_modeText) GetWindowTextA(ffb_modeText,line,sizeof line);
            STCheck("...on the red line above the sliders, which is shown and given room",
                    StrHas(line,"ffb_mode = 2")&&STVisible(ffb_modeText)&&ffb_laidWarn==1); }
        WritePrivateProfileStringA("ffb","ffb_mode","1",ini);
        ffb_LoadFrom(ini);
        STCheck("ffb_mode=1 (the old feedback) reads as the road model switched off",
                ffb_FileModel()==1&&ffb_ModelWarning()&&
                StrHas(ffb_ModelWarning(),"switches the road model off"));
        WritePrivateProfileStringA("ffb","ffb_mode",NULL,ini);
        ffb_LoadFrom(ini);
        STCheck("with no ffb_mode, ground=0 switches it off too, as the module reads it",
                ffb_FileModel()==1);
        WritePrivateProfileStringA("ffb","ffb_mode","7",ini);
        WritePrivateProfileStringA("ffb","ground",NULL,ini);
        ffb_LoadFrom(ini);
        STCheck("an ffb_mode the module ignores leaves it to ground - absent, so the road model",
                ffb_FileModel()==0&&!ffb_ModelWarning());
        WritePrivateProfileStringA("ffb","ffb_mode","0",ini);
        WritePrivateProfileStringA("ffb","ground","0",ini);
        ffb_LoadFrom(ini);
        STCheck("ffb_mode=0 is the road model whatever ground says - no warning",
                ffb_FileModel()==0&&!ffb_ModelWarning());
        {   char line[400]; line[0]=0;
            if(ffb_modeText) GetWindowTextA(ffb_modeText,line,sizeof line);
            STCheck("...and with no reason the red line is empty, hidden and takes no room",
                    !line[0]&&!STVisible(ffb_modeText)&&ffb_laidWarn==0); }
        DeleteFileA(ini);
        ffb_fileMode=-1; ffb_fileGround=1;

        /* ================= 2026-09-30: THE UTILITY OF THE 2026 ROAD MODEL =================
         * docs\UTILITY-CHANGES-v821.md B1-B5 with the owner's decisions. Every check reads the
         * FILE the page wrote or a control's own state, not only the variable behind it. */

        /* ---- his reference IS 100%: a file written at the tall marks holds his numbers ---- */
        {
            static const char *K[NGK]={"ground_sat_k","ground_caster_k","ground_opt_deg",
                "ground_damp_stand","ground_damp_move_pct","ground_damp_slip","ground_detail_k",
                "ground_detail_lim"};
            /* memory\the-approved-driving-feel.md, the table he confirmed on 2026-09-30 - written
               out here rather than read from GREF, or the check would be GREF agreeing with itself */
            static const int V[NGK]={7875,2400,9,6000,50,78,900,5000};
            ffb_ResetValues();
            ffb_SaveTo(ini,0);
            for(i=0;i<NGK;i++){
                wsprintfA(what,"at the reference the file holds his number: %s=%d (want %d)",
                          K[i],STFfb(ini,K[i]),V[i]);
                STCheck(what,STFfb(ini,K[i])==V[i]);
            }
            for(i=NPLAIN;i<NSLIDER;i++){
                wsprintfA(what,"...and %s sits on its tall mark, %d",SNAME[i],SREF[i]);
                STCheck(what,ffb_val[i]==SREF[i]);
            }
        }
        /* ---- every slider PROPORTIONAL, both keys of a pair together ---- */
        ffb_SetRow(S_GWEIGHT,150); ffb_SetRow(S_GBREAK,12); ffb_SetRow(S_GPARK,50);
        ffb_SetRow(S_GDRIVE,150);  ffb_SetRow(S_GSLIP,40);  ffb_SetRow(S_GTEXT,200);
        ffb_SaveTo(ini,0);
        STCheck("steering weight 150% scales the aligning torque AND the caster: 11813 / 3600",
                STFfb(ini,"ground_sat_k")==11813&&STFfb(ini,"ground_caster_k")==3600);
        STCheck("breakaway 12 degrees writes 12",STFfb(ini,"ground_opt_deg")==12);
        STCheck("parking damper 50% writes 3000 of 6000",STFfb(ini,"ground_damp_stand")==3000);
        STCheck("driving damper 150% writes 75% of the profile's (the reference is 50)",
                STFfb(ini,"ground_damp_move_pct")==75);
        STCheck("lightness in a slide writes the share itself",STFfb(ini,"ground_damp_slip")==40);
        STCheck("road texture 200% scales the gain AND its ceiling: 1800 / 10000",
                STFfb(ini,"ground_detail_k")==1800&&STFfb(ini,"ground_detail_lim")==10000);
        for(i=NPLAIN;i<NSLIDER;i++) ffb_val[i]=-1;
        ffb_LoadFrom(ini);
        STCheck("...and all six come back as they were set",
                ffb_val[S_GWEIGHT]==150&&ffb_val[S_GBREAK]==12&&ffb_val[S_GPARK]==50&&
                ffb_val[S_GDRIVE]==150&&ffb_val[S_GSLIP]==40&&ffb_val[S_GTEXT]==200);
        /* 0% MEANS NONE - and the module reads its own 0 as the OLD heavy damper */
        ffb_SetRow(S_GPARK,0);
        ffb_SaveTo(ini,0);
        STCheck("parking damper 0% is written as 1, not as the 0 the module reads as 20000",
                STFfb(ini,"ground_damp_stand")==1);
        ffb_LoadFrom(ini);
        STCheck("...and reads back as 0%",ffb_val[S_GPARK]==0);
        /* 400% OF HIS REFERENCE, key for key - the top of every road-model force row. The page
           writes the proportion; where the module clamps below it is docs\UTILITY-MODULE-NEEDS.md
           section 1, and the page still reads its own 400 back as 400. */
        ffb_SetRow(S_GWEIGHT,400); ffb_SetRow(S_GPARK,400); ffb_SetRow(S_GDRIVE,400);
        ffb_SetRow(S_GTEXT,400);
        ffb_SaveTo(ini,0);
        STCheck("steering weight 400% writes 31500 / 9600 - his 7875 and 2400 times four",
                STFfb(ini,"ground_sat_k")==31500&&STFfb(ini,"ground_caster_k")==9600);
        STCheck("road texture 400% writes 3600 / 20000",
                STFfb(ini,"ground_detail_k")==3600&&STFfb(ini,"ground_detail_lim")==20000);
        STCheck("parking damper 400% writes 24000, driving damper 400% writes 200",
                STFfb(ini,"ground_damp_stand")==24000&&STFfb(ini,"ground_damp_move_pct")==200);
        for(i=NPLAIN;i<NSLIDER;i++) ffb_val[i]=-1;
        ffb_LoadFrom(ini);
        STCheck("...and the page reads its own 400% back as 400% on all four",
                ffb_val[S_GWEIGHT]==400&&ffb_val[S_GPARK]==400&&ffb_val[S_GDRIVE]==400&&
                ffb_val[S_GTEXT]==400);
        ffb_SetRow(S_GWEIGHT,500);
        STCheck("the steering weight stops at 400",ffb_val[S_GWEIGHT]==400);
        ffb_SetRow(S_GWEIGHT,0);
        STCheck("...and goes all the way down to 0, both of its keys with it",
                ffb_val[S_GWEIGHT]==0&&ffb_gk[GK_SATK]==0&&ffb_gk[GK_CASTER]==0);
        ffb_SetRow(S_GBREAK,2);
        STCheck("the breakaway stops at 5 degrees",ffb_val[S_GBREAK]==5);
        ffb_SetRow(S_GBREAK,40);
        STCheck("...and at 15",ffb_val[S_GBREAK]==15);
        ffb_SetRow(S_GDRIVE,101);
        STCheck("an odd driving damper lands on a value its key can hold",ffb_val[S_GDRIVE]==102);
        STCheck("the breakaway slider snaps to nothing - ten degrees of travel",
                SliderMagnetise(&FFB_SLIDER_OPS,S_GBREAK,4)==4&&
                SliderMagnetise(&FFB_SLIDER_OPS,S_GBREAK,5)==5);
        /* 0..400 has a real zero now, so the weight snaps like every percent row: to its
           reference, to half the travel and to both ends, within 12 of each - nowhere else */
        STCheck("the steering weight snaps to 100, to 200, to 400 and to 0 - and not at 150",
                SliderMagnetise(&FFB_SLIDER_OPS,S_GWEIGHT,105)==100&&
                SliderMagnetise(&FFB_SLIDER_OPS,S_GWEIGHT,150)==150&&
                SliderMagnetise(&FFB_SLIDER_OPS,S_GWEIGHT,195)==200&&
                SliderMagnetise(&FFB_SLIDER_OPS,S_GWEIGHT,390)==400&&
                SliderMagnetise(&FFB_SLIDER_OPS,S_GWEIGHT,6)==0);

        /* A PAIR NOBODY MOVED KEEPS ITS SECOND KEY - a page may write only what it read */
        DeleteFileA(ini);
        WritePrivateProfileStringA("ffb","ground_sat_k","7875",ini);
        WritePrivateProfileStringA("ffb","ground_caster_k","3000",ini);
        WritePrivateProfileStringA("ffb","ground_detail_k","900",ini);
        WritePrivateProfileStringA("ffb","ground_detail_lim","4000",ini);
        ffb_LoadFrom(ini);
        ffb_SetRow(S_MASTER,90);
        ffb_SaveTo(ini,0);
        STCheck("a caster the page never moved survives a save: 3000",
                STFfb(ini,"ground_caster_k")==3000);
        STCheck("...and a texture ceiling: 4000",STFfb(ini,"ground_detail_lim")==4000);
        STCheck("...while the slider that did move wrote",STFfb(ini,"master")==90);
        ffb_SetRow(S_GWEIGHT,110);
        ffb_SaveTo(ini,0);
        STCheck("moving the weight rewrites BOTH keys from his reference: 8663 / 2640",
                STFfb(ini,"ground_sat_k")==8663&&STFfb(ini,"ground_caster_k")==2640);

        /* ---- WEIGHT BUILD-UP WITH SPEED: the three curves he drove, as tools\ground-keys.ps1
           holds them - preset7 = I, preset8 = O, preset6 = U. Copied from THAT file, not from the
           page's table, so the two are compared rather than one restated. ---- */
        {
            static const int I_[NSPD]={22,34,39,49,65,84,105,173};
            static const int O_[NSPD]={22,34,42,60,80,100,122,190};
            static const int U_[NSPD]={22,34,46,70,95,115,138,208};
            static const int KMH[NSPD]={10,20,30,50,60,70,80,110};
            const int *W[NCRV]={I_,O_,U_};
            int c,k,ok;
            for(c=0;c<NCRV;c++){
                ffb_SetCurve(c);
                ffb_SaveTo(ini,0);
                ok=1;
                for(k=0;k<NSPD;k++){
                    char kn[24];
                    wsprintfA(kn,"ground_spd%d_pct",k+1); if(STFfb(ini,kn)!=W[c][k]) ok=0;
                    wsprintfA(kn,"ground_spd%d_kmh",k+1); if(STFfb(ini,kn)!=KMH[k]) ok=0;
                }
                wsprintfA(what,"build-up %s writes the curve he drove, all eight points",
                          CRV_NAME[c]);
                STCheck(what,ok&&ffb_CurveNow()==c);
            }
            WritePrivateProfileStringA("ffb","ground_spd6_pct","99",ini);
            ffb_LoadFrom(ini);
            STCheck("a curve that is none of the three reads as the file's own",ffb_CurveNow()==-1);
            ffb_SetRow(S_MASTER,80);
            ffb_SaveTo(ini,0);
            STCheck("...and is kept as read by a save that did not touch it",
                    STFfb(ini,"ground_spd6_pct")==99);
            ffb_SetCurve(CRV_REF);
        }

        /* ---- THE DEVELOPERS' HOTKEYS: off in every file this page writes ---- */
        DeleteFileA(ini);
        ffb_ResetValues();
        ffb_SaveTo(ini,0);
        {
            int allOff=1;
            for(i=0;FFB_DEVKEYS[i];i++)
                if(!IniHas(ini,"ffb",FFB_DEVKEYS[i])||STFfb(ini,FFB_DEVKEYS[i])!=0) allOff=0;
            STCheck("a fresh file switches every developer hotkey off, all sixteen, by the "
                    "module's own names",allOff&&FFB_NDEVKEYS==16);
        }
        STCheck("...the ones he named among them",
                IniHas(ini,"ffb","road_roll_keys")&&IniHas(ini,"ffb","crash_keys")&&
                IniHas(ini,"ffb","ground_key_on")&&IniHas(ini,"ffb","ground_key_off")&&
                IniHas(ini,"ffb","impulse_key_on")&&IniHas(ini,"ffb","impulse_key_off")&&
                IniHas(ini,"ffb","preset1_key")&&IniHas(ini,"ffb","preset9_key"));
        STCheck("that file reads as released",ffb_DevKeysOff(ini));
        WritePrivateProfileStringA("ffb","preset6_key","85",ini);   /* U, as the bench arms it */
        STCheck("...and one live key makes it not",!ffb_DevKeysOff(ini));
        ffb_SaveTo(ini,0);
        STCheck("a bench key in a player's file is switched off by the next save",
                STFfb(ini,"preset6_key")==0);
        WritePrivateProfileStringA("ffb","dev_keys","1",ini);
        WritePrivateProfileStringA("ffb","preset6_key","85",ini);
        WritePrivateProfileStringA("ffb","road_roll_keys","1",ini);
        ffb_SaveTo(ini,0);
        STCheck("with dev_keys=1 the bench keeps its keys exactly as its file holds them",
                STFfb(ini,"preset6_key")==85&&STFfb(ini,"road_roll_keys")==1&&
                STFfb(ini,"dev_keys")==1);
        DeleteFileA(ini);

        /* ---- THE UNDO ARROW: back to each row's OWN reference ---- */
        ffb_ResetValues();
        ffb_SetRow(S_GUN,100);
        STCheck("gunfire at 100 shows its undo arrow - 100 is not its reference",
                STVisible(ffb_reset[S_GUN]));
        PageFfbCommand(FF_ID_RESET0+S_GUN,0,NULL);
        STCheck("...and the arrow puts it back to 0, not to 100, and goes away",
                ffb_val[S_GUN]==0&&!STVisible(ffb_reset[S_GUN]));
        ffb_SetRow(S_GBREAK,13);
        PageFfbCommand(FF_ID_RESET0+S_GBREAK,0,NULL);
        STCheck("the breakaway's arrow goes back to 9 degrees",ffb_val[S_GBREAK]==9);
        ffb_SetRow(S_GSLIP,10);
        PageFfbCommand(FF_ID_RESET0+S_GSLIP,0,NULL);
        STCheck("lightness in a slide goes back to 78",ffb_val[S_GSLIP]==78);
        ffb_SetRow(S_GWEIGHT,150);
        PageFfbCommand(FF_ID_RESET0+S_GWEIGHT,0,NULL);
        STCheck("the weight goes back to 100 and both its keys to his",
                ffb_val[S_GWEIGHT]==100&&ffb_gk[GK_SATK]==7875&&ffb_gk[GK_CASTER]==2400);

        /* ---- BACK TO DEFAULT: says what it does, does what it says ---- */
        STCheck("Back to default no longer claims 100% for everything, nor mentions a model",
                !StrHas(FFB_RECOMM_NOTE,"100%")&&StrHas(FFB_RECOMM_NOTE,"gunfire 0")&&
                StrHas(FFB_RECOMM_NOTE,"not touched")&&!StrHas(FFB_RECOMM_NOTE,"model"));
        {
            int was=ffb_range, ok=1;
            ffb_SetRow(S_GWEIGHT,150); ffb_SetRow(S_ROAD,360); ffb_SetRow(S_GUN,300);
            ffb_SetRow(S_GBREAK,14); ffb_SetRow(S_MASTER,400);
            ffb_SetCurve(CRV_HEAVY);
            ffb_range=900;
            ffb_Recommended();
            for(i=0;i<NSLIDER;i++) if(ffb_val[i]!=SREF[i]) ok=0;
            for(i=0;i<NGK;i++) if(ffb_gk[i]!=GREF[i]) ok=0;
            STCheck("Back to default puts every row on its tall mark and every key on his number",
                    ok);
            STCheck("...and the build-up on Reference",ffb_CurveNow()==CRV_REF);
            STCheck("...and leaves the range alone, as its note says",ffb_range==900);
            ffb_range=was;
        }

        /* ---- A PRESET SWITCHES NO MODEL, and it carries the road model's keys ----
           A preset file from the three-model days may still say ffb_mode=2. Picking it takes its
           VALUES; its model lines stay in the preset, carried, and never reach the file the mod
           reads. */
        {
            char pre[MAX_PATH],live[MAX_PATH],pk[MAX_PATH];
            int slotWas=ffb_slotSel,k;
            ffb_MkDirs();
            ffb_ProfilePath(pre,1);
            ffb_IniPath(live);
            DeleteFileA(pre); DeleteFileA(live);
            WritePrivateProfileStringA("ffb","ffb_mode","2",pre);
            WritePrivateProfileStringA("ffb","ground","0",pre);
            WritePrivateProfileStringA("ffb","master","55",pre);
            WritePrivateProfileStringA("ffb","ground_sat_k","9000",pre);
            ffb_SelectSlot(1);
            STCheck("picking a preset takes its values",
                    ffb_val[S_MASTER]==55&&ffb_gk[GK_SATK]==9000);
            STCheck("...and not its model: the file the mod reads gets no ffb_mode and no ground",
                    file_exists(live)&&!IniHas(live,"ffb","ffb_mode")&&
                    !IniHas(live,"ffb","ground"));
            STCheck("...while the preset keeps its own two lines exactly, carried",
                    STFfb(pre,"ffb_mode")==2&&STFfb(pre,"ground")==0);
            STCheck("the preset now carries the road model's keys",
                    STFfb(pre,"ground_sat_k")==9000&&IniHas(pre,"ffb","ground_damp_slip")&&
                    IniHas(pre,"ffb","ground_spd8_pct"));
            DeleteFileA(live);
            for(k=0;k<3;k++){ ffb_ProfilePath(pk,k); DeleteFileA(pk); }
            ffb_slotSel=slotWas;
            ffb_ResetValues();
        }

        /* ---- EVERY ROW IS ON THE PAGE, ONCE - asked of the layout and of the controls ---- */
        {
            int ok=1,side,k,old=0;
            char lbl[64];
            ffb_Layout();
            for(i=0;i<NSLIDER;i++){
                int n=0;
                for(side=0;side<FFB_NLAY;side++){
                    const ffb_lay *L=ffb_Lay(side);
                    for(k=0;L[k].kind!=LI_END;k++) if(L[k].kind==LI_ROW&&L[k].arg==i) n++;
                }
                if(n!=1||!STVisible(ffb_slider[i])||!STVisible(ffb_box[i])){
                    wsprintfA(what,"%s is laid out once and visible (laid out %d times)",
                              SNAME[i],n);
                    STCheck(what,0); ok=0;
                }
            }
            STCheck("all twelve rows are on the page, each exactly once",ok&&NSLIDER==12);
            STCheck("...and the build-up row with them",STVisible(ffb_curveBtn[CRV_REF]));
            GetWindowTextA(ffb_label[S_ROAD],lbl,sizeof lbl);
            STCheck("the road row is roll and curbs",SEq(lbl,"Roll and curbs"));
            for(i=0;i<NSLIDER;i++)
                if(SEq(SKEY[i],"spring")||SEq(SKEY[i],"sat")||SEq(SKEY[i],"damper_static")||
                   SEq(SKEY[i],"damper_moving")||SEq(SKEY[i],"truck_static")||
                   SEq(SKEY[i],"truck_moving")) old=1;
            STCheck("no row of the old feedback is left - spring, slide feel, dampers, trucks",
                    !old);
            /* THE 1.4.3 PAGE, 2026-10-01: "the difference should be almost none - a few sliders
               added, and that's it." Asked of the tables AND of where the controls landed. */
            STCheck("the two columns are 1.4.3's: FORCE FEEDBACK STRENGTH and WHEEL WEIGHT, each "
                    "heading centred in the section face over its own column",
                    SEq(FFB_TEXT[T_HEAD_L],"FORCE FEEDBACK STRENGTH")&&
                    SEq(FFB_TEXT[T_HEAD_R],"WHEEL WEIGHT")&&
                    (GetWindowLongA(ffb_text[T_HEAD_L],GWL_STYLE)&SS_CENTER)&&
                    (GetWindowLongA(ffb_text[T_HEAD_R],GWL_STYLE)&SS_CENTER)&&
                    (HFONT)SendMessageA(ffb_text[T_HEAD_L],WM_GETFONT,0,0)==g_fSection&&
                    (HFONT)SendMessageA(ffb_text[T_HEAD_R],WM_GETFONT,0,0)==g_fSection&&
                    STLeft(ffb_text[T_HEAD_L])==COL_LABEL&&STLeft(ffb_text[T_HEAD_R])==COL_MID);
            STCheck("the strength rows are on the left and the wheel-weight rows on the right",
                    STLeft(ffb_slider[S_CRASH])==COL_SLIDER&&STLeft(ffb_slider[S_GTEXT])==COL_SLIDER&&
                    STLeft(ffb_slider[S_GWEIGHT])==COL_MID+WW_SL_X&&
                    STLeft(ffb_slider[S_GSLIP])==COL_MID+WW_SL_X);
            /* HIS QUESTIONS OF 2026-10-01: "should it not be above everything?", then "if overall
               strength also affects the steering, the steering has to sit under it - now it looks as
               if it only acts on the left column". A row of its own above BOTH columns, asked of the
               tables and of where the controls landed: above both headings, the steering included. */
            STCheck("Total effects has a row of its own above both columns, in neither of them",
                    ffb_LayHas(ffb_Lay(2),LI_ROW,S_MASTER)&&
                    !ffb_LayHas(ffb_Lay(0),LI_ROW,S_MASTER)&&!ffb_LayHas(ffb_Lay(1),LI_ROW,S_MASTER)&&
                    STTop(ffb_slider[S_MASTER])<STTop(ffb_text[T_HEAD_L])&&
                    STTop(ffb_slider[S_MASTER])<STTop(ffb_text[T_HEAD_R])&&
                    STTop(ffb_slider[S_MASTER])<STTop(ffb_slider[S_GWEIGHT]));
            /* "put it in the centre, so it is clear it belongs to both columns" - the group from the
               label's left edge to the hint's right edge has as much page on either side */
            {   int l=STLeft(ffb_label[S_MASTER])-COL_LABEL;
                int r=(COL_LABEL+W_FULL)-(STLeft(ffb_hint[S_MASTER])+ffb_HintW(S_MASTER));
                wsprintfA(what,"...centred on the page: %d px of page on its left, %d on its right",l,r);
                STCheck(what,l-r<=1&&r-l<=1&&l>100);
            }
            /* his rename of 2026-10-01: the name itself says what the row leaves out */
            STCheck("...named Total effects (not affecting dampers), its hint naming the steering "
                    "among what it scales",
                    SEq(SNAME[S_MASTER],"Total effects (not affecting dampers)")&&
                    StrHas(SHINT[S_MASTER],"steering"));
            /* "it does not read as anything to do with gunfire" - so it is placed right under the
               gunfire row, from that row's own edge */
            STCheck("the gunfire note sits right under the gunfire row",
                    STLeft(ffb_noteGun)==STLeft(ffb_label[S_GUN])&&
                    STTop(ffb_noteGun)>STTop(ffb_slider[S_GUN])&&
                    STTop(ffb_noteGun)-STTop(ffb_slider[S_GUN])<=R_H+2&&
                    StrHas(FFB_NOTE_GUN,"Gunfire"));
            STCheck("the wheel-weight table has its two halves and the seam between them, 1.4.3's "
                    "Cars and Trucks style - the Damper half saying Overall does not scale it",
                    StrHas(FFB_TEXT[T_SUB_STEER],"Steering")&&
                    StrHas(FFB_TEXT[T_SUB_STEER],"build-up is how the weight grows with speed")&&
                    StrHas(FFB_TEXT[T_SUB_DAMP],"Damper")&&
                    StrHas(FFB_TEXT[T_SUB_DAMP],"not scaled by Total effects")&&
                    STTop(ffb_seam)>STTop(ffb_slider[S_GBREAK])&&
                    STTop(ffb_seam)<STTop(ffb_text[T_SUB_DAMP])&&
                    STTop(ffb_text[T_SUB_DAMP])<STTop(ffb_slider[S_GPARK]));
        }

        /* ---- THE RANGE, 1.4.3'S AGAIN - 2026-10-01 ----
           His words, translated: "by the law looks as silly as it gets... remove measured and by
           the law; label 600 as recommended and leave the others as they are." */
        {
            RECT b; int k,one=1;
            HWND page=g_page[LTAB_FFB];
            for(k=0;k<NDOR;k++){
                if(!ffb_dorBtn[k]) { one=0; continue; }
                GetWindowRect(ffb_dorBtn[k],&b);
                if(b.bottom-b.top!=30) one=0;
            }
            STCheck("every range button is one line, 30 px, as in 1.4.3",one&&DOR_BTN_H==30);
            /* "in the previous version the description itself made it clear that 600 is the
               default" - so the description says it, and no line under the buttons repeats it */
            STCheck("the line beside the heading is 1.4.3's, and names 600 as the default and "
                    "recommended",
                    StrHas(FFB_DOR_TEXT,"what your wheel's own driver is set to")&&
                    StrHas(FFB_DOR_TEXT,"600 is the default and recommended"));
            STCheck("no line under the range buttons - none of the ids it ever had exists",
                    page&&!GetDlgItem(page,5350)&&!GetDlgItem(page,5503));
        }

        /* ---- THE MODEL BLOCK IS GONE, 2026-10-01, and nothing on the page offers a model ----
           Translated: "this block is not needed after all: there are no other options, and it only
           makes the utility taller." Asked of the control ids, so a block that comes back under
           its old ids says so here. */
        {
            HWND page=g_page[LTAB_FFB];
            STCheck("no row names the model any more - its name, NEW plate and label are gone",
                    page&&!GetDlgItem(page,5435)&&!GetDlgItem(page,FF_ID_SECT0+5));
            /* by the WORDS on the page, not only the ids: a label can come back under a new id */
            STCheck("no text on the page names the model, calls it new, or says MEASURED or BY THE "
                    "LAW",
                    page&&!STPageHas(page,"road model 2026")&&!STPageHas(page,"NEW ROAD MODEL")&&
                    !STPageHas(page,"FEEDBACK MODEL")&&!STPageHas(page,"MEASURED")&&
                    !STPageHas(page,"BY THE LAW")&&!STPageHas(page,"by the law"));
            STCheck("the three-model selector is gone - none of its controls exists",
                    page&&!GetDlgItem(page,5430)&&!GetDlgItem(page,5431)&&
                    !GetDlgItem(page,5432)&&!GetDlgItem(page,5433));
        }

        /* ---- THE LAMP SAYS WHICH MODULE IS TALKING ---- */
        {
            char t[300];
            ffb_LampText(t,1,"TEST WHEEL","822",FFBV_NEW,0,1);
            STCheck("a status file carrying version=822 puts it on the lamp",StrHas(t,"v822"));
            ffb_LampText(t,1,"TEST WHEEL","",FFBV_OLD,1,1);
            STCheck("the 1.4.3 module, which writes no version, is named from the folder",
                    StrHas(t,"1.4.3"));
            ffb_LampText(t,1,"TEST WHEEL","",FFBV_OLD,0,1);
            STCheck("...and any other earlier build of ours as one",StrHas(t,"earlier build"));
            ffb_LampText(t,1,"TEST WHEEL","",FFBV_NEW,0,1);
            STCheck("this version's module with no version line adds nothing to the lamp",
                    SEq(t,"Driving effects on TEST WHEEL"));
            ffb_LampText(t,3,"","",FFBV_NONE,0,1);
            STCheck("never run with the mod on: the lamp says to start Mafia once, and fits",
                    StrHas(t,"start Mafia once")&&STFits(g_fBody,t,W_FULL-28));
            ffb_LampText(t,3,"","",FFBV_NONE,0,0);
            STCheck("...and with the mod off, to switch it on first, and fits",
                    StrHas(t,"switch the mod on")&&STFits(g_fBody,t,W_FULL-28));
        }

        /* ---- EVERY STRING FITS ITS BOX, in the face it is drawn in ---- */
        {
            static const int V[]={0,1,5,9,15,25,50,78,100,101,150,200,300,400,600};
            int bad=0,k;
            const char *s;
            for(i=0;i<NSLIDER;i++){
                const ffb_geo *g=ffb_GeoOf(i);
                if(!STFits(g_fBody,SNAME[i],g->lblW)){
                    wsprintfA(what,"label '%s' fits %d px",SNAME[i],g->lblW); STCheck(what,0); bad++; }
                if(!STFits(g_fField,SUNIT[i],g->unW)){
                    wsprintfA(what,"unit '%s' fits %d px",SUNIT[i],g->unW); STCheck(what,0); bad++; }
                for(k=0;k<(int)(sizeof V/sizeof V[0]);k++){
                    s=ffb_HintFor(i,V[k]);
                    if(!STFits(g_fSmall,s,g->hnW)){
                        wsprintfA(what,"hint '%s' fits %d px",s,g->hnW); STCheck(what,0); bad++; }
                }
            }
            STCheck("every row label, unit and hint fits its box",bad==0);
            STCheck("the build-up row's label fits",STFits(g_fBody,"Build-up with speed",WW_LBL_W));
            bad=0;
            for(i=0;i<NCRV;i++) if(!STFits(g_fField,CRV_NAME[i],CRV_W-10)) bad++;
            STCheck("the three curve names fit their buttons",bad==0);
            STCheck("the kept-curve line fits beside them",
                    STFits(g_fSmall,FFB_CURVE_KEPT,CRV_HINT_W));
            STCheck("the two column headings fit their columns",
                    STFits(g_fSection,FFB_TEXT[T_HEAD_L],W_LEFT)&&
                    STFits(g_fSection,FFB_TEXT[T_HEAD_R],W_RIGHT));
            STCheck("the line under the strength heading fits its column",
                    STFits(g_fBody,FFB_TEXT[T_LINE_L],W_LEFT));
            STCheck("the table's two small headings fit their column",
                    STFits(g_fField,FFB_TEXT[T_SUB_STEER],W_RIGHT)&&
                    STFits(g_fField,FFB_TEXT[T_SUB_DAMP],W_RIGHT));
            STCheck("the gunfire note fits one line of its column",
                    STFits(g_fSmall,FFB_NOTE_GUN,W_LEFT));
            STCheck("the Back to default note fits one line",
                    STFits(g_fSmall,FFB_RECOMM_NOTE,W_FULL));
            STCheck("the line beside the range heading fits",
                    STFits(g_fBody,FFB_DOR_TEXT,W_FULL-280));
            /* WHICH MODULE THE FOLDER HOLDS - an upgrade check, not a model. Every sentence the
               red line above the sliders can say must fit its two full-width lines. */
            {   int wasV=ffb_variant,was143=ffb_variant143,wasS=g_state[LTAB_FFB],
                    wasM=ffb_fileMode,wasG=ffb_fileGround,lineW=W_FULL*2-120;
                STCheck("this version's payload reads as this version's module",
                        ffb_VariantOfMd5(PAYLOAD_ASI_MD5)==FFBV_NEW);
                STCheck("the 1.4.3 module (c56669c4) reads as an earlier build of ours",
                        ffb_VariantOfMd5(ASI_RELEASE_143_MD5)==FFBV_OLD&&
                        is_our_asi(ASI_RELEASE_143_MD5));
                STCheck("an md5 we never shipped reads as somebody else's",
                        ffb_VariantOfMd5("0123456789abcdef0123456789abcdef")==FFBV_OTHER);
                g_state[LTAB_FFB]=JMOD_ON; ffb_fileMode=-1; ffb_fileGround=1;
                ffb_variant=FFBV_NEW; ffb_variant143=0;
                STCheck("this version's module in the folder: no warning, no red line",
                        !ffb_ModelWarning());
                ffb_variant=FFBV_OLD; ffb_variant143=1;
                STCheck("an upgrade from 1.4.3 is seen: its module still in the folder, said in "
                        "red, and it fits the two lines",
                        ffb_ModelWarning()&&StrHas(ffb_ModelWarning(),"release 1.4.3")&&
                        StrHas(ffb_ModelWarning(),"switch the mod off and on")&&
                        STFits(g_fSmall,ffb_ModelWarning(),lineW));
                ffb_variant143=0;
                STCheck("...and any earlier build of ours, without the number",
                        ffb_ModelWarning()&&StrHas(ffb_ModelWarning(),"earlier build")&&
                        STFits(g_fSmall,ffb_ModelWarning(),lineW));
                g_state[LTAB_FFB]=JMOD_ABSENT;
                STCheck("with the mod off an old module is no warning - the toggle installs ours",
                        !ffb_ModelWarning());
                g_state[LTAB_FFB]=JMOD_ON; ffb_variant=FFBV_OTHER;
                STCheck("a module we never shipped (a bench build) is left alone, called nothing",
                        !ffb_ModelWarning());
                ffb_fileMode=1;
                STCheck("the file-switches-the-model-off warning fits the two lines",
                        ffb_ModelWarning()&&STFits(g_fSmall,ffb_ModelWarning(),lineW));
                ffb_fileMode=2;
                STCheck("...and the developers'-feedback one",
                        ffb_ModelWarning()&&STFits(g_fSmall,ffb_ModelWarning(),lineW));
                ffb_variant=wasV; ffb_variant143=was143; g_state[LTAB_FFB]=wasS;
                ffb_fileMode=wasM; ffb_fileGround=wasG;
            }
        }

        /* ---- THE PAGE IS SHORTER, AND A LINE TAKES ROOM ONLY WHILE IT HAS SOMETHING TO SAY ----
           His complaint of 2026-10-01, translated: "we worked hard to make it smaller, and now it
           has stretched vertically again" - the same one as 2026-08-05. The height is PRINTED, so
           every report shows the number, and FFB_PAGE_MAX is the height this layout measured with
           nothing to warn about: a page that grows back fails here first. */
        {   int k,tallest=0,h0,t0,wasV=ffb_variant,wasM=ffb_fileMode,wasG=ffb_fileGround;
            for(k=0;k<NOLD;k++) ffb_old[k]=100;
            ffb_fileMode=-1; ffb_fileGround=1; ffb_variant=FFBV_NONE;
            ffb_RefreshNotes(); ffb_Layout();
            for(k=0;k<LNTAB;k++) if(g_contentH[k]>tallest) tallest=g_contentH[k];
            wsprintfA(what,"the Force Feedback page is %d px tall with nothing to warn about, at "
                      "most %d (H-shifter %d, first person %d; the window asks for %d of client)",
                      ffb_pageH,FFB_PAGE_MAX,g_contentH[LTAB_SHIFTER],g_contentH[LTAB_FPV],
                      TITLEBAND+HEADH+tallest+LOGH);
            STCheck(what,ffb_pageH<=FFB_PAGE_MAX&&!ffb_laidWarn&&!ffb_laidOld);
            /* THE WINDOW IS AS TALL AS ITS TALLEST TAB (SizeToContent), so a short Force feedback
               page buys nothing while another tab stays tall - which is how "why so much empty
               space under the presets?" came about on 2026-10-01: First person was 724 px against
               this page's 680. No tab may be more than a few pixels taller than this one. */
            wsprintfA(what,"no tab is more than 8 px taller than this page (tallest %d, this %d) - "
                      "or this page shows that much empty paper under its presets",tallest,ffb_pageH);
            STCheck(what,tallest<=ffb_pageH+8);
            h0=ffb_pageH; t0=STTop(ffb_slider[S_MASTER]);
            ffb_fileMode=2; ffb_RefreshNotes();
            STCheck("a reason appearing gives the red line its room - the page grows by 40 px and "
                    "everything under it moves down",
                    ffb_pageH==h0+40&&STTop(ffb_slider[S_MASTER])==t0+40&&STVisible(ffb_modeText));
            ffb_fileMode=-1; ffb_RefreshNotes();
            STCheck("...and gives it back the moment the reason goes",
                    ffb_pageH==h0&&STTop(ffb_slider[S_MASTER])==t0&&!STVisible(ffb_modeText));
            ffb_old[0]=150; ffb_RefreshNotes();
            STCheck("the earlier-version note takes a line only while it has one to say",
                    ffb_pageH==h0+22&&STVisible(ffb_noteOld));
            ffb_old[0]=100; ffb_RefreshNotes();
            STCheck("...and none once it has nothing",ffb_pageH==h0&&!STVisible(ffb_noteOld));
            ffb_variant=wasV; ffb_fileMode=wasM; ffb_fileGround=wasG;
            ffb_RefreshNotes();
        }
        ffb_ResetValues();
        DeleteFileA(ini);
    }

    /* ---- WHICH MODULE THE FOLDER HOLDS, on a real install - 2026-09-30 ------------------------
     * Legacy is gone from the utility, and with it the 1.4.3 payload and the swap. What stays is
     * the UPGRADE check: a folder that still holds an earlier module of ours is said, in red, and
     * the page swaps nothing. The OLD case is proven above through the md5 and the page's state -
     * a file with the 1.4.3 md5 cannot be made here without shipping it - so this drives the two
     * ends a real folder can reach: our own module after the toggle, and a bench build left
     * alone. Driven through the real toggle, on the scratch folder. */
    {
        char asi[MAX_PATH],m[33],live[MAX_PATH],pk[MAX_PATH];
        const unsigned char *pl; size_t n=0; int k;
        SCpy(asi,scr); SCat(asi,"\\mafia_ffb.asi");
        ffb_IniPath(live);
        STCheck("the 1.4.3 module is no longer inside the utility - resource 107 is gone",
                payload(107,&n)==NULL);
        DeleteFileA(live);
        g_pending[LTAB_FFB]=0;      /* so only the toggle can ask for the write below */
        STCheck("switching FFB on installs this version's module",
                ToggleOn(LTAB_FFB)==1&&file_md5(asi,m)&&SEq(m,PAYLOAD_ASI_MD5));
        STCheck("...which the page reads as this version's - no red line over the sliders",
                ffb_variant==FFBV_NEW&&!ffb_ModelWarning());
        STCheck("...and the toggle itself asks for the settings file to be written",
                g_pending[LTAB_FFB]!=0);
        STFlushFfb();
        STCheck("...which then switches the developers' hotkeys OFF",
                file_exists(live)&&ffb_DevKeysOff(live)&&!ffb_DevKeysKept(live));
        STCheck("...and names no model",
                !IniHas(live,"ffb","ffb_mode")&&!IniHas(live,"ffb","ground"));
        {   /* a bench build in the folder: not ours, called nothing, and not replaced */
            static const char J[]="not a module this build ships";
            write_file(asi,J,sizeof J);
            ffb_RefreshVariant();
            STCheck("a module we never shipped reads as somebody else's, with no warning",
                    ffb_variant==FFBV_OTHER&&!ffb_ModelWarning()&&
                    file_md5(asi,m)&&!SEq(m,PAYLOAD_ASI_MD5));
            /* ours back, so the switch-off below takes it out by its own journal line */
            pl=payload(RES_ASI,&n);
            STCheck("our own module can be put back for the switch-off",
                    pl&&write_file(asi,pl,n)&&file_md5(asi,m)&&SEq(m,PAYLOAD_ASI_MD5));
            ffb_RefreshVariant();
        }
        STCheck("switching the mod off takes it out",
                ToggleOff(LTAB_FFB)==1&&!file_exists(asi)&&
                JModState(scr,J_MOD_FFB)==JMOD_ABSENT);
        DeleteFileA(live);
        for(k=0;k<3;k++){ ffb_ProfilePath(pk,k); DeleteFileA(pk); }
    }

    /* Clean up before the round-trip check: the ini is ours to remove, and it was never
       journalled - the toggle did not write it, this test did. */
    SCpy(p,scr); SCat(p,"\\" GEARBOX_DIR "\\gearbox.ini");
    DeleteFileA(p);
    SCpy(p,scr); SCat(p,"\\" GEARBOX_DIR);
    RemoveDirectoryA(p);
    /* The FFB folders too, and for the same reason: the checks above called ffb_MkDirs to prove
       where it puts them, so they are this test's litter and not the installer's. Deepest
       first - RemoveDirectory will not touch a folder with anything in it. */
    SCpy(p,scr); SCat(p,"\\" FFBDIR "\\profiles"); RemoveDirectoryA(p);
    SCpy(p,scr); SCat(p,"\\" FFBDIR);              RemoveDirectoryA(p);
    /* And its parent, which is new since 2026-08-07: the gearbox folder hangs under
       "ALXG mods\" now, so cleaning up only the leaf leaves the round-trip check looking at a
       folder this test made rather than one the installer left behind. RemoveDirectory fails
       on a non-empty folder, which is exactly the safety wanted - if the installer HAS left
       something in there, the check below still fails and says so. */
    SCpy(p,scr); SCat(p,"\\" ALXG_DIR);
    RemoveDirectoryA(p);

    /* The whole point of the round trip: the folder is back. */
    SCpy(p,scr); SCat(p,"\\Game.exe");
    file_md5(p,after);
    STCheck("Game.exe is byte for byte what it was",SEq(before,after));
    SCpy(p,scr); SCat(p,"\\dinput8.dll");
    STCheck("the loader is gone with the last mod",!file_exists(p));
    SCpy(p,scr); SCat(p,"\\ALXG mods");
    STCheck("and our own folder is gone too",!file_exists_dir(p));

    /* ---- THE RENAME MIGRATION, 2026-08-12 ---------------------------------------------------
     * The product became "ALXGtech Mafia 1 Mods" and the risk was never the new name - it was an
     * uninstall that no longer recognises what the PREVIOUS version installed. A machine that ran
     * 1.1.1 has "ALXG Mafia Mods.log" sitting in the game folder, and a delete list that only
     * knows the current name leaves it there with nothing left that would ever remove it.
     *
     * So the old names are planted here deliberately and the uninstall is asked to take them.
     * This is the re-injection rule applied to a migration: put the fault back and prove the fix
     * still catches it, rather than trusting a list I have just edited. */
    { char dir[MAX_PATH], oldLog[MAX_PATH], newLog[MAX_PATH], old1[MAX_PATH];
      HANDLE h;
      SCpy(dir,scr); SCat(dir,"\\");
      SCpy(oldLog,dir); SCat(oldLog,"ALXG Mafia Mods.log");
      SCpy(old1,  dir); SCat(old1,  "ALXG Mafia Mods.log.1");
      SCpy(newLog,dir); SCat(newLog,BRAND_NAME ".log");

      /* CASE 1: only the old file exists - it must be TAKEN OVER, not left and not lost. */
      DeleteFileA(newLog);
      h=CreateFileA(oldLog,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
      STCheck("a log under the pre-rename name can be planted",h!=INVALID_HANDLE_VALUE);
      if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);
      LogMigrateOldName(dir);
      STCheck("the old log is gone after the migration",!file_exists(oldLog));
      STCheck("...and its history survives under the new name",file_exists(newLog));

      /* CASE 2: BOTH exist - the live file wins and the dead one is removed, or the folder keeps
         a file nothing will ever write to or delete again. */
      h=CreateFileA(oldLog,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
      if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);
      h=CreateFileA(old1,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
      if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);
      LogMigrateOldName(dir);
      STCheck("with both present the old one is removed",!file_exists(oldLog));
      STCheck("the rotated old log goes too",!file_exists(old1));
      STCheck("the current log is untouched",file_exists(newLog));
      DeleteFileA(newLog); }

    fprintf(g_st,g_stFails?"\nFAILED\n":"\nall green\n");
    fclose(g_st);
    /* the log is ours and it is OPEN - a file with a handle on it does not delete, and the
       leftover is what the next run reports as a folder that is not fresh */
    if(g_logFile){ fclose(g_logFile); g_logFile=NULL; }
    WipeDir(scr);
    return g_stFails?1:0;
}

static int WantsSelfTest(const char *cmd,char *stock,size_t n){
    int i=0,j=0;
    for(;cmd[i];i++){
        if(cmd[i]=='-'&&cmd[i+1]=='-'&&cmd[i+2]=='s'&&cmd[i+3]=='e'&&cmd[i+4]=='l'
                      &&cmd[i+5]=='f'&&cmd[i+6]=='t'&&cmd[i+7]=='e'&&cmd[i+8]=='s'
                      &&cmd[i+9]=='t'){
            i+=10;
            while(cmd[i]==' '||cmd[i]=='"') i++;
            while(cmd[i]&&cmd[i]!='"'&&j<(int)n-1) stock[j++]=cmd[i++];
            while(j>0&&stock[j-1]==' ') j--;
            stock[j]=0;
            return 1;
        }
    }
    return 0;
}

/* --tab N opens on that tab. It exists so a tab can be PHOTOGRAPHED without driving the mouse:
   the four defects the FFB tuner shipped with were all found by looking at a picture, and
   sending synthetic clicks into a window is both fragile and a way to steal focus from a game
   test running next door. Out of range is ignored rather than refused - it is a convenience. */
static int WantedTab(const char *cmd){
    int i=0,n=0,got=0;
    for(;cmd[i];i++){
        if(cmd[i]=='-'&&cmd[i+1]=='-'&&cmd[i+2]=='t'&&cmd[i+3]=='a'&&cmd[i+4]=='b'){
            i+=5;
            while(cmd[i]==' '||cmd[i]=='=') i++;
            while(cmd[i]>='0'&&cmd[i]<='9'){ n=n*10+(cmd[i]-'0'); i++; got=1; }
            break;
        }
    }
    return (got&&n>=0&&n<LNTAB)?n:0;
}

/* ---- --shot <file.bmp>: PHOTOGRAPH A TAB WITHOUT PUTTING A WINDOW ON THE SCREEN ---------------
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * `--tab N` has existed since the FFB tuner's four defects were found by looking at pictures, and
 * its own comment says it is there so a tab can be photographed. The other half was missing: there
 * was no way to TAKE the picture except by opening the window on the shared screen and grabbing it,
 * which a bench run next door cannot survive and which the routine guard refuses outright.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * So the window is built HIDDEN and asked to draw itself with PrintWindow - the same method
 * `tools\grab-screen.ps1` uses, and the one this project already recorded as the one that works for
 * a GUI. Nothing is ever fronted, no focus moves, and it is safe while a game test is running.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * BMP because it needs no library: a file header, an info header and the pixels straight out of
 * GetDIBits. Convert it afterwards with ffmpeg if a PNG is wanted.
 */
static HDC  g_shotDC=NULL;
static RECT g_shotFrame;

/* One descendant, drawn at its own place inside the frame. WM_PRINT is sent as well as
   PrintWindow: an owner-drawn control answers WM_PRINTCLIENT, and the two together cover both
   kinds. Failures are ignored on purpose - a control that will not print leaves its patch of the
   picture as it was, which is visible, rather than aborting the whole shot. */
static BOOL CALLBACK ShotChild(HWND w,LPARAM lp)
{
    RECT cr; POINT org;
    (void)lp;
    if(!g_shotDC||!IsWindowVisible(w)) return TRUE;
    GetWindowRect(w,&cr);
    SetViewportOrgEx(g_shotDC,cr.left-g_shotFrame.left,cr.top-g_shotFrame.top,&org);
    if(!PrintWindow(w,g_shotDC,PW_CLIENTONLY))
        SendMessageA(w,WM_PRINT,(WPARAM)g_shotDC,
                     PRF_CLIENT|PRF_CHILDREN|PRF_NONCLIENT|PRF_ERASEBKGND);
    SetViewportOrgEx(g_shotDC,org.x,org.y,NULL);
    return TRUE;
}

static int ShotWindow(const char *path)
{
    RECT rc; int w,h,ok=0,spin;
    MSG msg;
    HDC screen,mem; HBITMAP bmp,old; BITMAPINFO bi; void *bits=NULL;
    if(!g_frame) return 1;

    /* A HIDDEN window has nothing to draw: the first attempt at this produced 4.9 MB of pure
       black. It has to be SHOWN for its children to paint - so it is shown OFF THE SCREEN, at
       -32000, with SWP_NOACTIVATE. Nothing appears where a human or a game test could see it,
       no focus moves, and the window paints itself normally.
       Then the message queue is drained: WM_PAINT arrives through the queue, and a PrintWindow
       taken before that is a photograph of an unpainted window. */
    /* ON the screen, at 0,0, but NEVER activated. Off-screen at -32000 was tried first and the
       controls refused to paint - three shots of an empty white page. Windows will not render a
       window it considers invisible, whatever PrintWindow is asked for.
       SWP_NOACTIVATE means the focus does not move, so a game keeps its input; the window is up
       for well under a second and the caller is expected to have checked the bench lock first.
       This is a developer switch: no player ever passes --shot. */
    SetWindowPos(g_frame,HWND_BOTTOM,0,0,0,0,
                 SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    UpdateWindow(g_frame);
    for(spin=0;spin<200;spin++){
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        Sleep(2);
    }
    UpdateWindow(g_frame);

    GetWindowRect(g_frame,&rc);
    w=rc.right-rc.left; h=rc.bottom-rc.top;
    if(w<=0||h<=0||w>16384||h>16384) return 1;

    screen=GetDC(NULL);
    mem=CreateCompatibleDC(screen);
    ZeroMemory(&bi,sizeof bi);
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w;
    bi.bmiHeader.biHeight=-h;              /* top-down, so the rows come out the right way up */
    bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,NULL,0);
    if(bmp&&bits){
        old=(HBITMAP)SelectObject(mem,bmp);
        /* PrintWindow on the FRAME alone gave a window with a title bar and a blank white body:
           the owner-drawn children did not paint into it. So each child is asked to print
           itself, at its own offset inside the frame - the viewport origin is moved so the child
           draws where it actually sits. This is the whole reason the first two attempts produced
           a picture that proved nothing. */
        if(PrintWindow(g_frame,mem,2)||PrintWindow(g_frame,mem,0)){
            /* EVERY DESCENDANT, not just the direct children: the page is a container window and
               the controls live inside it, so a one-level walk photographs an empty page. */
            g_shotDC=mem; GetWindowRect(g_frame,&g_shotFrame);
            EnumChildWindows(g_frame,ShotChild,0);
            g_shotDC=NULL;
            FILE *f=fopen(path,"wb");
            if(f){
                unsigned int rowBytes=(unsigned int)w*4u, dataBytes=rowBytes*(unsigned int)h;
                unsigned char fh[14]; unsigned char ih[40]; unsigned int off=14+40;
                unsigned int total=off+dataBytes; int i;
                for(i=0;i<14;i++) fh[i]=0;
                fh[0]='B'; fh[1]='M';
                fh[2]=(unsigned char)(total); fh[3]=(unsigned char)(total>>8);
                fh[4]=(unsigned char)(total>>16); fh[5]=(unsigned char)(total>>24);
                fh[10]=(unsigned char)(off); fh[11]=(unsigned char)(off>>8);
                for(i=0;i<40;i++) ih[i]=0;
                ih[0]=40;
                ih[4]=(unsigned char)(w); ih[5]=(unsigned char)(w>>8);
                ih[6]=(unsigned char)(w>>16); ih[7]=(unsigned char)(w>>24);
                { int nh=-h;
                  ih[8]=(unsigned char)(nh); ih[9]=(unsigned char)(nh>>8);
                  ih[10]=(unsigned char)(nh>>16); ih[11]=(unsigned char)(nh>>24); }
                ih[12]=1; ih[14]=32;
                ih[20]=(unsigned char)(dataBytes); ih[21]=(unsigned char)(dataBytes>>8);
                ih[22]=(unsigned char)(dataBytes>>16); ih[23]=(unsigned char)(dataBytes>>24);
                fwrite(fh,1,14,f); fwrite(ih,1,40,f);
                fwrite(bits,1,dataBytes,f);
                fclose(f);
                ok=1;
            }
        }
        SelectObject(mem,old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(NULL,screen);
    return ok?0:1;
}

/* --dir "<game folder>" - photograph the window as it looks POINTED AT A REAL INSTALL.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Alex, 2026-08-14, after the wide-screen switch shipped with its hint text clipped: *"we need a
 * GUI that shows the real interface rather than a fake one - one you could actually click through
 * in its different states: switch it on, switch it off. And that way you would see that the text
 * is being cut off there and something is left unfinished."*
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Without this, `--shot` photographs the folder the exe happens to sit in - `build\`, which holds
 * no Game.exe - so every control is disabled and every picture shows the same dead page. The
 * defects that only appear in the LIVE state are then invisible by construction. With it, the
 * caller sets the ini to the state it wants and takes the picture of the real thing.
 * Settled 2026-08-01: a user should be able to send this file in, and it should be readable enough
 * Quote the path if it has spaces, and pass --dir BEFORE --shot: an unquoted --shot swallows the
 * rest of the line by design.
 */
static int WantedDir(const char *cmd,char *out,size_t n)
{
    int i=0,j=0;
    for(;cmd[i];i++){
        if(cmd[i]=='-'&&cmd[i+1]=='-'&&cmd[i+2]=='d'&&cmd[i+3]=='i'&&cmd[i+4]=='r'){
            i+=5;
            while(cmd[i]==' '||cmd[i]=='='||cmd[i]=='"') i++;
            while(cmd[i]&&cmd[i]!='"'&&j<(int)n-1){
                if(cmd[i]==' '&&cmd[i+1]=='-'&&cmd[i+2]=='-') break;
                out[j++]=cmd[i++];
            }
            while(j>0&&out[j-1]==' ') j--;
            out[j]=0;
            return j>0;
        }
    }
    return 0;
}

/* --shot <path>, parsed the same way --selftest is. */
static int WantsShot(const char *cmd,char *out,size_t n)
{
    int i=0,j=0;
    for(;cmd[i];i++){
        if(cmd[i]=='-'&&cmd[i+1]=='-'&&cmd[i+2]=='s'&&cmd[i+3]=='h'&&cmd[i+4]=='o'&&cmd[i+5]=='t'){
            i+=6;
            while(cmd[i]==' '||cmd[i]=='"'||cmd[i]=='=') i++;
            while(cmd[i]&&cmd[i]!='"'&&j<(int)n-1) out[j++]=cmd[i++];
            while(j>0&&out[j-1]==' ') j--;
            out[j]=0;
            return j>0;
        }
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR cmd,int show)
{
    MSG msg;
    (void)hp; (void)show;

    char stock[MAX_PATH];

    /* Before anything else can load a DLL on our behalf - a common dialog pulling in a shell
       extension is the usual way this happens. It cannot help a STATIC import (those are bound
       before this line runs), which is why the DirectInput calls stay dynamic and go through
       LoadSystemDll. */
    HardenDllSearch();

    SkinInit();
    RegisterClasses(hi);
    ExeDir();

    if(cmd&&WantsSelfTest(cmd,stock,sizeof stock)) return SelfTest(stock);

    /* A folder the user chose last time wins over the one we are sitting in - but only while it
       still holds a Game.exe, so a moved or deleted install falls back silently. */
    { char saved[MAX_PATH];
      if(LoadGameDir(saved,sizeof saved)){
          SCpy(g_gameDir,saved);
          if(SLen(g_gameDir)&&g_gameDir[SLen(g_gameDir)-1]!='\\') SCat(g_gameDir,"\\");
      } }

    /* ...and --dir beats both, because it is the developer saying WHICH install to show. It is
       not remembered: photographing a folder must not change what the next window opens on. */
    { char dir[MAX_PATH];
      if(cmd&&WantedDir(cmd,dir,sizeof dir)){
          SCpy(g_gameDir,dir);
          if(SLen(g_gameDir)&&g_gameDir[SLen(g_gameDir)-1]!='\\') SCat(g_gameDir,"\\");
      } }

    Relocate();
    /* --shot builds the window HIDDEN, so nothing appears on the shared screen. */
    { char shot[MAX_PATH];
      if(cmd&&WantsShot(cmd,shot,sizeof shot)){
          BuildUi(SW_HIDE);
          TabSelect(WantedTab(cmd));
          RefreshPage(g_curTab);
          return ShotWindow(shot);
      } }
    BuildUi(SW_SHOW);
    TabSelect(WantedTab(cmd?cmd:""));
    /* The header shows the SELECTED tab's mod, and TabSelect does not refresh it - so opening on
       any tab but the first showed the first tab's lamp. Photographed: the switch said DISABLED
       over a mod that was installed. */
    RefreshPage(g_curTab);

    while(GetMessageA(&msg,NULL,0,0)>0){
        if(!IsDialogMessageA(g_frame,&msg)){ TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    return 0;
}
