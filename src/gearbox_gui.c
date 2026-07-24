/* gearbox-setup.exe - the shifter binding tool, with a window and a mouse.
 *
 * Replaces the console binder. The console walked a fixed list once, top to bottom, and a
 * mis-press meant starting over - Alex's actual complaint. Here every binding is a button:
 * click it, press the thing you want, done. Click it again to redo just that one. Nothing is
 * sequential and nothing has to be finished in order.
 *
 * WHERE IT LIVES: `<game>\gearbox hshifter setup\`, inside the GOG install. That folder holds
 * every file this module owns - this exe, gearbox.ini, and the mod's own log - so the game
 * directory stays clean and the user has one place to look. The single exception is
 * `gearbox_hook.asi`, which must sit in the game root: Ultimate ASI Loader only scans the game
 * directory plus `scripts\` and `plugins\`, so a mod in any other folder is never loaded.
 *
 * Portable by construction: one exe, no install, no CRT calls, settings written next to the
 * exe as plain text. The game is found by looking one level up from this exe, so the tool
 * works with no configuration at all when it sits where it ships.
 *
 * Rows 0..8 capture DirectInput buttons from ANY attached device - the H-shifter and the
 * wheelbase are different devices and a real rig uses both. Rows 9..11 capture KEYBOARD keys:
 * the mod shifts by pressing the keys the game itself has bound, so it must know them.
 *
 * Build: i686-w64-mingw32-clang -O2 -m32 -mwindows -o gearbox-setup.exe gearbox_gui.c
 *        -lkernel32 -luser32 -lcomdlg32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

/* ---------- DirectInput 8, just enough of it ---------- */
#define DI8_VERSION          0x0800
#define DI8DEVCLASS_GAMECTRL 4
#define DIEDFL_ATTACHEDONLY  0x00000001
#define DIENUM_CONTINUE      1
#define DIENUM_STOP          0
#define DIDF_ABSAXIS         0x00000001
#define DIDFT_ABSAXIS        0x00000002
#define DIDFT_PSHBUTTON      0x00000004
#define DIDFT_TGLBUTTON      0x00000008
#define DIDFT_BUTTON         (DIDFT_PSHBUTTON|DIDFT_TGLBUTTON)
#define DIDFT_ANYINSTANCE    0x00FFFF00
#define DIDFT_OPTIONAL       0x80000000
#define DISCL_BACKGROUND     0x00000008
#define DISCL_NONEXCLUSIVE   0x00000002
#define DI_CREATEDEVICE 3
#define DI_ENUMDEVICES  4
#define DEV_ACQUIRE     7
#define DEV_GETSTATE    9
#define DEV_SETDATAFORMAT 11
#define DEV_SETCOOP     13
#define DEV_POLL        25

typedef struct { DWORD a; WORD b,c; BYTE d[8]; } GUID_;
typedef struct {
    DWORD dwSize; GUID_ guidInstance, guidProduct; DWORD dwDevType;
    CHAR tszInstanceName[260], tszProductName[260]; GUID_ guidFFDriver;
    WORD wUsagePage, wUsage;
} DIDEVINST;
typedef struct { const GUID_ *pguid; DWORD dwOfs,dwType,dwFlags; } DIOBJDF;
typedef struct { DWORD dwSize,dwObjSize,dwFlags,dwDataSize,dwNumObjs; DIOBJDF *rgodf; } DIDF;

typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE,DWORD,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *EnumDevices_t)(void *,DWORD,void *,void *,DWORD);
typedef HRESULT (__stdcall *CreateDevice_t)(void *,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *SetDataFormat_t)(void *,const DIDF *);
typedef HRESULT (__stdcall *SetCoop_t)(void *,HWND,DWORD);
typedef HRESULT (__stdcall *Acquire_t)(void *);
typedef HRESULT (__stdcall *GetState_t)(void *,DWORD,void *);
typedef HRESULT (__stdcall *Poll_t)(void *);

static const GUID_ GUID_XAxis =
    {0xA36D02E0,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID_ IID_IDirectInput8A =
    {0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};

#define NBTN 128
static DIOBJDF g_objs[1+NBTN];
static DIDF g_fmt = { sizeof(DIDF), sizeof(DIOBJDF), DIDF_ABSAXIS, 4+NBTN, 1+NBTN, g_objs };
typedef struct { LONG x; BYTE btn[NBTN]; } DSTATE;

#define MAXDEV 8
static GUID_ g_guid[MAXDEV];
static char  g_dname[MAXDEV][260];
static DWORD g_ndev=0;
static void *g_dev[MAXDEV];
static DSTATE g_prev[MAXDEV];
static int g_haveBase=0;

static void *VT(void *o,int i){ return (void*)((DWORD*)(*(DWORD*)o))[i]; }

static BOOL __stdcall EnumCb(const DIDEVINST *d,void *c){
    (void)c; if(g_ndev>=MAXDEV) return DIENUM_STOP;
    g_guid[g_ndev]=d->guidInstance;
    int i=0; for(;i<259&&d->tszProductName[i];i++) g_dname[g_ndev][i]=d->tszProductName[i];
    g_dname[g_ndev][i]=0; g_ndev++; return DIENUM_CONTINUE;
}

/* ---------- the bindings ---------- */
#define NROW 12
#define ROW_MODEBTN 8
#define ROW_KEYUP   9
#define ROW_KEYDOWN 10
#define ROW_KEYMODE 11
static const char *ROWNAME[NROW]={
    "Gear 1","Gear 2","Gear 3","Gear 4","Gear 5","Gear 6",
    "Reverse","Neutral","Gearbox mode button",
    "Game key: GEAR UP","Game key: GEAR DOWN","Game key: gearbox mode" };
/* ini key for each row, in the format the .asi reads */
static const char *ROWKEY[NROW]={
    "gear1","gear2","gear3","gear4","gear5","gear6",
    "reverse","neutral","mode_btn","gearup_dik","geardown_dik","mode_dik" };

static int g_rowDev[NROW];      /* device index for rows 0..8, -1 = unset */
static int g_rowBtn[NROW];      /* button number, or DIK scancode for rows 9..11 */
static int g_capture=-1;        /* row currently waiting for input, -1 = none */

static HWND g_hwnd, g_btn[NROW], g_clr[NROW], g_log, g_gamePathEdit, g_toggle, g_hold, g_press;
/* the A/M pair is one choice shown as two controls, so neither is ever repainted alone */
static void RefreshMode(void){
    if(g_hold)  InvalidateRect(g_hold,NULL,TRUE);
    if(g_press) InvalidateRect(g_press,NULL,TRUE);
}
/* set while we watch how long the just-bound mode control stays down */
static int g_modeWatchDev=-1, g_modeWatchBtn=-1; static DWORD g_modeWatchDown=0;
static int g_holdOn=0;   /* the hold-switch option - owner-drawn, so we hold the state */
static char g_exeDir[MAX_PATH];
static void RefreshToggle(void);   /* keeps the ON/OFF button label true */
static char g_gameExe[MAX_PATH];

/* ---------- tiny helpers (no CRT) ---------- */
static int SLen(const char *s){ int n=0; while(s[n])n++; return n; }
static void SCat(char *d,const char *s){ int n=SLen(d),i=0; while(s[i]) d[n+i]=s[i],i++; d[n+i]=0; }
static void SCpy(char *d,const char *s){ int i=0; while(s[i]) d[i]=s[i],i++; d[i]=0; }

static void LogLine(const char *s){
    int n=GetWindowTextLengthA(g_log);
    SendMessageA(g_log,EM_SETSEL,(WPARAM)n,(LPARAM)n);
    SendMessageA(g_log,EM_REPLACESEL,FALSE,(LPARAM)s);
    SendMessageA(g_log,EM_REPLACESEL,FALSE,(LPARAM)"\r\n");
}

static const char *KeyName(int sc,char *buf){
    /* scancode -> a readable name, via the OS so a non-US layout still reads right */
    if(GetKeyNameTextA((LONG)(sc<<16),buf,32)>0) return buf;
    wsprintfA(buf,"scancode 0x%02X",sc); return buf;
}

static void RefreshRow(int r){
    char t[320],kb[40];
    if(r>=ROW_KEYUP){
        if(g_rowBtn[r]<0) SCpy(t,"click to set");
        else wsprintfA(t,"%s   (0x%02X)",KeyName(g_rowBtn[r],kb),g_rowBtn[r]);
    } else if(g_rowBtn[r]<0){
        SCpy(t, r==7 ? "none - neutral is the rest position" : "click to set");
    } else {
        wsprintfA(t,"button %d   -   %s",g_rowBtn[r],g_dname[g_rowDev[r]]);
    }
    if(g_capture==r) SCpy(t, r>=ROW_KEYUP ? "press a KEY now..." : "press a button now...");
    SetWindowTextA(g_btn[r],t);
}
static void RefreshAll(void){ for(int i=0;i<NROW;i++) RefreshRow(i); }

/* ---------- settings file ---------- */
/* The one folder every gearbox file lives in, inside the game install. This exe ships in it,
   so "next to the exe" and "in the gearbox folder" are the same place - and stay the same
   place after the user drags the folder somewhere else. */
#define GBDIR "gearbox hshifter setup"
static void SettingsPath(char *out){ SCpy(out,g_exeDir); SCat(out,"gearbox.ini"); }

static void AppendStr(char *buf,const char *s){ SCat(buf,s); }

static void SaveSettings(const char *path){
    char buf[8192]; buf[0]=0;
    AppendStr(buf,"; Mafia gearbox - written by gearbox-setup.exe\r\n; lives in <game>\\" GBDIR "\\ with the rest of the gearbox files\r\n\r\n[hook]\r\n");
    /* device slots, in order of first use */
    int slot[MAXDEV],nslot=0;
    for(int r=0;r<=ROW_MODEBTN;r++){
        if(g_rowBtn[r]<0||g_rowDev[r]<0) continue;
        int seen=0; for(int s=0;s<nslot;s++) if(slot[s]==g_rowDev[r]) seen=1;
        if(!seen&&nslot<4) slot[nslot++]=g_rowDev[r];
    }
    char line[512];
    for(int s=0;s<nslot;s++){
        wsprintfA(line,"device%d=%s\r\nsuppress%d=",s+1,g_dname[slot[s]],s+1); AppendStr(buf,line);
        int first=1;
        for(int r=0;r<=ROW_MODEBTN;r++) if(g_rowDev[r]==slot[s]&&g_rowBtn[r]>=0){
            wsprintfA(line,"%s%d",first?"":",",g_rowBtn[r]); AppendStr(buf,line); first=0; }
        AppendStr(buf,"\r\n");
    }
    AppendStr(buf,"\r\n[shift]\r\nenable=1\r\nclosed_loop=1\r\n\r\n");
    for(int r=0;r<=ROW_MODEBTN;r++){
        if(g_rowBtn[r]<0){
            if(r==7) AppendStr(buf,"; neutral is the lever's rest position - no button\r\nneutral=-1\r\n");
            continue;
        }
        int s=0; for(int i=0;i<nslot;i++) if(slot[i]==g_rowDev[r]) s=i;
        wsprintfA(line,"%s=%d:%d\r\n",ROWKEY[r],s+1,g_rowBtn[r]); AppendStr(buf,line);
    }
    AppendStr(buf,"\r\n");
    for(int r=ROW_KEYUP;r<NROW;r++){
        wsprintfA(line,"%s=0x%02X\r\n",ROWKEY[r],g_rowBtn[r]>=0?g_rowBtn[r]:0); AppendStr(buf,line);
    }
    AppendStr(buf,"hold_ms=40\r\ngap_ms=60\r\nretry_ms=1200\r\n");
    /* measured on a real drive: gate-to-gate takes 0.22-0.94 s, a deliberate neutral 1.1 s+ */
    AppendStr(buf,"neutral_delay_ms=1100\r\n");
    /* this one silently went missing once: the checkbox was ticked, the file said nothing, and
       the mod ran in button mode against a latching switch - one toggle in, one toggle out */
    { wsprintfA(line,"mode_hold=%d\r\n",g_holdOn?1:0); AppendStr(buf,line); }
    AppendStr(buf,"\r\n; GOG address table\r\ngameptr=0x63788C\r\ngear_ofs=0x5D0\r\nmode_ofs=0x53C\r\n");
    if(g_gameExe[0]){ wsprintfA(line,"\r\n[app]\r\ngame_exe=%s\r\n",g_gameExe); AppendStr(buf,line); }

    HANDLE h=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){ char m[400]; wsprintfA(m,"could NOT write %s",path); LogLine(m); return; }
    DWORD w; WriteFile(h,buf,(DWORD)SLen(buf),&w,NULL); CloseHandle(h);
    char m[400]; wsprintfA(m,"saved %s",path); LogLine(m);
}

static void LoadSettingsFrom(const char *path){
    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) return;
    char names[4][64]; int nnames=0;
    for(int s=0;s<4;s++){
        char k[16]; wsprintfA(k,"device%d",s+1);
        GetPrivateProfileStringA("hook",k,"",names[s],sizeof(names[s]),path);
        if(names[s][0]) nnames=s+1;
    }
    for(int r=0;r<NROW;r++){
        char v[64]; GetPrivateProfileStringA("shift",ROWKEY[r],"",v,sizeof(v),path);
        if(!v[0]) continue;
        if(r>=ROW_KEYUP){
            int val=0,i=0; if(v[0]=='0'&&(v[1]=='x'||v[1]=='X')) i=2;
            for(;v[i];i++){ char c=v[i]; int d;
                if(c>='0'&&c<='9') d=c-'0'; else if(c>='a'&&c<='f') d=c-'a'+10;
                else if(c>='A'&&c<='F') d=c-'A'+10; else break;
                val=val*16+d; }
            if(val>0) g_rowBtn[r]=val;
            continue;
        }
        /* "<slot>:<button>" - the slot names a device, which we match back by name */
        int a=0,b=0,i=0,neg=0;
        if(v[0]=='-'){ neg=1; i=1; }
        for(;v[i]>='0'&&v[i]<='9';i++) a=a*10+(v[i]-'0');
        if(neg) continue;                       /* neutral=-1 : rest position, leave unset */
        if(v[i]==':'){ i++; for(;v[i]>='0'&&v[i]<='9';i++) b=b*10+(v[i]-'0'); }
        else { b=a; a=1; }
        if(a<1||a>nnames) continue;
        for(DWORD d=0;d<g_ndev;d++){
            const char *n=g_dname[d],*s=names[a-1]; int hit=0;
            for(int p=0;n[p];p++){ int k=0; while(s[k]&&n[p+k]==s[k]) k++; if(!s[k]){ hit=1; break; } }
            if(hit){ g_rowDev[r]=(int)d; g_rowBtn[r]=b; break; }
        }
    }
    { char v[16]; GetPrivateProfileStringA("shift","mode_hold","0",v,sizeof(v),path);
      g_holdOn=(v[0]=='1'); RefreshMode(); }
    GetPrivateProfileStringA("app","game_exe","",g_gameExe,sizeof(g_gameExe),path);
    if(g_gameExe[0]) SetWindowTextA(g_gamePathEdit,g_gameExe);
    RefreshToggle();
}
/* This exe ships inside `<game>\gearbox hshifter setup\`, so the game is one level up. Finding
   it that way means a fresh install needs no file dialog and no stored path - and a stored path
   that no longer resolves (the folder was moved, the game reinstalled) is replaced rather than
   silently kept, which is how the tool ended up pointing at a game that was not there. */
static int FindGameBesideUs(char *out){
    char p[MAX_PATH]; SCpy(p,g_exeDir);
    int n=SLen(p); if(n>0&&(p[n-1]=='\\'||p[n-1]=='/')) n--;      /* drop our own trailing slash */
    while(n>0&&p[n-1]!='\\'&&p[n-1]!='/') n--;                     /* ...and our own folder name */
    p[n]=0;
    if(!n) return 0;
    SCat(p,"Game.exe");
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES) return 0;
    SCpy(out,p); return 1;
}
static void LoadSettings(void){
    char path[MAX_PATH]; SettingsPath(path);
    if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES){
        LoadSettingsFrom(path);
        LogLine("loaded gearbox.ini from next to the exe");
    }
    if(!g_gameExe[0]||GetFileAttributesA(g_gameExe)==INVALID_FILE_ATTRIBUTES){
        char found[MAX_PATH];
        if(FindGameBesideUs(found)){
            SCpy(g_gameExe,found); SetWindowTextA(g_gamePathEdit,g_gameExe);
            char m[400]; wsprintfA(m,"found the game one folder up: %s",g_gameExe); LogLine(m);
            RefreshToggle();
        }
    }
}

/* ---------- install into the game ---------- */
static void GameDir(char *out){
    SCpy(out,g_gameExe);
    int n=SLen(out); while(n>0&&out[n-1]!='\\'&&out[n-1]!='/') n--; out[n]=0;
}
/* case-insensitive path compare, enough for "did we just write the file we live in?" */
static int SameStrI(const char *a,const char *b){
    int i=0; for(;a[i]&&b[i];i++){
        char x=a[i],y=b[i];
        if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32;
        if(x=='/') x='\\'; if(y=='/') y='\\';
        if(x!=y) return 0;
    }
    return a[i]==b[i];
}
static int InstallToGame(void){
    if(!g_gameExe[0]){ LogLine("no game exe chosen - use 'Choose Game.exe' first"); return 0; }
    char dir[MAX_PATH],sub[MAX_PATH],dst[MAX_PATH],src[MAX_PATH],msg[600];
    GameDir(dir);
    SCpy(sub,dir); SCat(sub,GBDIR);
    CreateDirectoryA(sub,NULL);                       /* our files live in their own folder */
    SCpy(dst,sub); SCat(dst,"\\gearbox.ini");
    SaveSettings(dst);
    /* when the tool is run from outside the game, keep its own copy current too */
    { char here[MAX_PATH]; SettingsPath(here); if(!SameStrI(here,dst)) SaveSettings(here); }
    /* the .asi cannot join it: Ultimate ASI Loader scans the game root, `scripts\` and
       `plugins\` and nothing else, so the mod has to sit in the root next to mafia_ffb.asi */
    SCpy(src,g_exeDir); SCat(src,"gearbox_hook.asi");
    char adst[MAX_PATH]; SCpy(adst,dir); SCat(adst,"gearbox_hook.asi");
    if(GetFileAttributesA(src)!=INVALID_FILE_ATTRIBUTES){
        if(CopyFileA(src,adst,FALSE)){ wsprintfA(msg,"installed gearbox_hook.asi -> %s",adst); LogLine(msg); }
        else LogLine("could not copy gearbox_hook.asi (is the game running?)");
    } else if(GetFileAttributesA(adst)!=INVALID_FILE_ATTRIBUTES){
        LogLine("gearbox_hook.asi is already in the game folder - settings updated, mod left as it is");
    } else {
        LogLine("WARNING: no gearbox_hook.asi anywhere - the settings are installed but nothing will load them");
    }
    wsprintfA(msg,"settings installed -> %s",dst); LogLine(msg);
    RefreshToggle();
    return 1;
}
/* ---------- the red button: give the game back exactly as it was ----------------------------
 * A user may want the wheel's buttons back for their in-game bindings, or to play on the
 * keyboard, without uninstalling anything. Renaming the .asi is the honest off switch: with it
 * renamed the loader never loads us, so nothing is hooked, nothing is suppressed and nothing is
 * injected - the game is stock on the next launch. Settings are left alone, so switching back
 * on costs one click. */
static void AsiPaths(char *on,char *off){
    char dir[MAX_PATH]; GameDir(dir);
    SCpy(on,dir);  SCat(on,"gearbox_hook.asi");
    SCpy(off,dir); SCat(off,"gearbox_hook.asi.off");
}
static int ModIsOn(void){
    if(!g_gameExe[0]) return -1;
    char on[MAX_PATH],off[MAX_PATH]; AsiPaths(on,off);
    if(GetFileAttributesA(on)!=INVALID_FILE_ATTRIBUTES) return 1;
    if(GetFileAttributesA(off)!=INVALID_FILE_ATTRIBUTES) return 0;
    return -1;
}
static void ToggleMod(void){
    if(!g_gameExe[0]){ LogLine("choose Game.exe first"); return; }
    char on[MAX_PATH],off[MAX_PATH]; AsiPaths(on,off);
    int st=ModIsOn();
    if(st==1){
        DeleteFileA(off);
        if(MoveFileA(on,off)) LogLine("MOD OFF - the game is stock again: buttons, keyboard and gearbox all behave as they did before. Settings kept.");
        else LogLine("could not switch off (is the game running?)");
    } else if(st==0){
        if(MoveFileA(off,on)) LogLine("MOD ON - shifter active on the next launch.");
        else LogLine("could not switch on (is the game running?)");
    } else {
        LogLine("no gearbox_hook.asi in the game folder - press 'Apply' first");
    }
    RefreshToggle();
}

/* This control says what IS, not what clicking it would do. "Disable mod" drawn in red is
   ambiguous in exactly the way Alex hit: red reads as the state, the verb reads as the state,
   and the two disagree. So the label is the state and the colour is the state - green for on,
   red for off - and clicking it flips both. */
static void RefreshToggle(void){
    int st=ModIsOn();
    SetWindowTextA(g_toggle, st==1 ? "Mod is ENABLED" : st==0 ? "Mod is DISABLED" : "Not installed");
    /* the label changes width, so the old one has to be erased, not merely drawn over */
    if(g_toggle) InvalidateRect(g_toggle,NULL,TRUE);
}

static void LaunchGame(void){
    if(!InstallToGame()) return;
    char dir[MAX_PATH]; GameDir(dir);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    for(int i=0;i<(int)sizeof(si);i++) ((BYTE*)&si)[i]=0;
    si.cb=sizeof(si);
    if(CreateProcessA(g_gameExe,NULL,NULL,NULL,FALSE,0,NULL,dir,&si,&pi)){
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); LogLine("game launched");
    } else LogLine("could not start the game");
}
/* The label on the window is a claim, so it is checked. Mafia's version resource says
   "Master 1.0" on every build ever shipped, so it is worthless - the GOG v1.3 Game.exe is
   identified by its size, which is what the gear addresses were derived against. */
#define GOG13_GAME_EXE_SIZE 2355200
static void CheckGameBuild(void){
    if(!g_gameExe[0]) return;
    HANDLE h=CreateFileA(g_gameExe,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){ LogLine("cannot open that Game.exe"); return; }
    DWORD sz=GetFileSize(h,NULL); CloseHandle(h);
    char m[300];
    if(sz==GOG13_GAME_EXE_SIZE) LogLine("Game.exe matches the GOG v1.3 build this mod was tested on");
    else { wsprintfA(m,"WARNING: this Game.exe is %lu bytes, not the %d of GOG v1.3 - the gear "
                      "addresses were derived on that build and may not fit this one",sz,GOG13_GAME_EXE_SIZE);
           LogLine(m); }
}
static void ChooseGame(void){
    char buf[MAX_PATH]; buf[0]=0;
    OPENFILENAMEA ofn; for(int i=0;i<(int)sizeof(ofn);i++) ((BYTE*)&ofn)[i]=0;
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter="Game.exe\0Game.exe\0All files\0*.*\0"; ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(GetOpenFileNameA(&ofn)){ SCpy(g_gameExe,buf); SetWindowTextA(g_gamePathEdit,g_gameExe); LogLine(g_gameExe); CheckGameBuild(); RefreshToggle(); }
}

/* ---------- input capture ---------- */
static void PollDevices(DSTATE *out){
    for(DWORD i=0;i<g_ndev;i++){
        for(int k=0;k<(int)sizeof(DSTATE);k++) ((BYTE*)&out[i])[k]=0;
        if(!g_dev[i]) continue;
        ((Poll_t)VT(g_dev[i],DEV_POLL))(g_dev[i]);
        if(((GetState_t)VT(g_dev[i],DEV_GETSTATE))(g_dev[i],sizeof(DSTATE),&out[i])<0)
            ((Acquire_t)VT(g_dev[i],DEV_ACQUIRE))(g_dev[i]);
    }
}
static void OnTimer(void){
    DSTATE cur[MAXDEV]; PollDevices(cur);
    if(!g_haveBase){ for(DWORD i=0;i<g_ndev;i++) g_prev[i]=cur[i]; g_haveBase=1; return; }

    if(g_capture>=0&&g_capture<ROW_KEYUP){
        for(DWORD i=0;i<g_ndev;i++){
            if(!g_dev[i]) continue;
            for(int b=0;b<NBTN;b++){
                int now=(cur[i].btn[b]&0x80)!=0, was=(g_prev[i].btn[b]&0x80)!=0;
                if(now&&!was){
                    int r=g_capture; g_capture=-1;
                    g_rowDev[r]=(int)i; g_rowBtn[r]=b;
                    if(r==ROW_MODEBTN){ g_modeWatchDev=(int)i; g_modeWatchBtn=b; g_modeWatchDown=GetTickCount(); }
                    char m[400]; wsprintfA(m,"%s = button %d on %s",ROWNAME[r],b,g_dname[i]);
                    LogLine(m); RefreshRow(r);
                    for(DWORD k=0;k<g_ndev;k++) g_prev[k]=cur[k];
                    return;
                }
            }
        }
    } else if(g_capture>=ROW_KEYUP){
        static int prevKey[256]; static int keyBase=0;
        if(!keyBase){ for(int v=0;v<256;v++) prevKey[v]=(GetAsyncKeyState(v)&0x8000)!=0; keyBase=1; }
        for(int vk=0x08;vk<=0xFE;vk++){
            if(vk==VK_LBUTTON||vk==VK_RBUTTON||vk==VK_MBUTTON) continue;
            int now=(GetAsyncKeyState(vk)&0x8000)!=0;
            if(now&&!prevKey[vk]){
                UINT sc=MapVirtualKeyA((UINT)vk,0);
                int r=g_capture; g_capture=-1; keyBase=0;
                if(sc){ g_rowBtn[r]=(int)sc;
                    char m[400],kb[40]; wsprintfA(m,"%s = %s (scancode 0x%02X)",ROWNAME[r],KeyName((int)sc,kb),sc);
                    LogLine(m); }
                RefreshRow(r);
                for(int v=0;v<256;v++) prevKey[v]=0;
                for(DWORD k=0;k<g_ndev;k++) g_prev[k]=cur[k];
                return;
            }
            prevKey[vk]=now;
        }
    }
    /* A latching switch stays down; a button springs back. One second of daylight between the
       two is plenty, and getting this wrong inverts the gearbox mode, so it is worth deciding
       for the user rather than asking them to know. */
    if(g_modeWatchDev>=0&&g_modeWatchBtn>=0){
        int down=(cur[g_modeWatchDev].btn[g_modeWatchBtn]&0x80)!=0;
        DWORD held=GetTickCount()-g_modeWatchDown;
        if(!down){
            char m[200];
            if(held>1000){
                g_holdOn=1;
                wsprintfA(m,"that control stayed down %lu ms - it is a switch, so 'Hold to switch A/M' is now chosen",held);
            } else {
                /* it also has to be SET, not merely reported: a profile loaded with hold=1 used
                   to survive this branch, so the log said button and the file still said switch */
                g_holdOn=0;
                wsprintfA(m,"that control sprang back after %lu ms - it is a button, so 'One press to switch A/M' is now chosen",held);
            }
            RefreshMode();
            LogLine(m);
            g_modeWatchDev=-1; g_modeWatchBtn=-1;
        } else if(held>3000){
            g_holdOn=1; RefreshMode();
            LogLine("that control is still down after 3 s - it is a switch, so 'Hold to switch A/M' is now chosen");
            g_modeWatchDev=-1; g_modeWatchBtn=-1;
        }
    }
    for(DWORD i=0;i<g_ndev;i++) g_prev[i]=cur[i];
}

/* ---------- window ---------- */
#define ID_BTN0  1000
#define ID_CLR0  2000
#define ID_SAVE  3001
#define ID_LAUNCH 3002
#define ID_CHOOSE 3003
#define ID_INSTALL 3004
#define ID_TOGGLE  3005
#define ID_HOLD    3006
#define ID_PRESS   3008
#define ID_LOAD    3007
#define ID_LOG   3010
#define ID_PATH  3011


/* ============================ THE MAFIA SKIN ==============================================
 * The game's menus are a sheet of crumpled off-white paper with torn, sooty edges laid over a
 * dark room; headings are handwritten, bindings sit in thin-ruled white fields, and the row you
 * are on carries a small red arrow. None of that is a Windows control, so every control here is
 * owner-drawn and the paper is generated at runtime - no image files, the exe stays one portable
 * file that a user can drop anywhere.
 */
#define INK        RGB(24,20,16)
#define INK_SOFT   RGB(78,70,60)
#define PAPER      RGB(232,228,216)
#define FIELD      RGB(246,244,238)
#define ROOMDARK   RGB(30,26,22)
#define MAFIA_RED  RGB(150,22,22)
/* the one colour the game's palette does not have, and the one thing here that needs a state
   rather than an action: the mod is either on or it is not */
#define MAFIA_GREEN RGB(32,94,42)

static HFONT g_fTitle,g_fBody,g_fField,g_fAction;
static HBRUSH g_brPaper,g_brField,g_brRoom;
static HBITMAP g_paper; static int g_paperW,g_paperH;
static HBRUSH g_brSheet;   /* the paper itself, as a brush, so labels blend into it */

/* a deterministic noise source - the same sheet of paper every launch */
static unsigned g_rnd=0x1B3F5D7u;
static unsigned Rnd(void){ g_rnd=g_rnd*1664525u+1013904223u; return (g_rnd>>16)&0x7FFF; }

static void MakePaper(HDC ref,int w,int h){
    if(g_paper&&g_paperW==w&&g_paperH==h) return;
    if(g_paper) DeleteObject(g_paper);
    g_paperW=w; g_paperH=h;
    HDC mem=CreateCompatibleDC(ref);
    g_paper=CreateCompatibleBitmap(ref,w,h);
    HGDIOBJ old=SelectObject(mem,g_paper);
    RECT all={0,0,w,h}; FillRect(mem,&all,g_brPaper);
    g_rnd=0x1B3F5D7u;
    /* creases: long faint strokes, lighter and darker, the way folded paper catches light */
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
    /* sooty, worn edges */
    for(int i=0;i<1800;i++){
        int edge=(int)(Rnd()&3), d=(int)(Rnd()%15), r=1+(int)(Rnd()%2);
        int x,y;
        if(edge==0){ x=(int)(Rnd()%(unsigned)w); y=d; }
        else if(edge==1){ x=(int)(Rnd()%(unsigned)w); y=h-1-d; }
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
    /* headings handwritten, lists in a plain serif, the menu row heavy - as the game has them */
    g_fTitle =CreateFontA(27,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe Script");
    g_fBody  =CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Georgia");
    g_fField =CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
    g_fAction=CreateFontA(17,0,0,0,FW_BOLD,  0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Arial");
}

#define TITLEBAND 62
static void PaintBackdrop(HWND h,HDC dc){
    RECT rc; GetClientRect(h,&rc);
    MakePaper(dc,rc.right,rc.bottom);
    HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_paper);
    BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);
    SelectObject(mem,old); DeleteDC(mem);

    RECT band={0,0,rc.right,TITLEBAND};
    FillRect(dc,&band,g_brRoom);
    /* the band's torn lower lip */
    g_rnd=0x77A21u;
    for(int x=0;x<rc.right;x+=2){
        int d=(int)(Rnd()%7);
        RECT t={x,TITLEBAND,x+2,TITLEBAND+d};
        FillRect(dc,&t,g_brRoom);
    }
    SetBkMode(dc,TRANSPARENT);
    HGDIOBJ of=SelectObject(dc,g_fTitle);
    SetTextColor(dc,RGB(238,233,222));
    TextOutA(dc,26,14,"Gearbox - H-shifter Setup:",26);
    SelectObject(dc,g_fBody);
    SetTextColor(dc,RGB(150,140,124));
    TextOutA(dc,rc.right-190,30,"tested on GOG v1.3",18);
    SelectObject(dc,of);
}

/* the red arrow the game puts beside the row you are on */
static void RedArrow(HDC dc,int x,int cy){
    POINT p[3]={{x,cy-6},{x+8,cy},{x,cy+6}};
    HBRUSH b=CreateSolidBrush(MAFIA_RED); HGDIOBJ ob=SelectObject(dc,b);
    HPEN pen=CreatePen(PS_SOLID,1,MAFIA_RED); HGDIOBJ op=SelectObject(dc,pen);
    Polygon(dc,p,3);
    SelectObject(dc,op); DeleteObject(pen); SelectObject(dc,ob); DeleteObject(b);
}

/* An owner-drawn control is handed a DC nobody erased, and the parent is WS_CLIPCHILDREN so it
 * never paints underneath one. Whatever the control drew last is STILL THERE: a label that
 * changes ("Enable mod" -> "Disable mod") lands on top of its predecessor, and the focus
 * underline outlives the focus - both of which Alex saw on screen. So every branch below starts
 * by putting the backdrop back, cut from the same paper bitmap at the same offset so the grain
 * lines up with the window instead of restarting inside each button. */
static void PaperBlit(HDC dc,HWND item,const RECT *r){
    POINT o={0,0}; HWND par=GetParent(item);
    if(par) MapWindowPoints(item,par,&o,1);
    RECT rr=*r;
    /* the dark band across the top is painted over the paper, not into it */
    if(o.y+r->bottom<=TITLEBAND){ FillRect(dc,&rr,g_brRoom); return; }
    if(!g_paper){ FillRect(dc,&rr,g_brPaper); return; }
    HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_paper);
    BitBlt(dc,r->left,r->top,r->right-r->left,r->bottom-r->top,
           mem,o.x+r->left,o.y+r->top,SRCCOPY);
    SelectObject(mem,old); DeleteDC(mem);
}

static void DrawItem(DRAWITEMSTRUCT *d){
    char txt[320]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
    int id=(int)d->CtlID;
    RECT r=d->rcItem;
    SetBkMode(d->hDC,TRANSPARENT);

    if(id>=ID_BTN0&&id<ID_BTN0+NROW){
        FillRect(d->hDC,&r,g_brField);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(60,54,48)); HGDIOBJ op=SelectObject(d->hDC,pen);
        HGDIOBJ ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);
        SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        int capturing=(g_capture==id-ID_BTN0);
        if(capturing||(d->itemState&ODS_FOCUS)) RedArrow(d->hDC,r.left-12,(r.top+r.bottom)/2);
        HGDIOBJ of=SelectObject(d->hDC,g_fField);
        SetTextColor(d->hDC,capturing?MAFIA_RED:INK);
        RECT t=r; t.left+=10;
        DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(d->hDC,of);
        return;
    }
    if(id>=ID_CLR0&&id<ID_CLR0+NROW){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ of=SelectObject(d->hDC,g_fBody);
        SetTextColor(d->hDC,(d->itemState&ODS_SELECTED)?MAFIA_RED:INK_SOFT);
        DrawTextA(d->hDC,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(d->hDC,of);
        return;
    }
    if(id==ID_HOLD||id==ID_PRESS){
        /* one choice, two buttons: the live one is green, the other is plain black. A tick box
           showed neither what the alternative was nor which of the two was in force. */
        PaperBlit(d->hDC,d->hwndItem,&r);
        int chosen = (id==ID_HOLD) ? g_holdOn : !g_holdOn;
        COLORREF c = chosen ? MAFIA_GREEN : INK;
        HPEN pen=CreatePen(PS_SOLID,chosen?2:1,chosen?MAFIA_GREEN:INK_SOFT);
        HGDIOBJ op=SelectObject(d->hDC,pen), ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);
        SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        HGDIOBJ of=SelectObject(d->hDC,chosen?g_fAction:g_fBody);
        SetTextColor(d->hDC,c);
        DrawTextA(d->hDC,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(d->hDC,of);
        return;
    }
    if(id==ID_TOGGLE){
        /* not an action but a state, so it is drawn as one: a boxed lamp that reads green when
           the mod is on and red when it is off. It is still a button - clicking flips it. */
        PaperBlit(d->hDC,d->hwndItem,&r);
        int st=ModIsOn();
        COLORREF c = st==1 ? MAFIA_GREEN : st==0 ? MAFIA_RED : INK_SOFT;
        HPEN pen=CreatePen(PS_SOLID,(d->itemState&ODS_SELECTED)?2:1,c);
        HGDIOBJ op=SelectObject(d->hDC,pen), ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);
        SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        HGDIOBJ of=SelectObject(d->hDC,g_fAction);
        SetTextColor(d->hDC,c);
        DrawTextA(d->hDC,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(d->hDC,of);
        return;
    }
    {   /* the action row: heavy type, no chrome. Red and the underline both mark the PRESS and
           nothing else - they appear on mouse-down and are gone on mouse-up. A clicked button
           keeps the focus, so tying either to focus left the last thing you touched permanently
           lit, which is what it looked like: a state, on a control that has none. */
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ of=SelectObject(d->hDC,g_fAction);
        int pressed=(d->itemState&ODS_SELECTED)!=0;
        SetTextColor(d->hDC,pressed?MAFIA_RED:INK);
        DrawTextA(d->hDC,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(pressed){
            HPEN pen=CreatePen(PS_SOLID,2,MAFIA_RED); HGDIOBJ op=SelectObject(d->hDC,pen);
            MoveToEx(d->hDC,r.left+6,r.bottom-4,NULL); LineTo(d->hDC,r.right-6,r.bottom-4);
            SelectObject(d->hDC,op); DeleteObject(pen);
        }
        SelectObject(d->hDC,of);
    }
}

static HWND MkText(HWND p,const char *s,int x,int y,int w,int h){
    return CreateWindowExA(0,"STATIC",s,WS_CHILD|WS_VISIBLE,x,y,w,h,p,NULL,NULL,NULL);
}
static HWND MkBtn(HWND p,const char *s,int x,int y,int w,int h,int id){
    return CreateWindowExA(0,"BUTTON",s,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,x,y,w,h,p,(HMENU)(INT_PTR)id,NULL,NULL);
}

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE: {
        HFONT f=g_fBody;
        int y=TITLEBAND+24;
        MkText(h,"Click a binding, then press the button or key you want. Click again to change it.",26,y,640,18); y+=26;
        for(int i=0;i<NROW;i++){
            /* The mode row gets its own fenced block. The checkbox belongs to THIS row alone -
               floating under the gear rows it read as a global setting - so a rule above, a rule
               below and some air make the scope unmistakable. */
            if(i==ROW_MODEBTN){
                y+=8;
                CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,26,y,644,2,h,NULL,NULL,NULL);
                y+=10;
            }
            if(i==ROW_KEYUP){
                y+=8;
                CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,26,y,644,2,h,NULL,NULL,NULL);
                y+=10;
                MkText(h,"The keys the GAME itself uses - set them in Mafia's Options, then press them here:",26,y,644,18); y+=22;
            }
            HWND s=MkText(h,ROWNAME[i],26,y+4,170,18);
            g_btn[i]=MkBtn(h,"click to set",200,y,398,24,ID_BTN0+i);
            g_clr[i]=MkBtn(h,"clear",606,y,64,24,ID_CLR0+i);
            if(i==ROW_MODEBTN){
                /* Users often do not know whether their own shifter latches or springs back, so
                   each option says what the control DOES, and the tool decides for them by timing
                   it when it is bound. A ticked box said neither which two things were on offer
                   nor which one was live, so it is two buttons: the chosen one is green, the
                   other is plain black, and they are visibly one-or-the-other. */
                g_hold =MkBtn(h,"Hold to switch A/M",200,y+28,200,24,ID_HOLD);
                g_press=MkBtn(h,"One press to switch A/M",410,y+28,236,24,ID_PRESS);
                SendMessageA(g_hold,WM_SETFONT,(WPARAM)f,TRUE);
                SendMessageA(g_press,WM_SETFONT,(WPARAM)f,TRUE);
                y+=32;
            }
            SendMessageA(s,WM_SETFONT,(WPARAM)f,TRUE);
            SendMessageA(g_btn[i],WM_SETFONT,(WPARAM)f,TRUE);
            SendMessageA(g_clr[i],WM_SETFONT,(WPARAM)f,TRUE);
            y+=28;
        }
        y+=6;
        HWND s2=MkText(h,"Game.exe",26,y+4,80,18);
        g_gamePathEdit=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_READONLY,
            110,y,340,24,h,(HMENU)(INT_PTR)ID_PATH,NULL,NULL);
        HWND b1=MkBtn(h,"Choose Game.exe",462,y,208,24,ID_CHOOSE);
        y+=34;
        /* Windows convention: the primary action sits bottom-RIGHT and everything secondary
           or reversible groups to the left, away from it. Apply & Launch is what the user
           reaches for at the end, so it gets the corner. */
        HWND b2=MkBtn(h,"Save settings",26,y,116,28,ID_SAVE);
        HWND b5=MkBtn(h,"Load settings",146,y,116,28,ID_LOAD);
        g_toggle=MkBtn(h,"Mod is ENABLED",266,y,144,28,ID_TOGGLE);   /* widest label: 14 chars */
        HWND b3=MkBtn(h,"Apply",416,y,90,28,ID_INSTALL);
        HWND b4=MkBtn(h,"Apply & Launch",512,y,158,28,ID_LAUNCH);
        SendMessageA(b5,WM_SETFONT,(WPARAM)f,TRUE);
        y+=36;
        g_log=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            26,y,644,150,h,(HMENU)(INT_PTR)ID_LOG,NULL,NULL);
        SendMessageA(s2,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageA(g_gamePathEdit,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageA(b1,WM_SETFONT,(WPARAM)f,TRUE); SendMessageA(b2,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageA(b3,WM_SETFONT,(WPARAM)f,TRUE); SendMessageA(b4,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageA(g_toggle,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageA(g_log,WM_SETFONT,(WPARAM)f,TRUE);
        SetTimer(h,1,8,NULL);
        return 0; }
    case WM_COMMAND: {
        int id=LOWORD(w);
        if(id>=ID_BTN0&&id<ID_BTN0+NROW){
            int prev=g_capture; g_capture=id-ID_BTN0; g_haveBase=0;
            if(prev>=0) RefreshRow(prev);
            RefreshRow(g_capture);
            return 0;
        }
        if(id>=ID_CLR0&&id<ID_CLR0+NROW){
            int r=id-ID_CLR0; g_rowDev[r]=-1; g_rowBtn[r]=-1;
            if(g_capture==r) g_capture=-1;
            RefreshRow(r);
            char m[200]; wsprintfA(m,"%s cleared",ROWNAME[r]); LogLine(m);
            return 0;
        }
        if(id==ID_SAVE){ char p[MAX_PATH]; SettingsPath(p); SaveSettings(p); return 0; }
        if(id==ID_LOAD){
            char p[MAX_PATH]; SettingsPath(p);
            if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES) LogLine("no gearbox.ini next to the exe yet - save one first");
            else { LoadSettingsFrom(p); RefreshAll(); CheckGameBuild(); LogLine("settings reloaded from gearbox.ini"); }
            return 0; }
        if(id==ID_INSTALL){ InstallToGame(); return 0; }
        if(id==ID_LAUNCH){ char p[MAX_PATH]; SettingsPath(p); SaveSettings(p); LaunchGame(); return 0; }
        if(id==ID_CHOOSE){ ChooseGame(); return 0; }
        if(id==ID_TOGGLE){ ToggleMod(); return 0; }
        /* each button SETS its own answer - it does not toggle, so clicking the live one twice
           cannot leave the pair showing the opposite of what it says */
        if(id==ID_HOLD){  g_holdOn=1; RefreshMode(); return 0; }
        if(id==ID_PRESS){ g_holdOn=0; RefreshMode(); return 0; }
        return 0; }
    case WM_ERASEBKGND: return 1;                 /* the paper is painted in WM_PAINT */
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        PaintBackdrop(h,dc);
        EndPaint(h,&ps); return 0; }
    case WM_DRAWITEM: DrawItem((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_CTLCOLORSTATIC: {
        /* a read-only EDIT is coloured through this message too, and a path smeared with paper
           grain is unreadable - those get the clean field, labels get the sheet */
        int cid=GetDlgCtrlID((HWND)l);
        if(cid==ID_PATH||cid==ID_LOG){
            SetBkColor((HDC)w,FIELD); SetTextColor((HDC)w,INK);
            return (LRESULT)g_brField;
        }
        SetBkMode((HDC)w,TRANSPARENT); SetTextColor((HDC)w,INK);
        return (LRESULT)(g_brSheet?g_brSheet:g_brPaper); }
    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)w,FIELD); SetTextColor((HDC)w,INK);
        return (LRESULT)g_brField; }
    case WM_TIMER: OnTimer(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h,m,w,l);
}

/* ---------- headless self-test ------------------------------------------------------------
 * `gearbox-setup.exe --selftest` saves a profile with known bindings, reads it back, and checks
 * every key the .asi will look for. It exists because mode_hold once went missing from the file
 * while the checkbox was ticked: the mod then ran in button mode against a latching switch, and
 * the only way anyone found out was Alex driving. A file that is written is not a file that is
 * complete, and this is what tells the difference. Its two files - selftest-gearbox.ini and
 * selftest-report.txt - are written to the CURRENT directory, not next to the exe: the exe now
 * ships inside the game install, and a test run must not leave anything there.
 */
static void WorkPath(char *out,const char *name){
    DWORD n=GetCurrentDirectoryA(MAX_PATH,out);
    if(!n){ SCpy(out,g_exeDir); SCat(out,name); return; }
    if(n&&out[n-1]!='\\'&&out[n-1]!='/'){ out[n]='\\'; out[n+1]=0; }
    SCat(out,name);
}
static int g_stFail=0;
static char g_stReport[8192];
static void STCheck(const char *what,int ok,const char *got){
    char line[512];
    if(!ok) g_stFail++;
    wsprintfA(line,"  %s  %s%s%s\r\n",ok?"PASS":"FAIL",what,got&&got[0]?"  -> got: ":"",got?got:"");
    SCat(g_stReport,line);
}
static int SelfTest(void){
    char ini[MAX_PATH],val[128],exp[128];
    WorkPath(ini,"selftest-gearbox.ini");
    DeleteFileA(ini);

    /* a rig like Alex's: gears + reverse on a shifter, the mode switch on the wheelbase */
    SCpy(g_dname[0],"TEST-SHIFTER"); SCpy(g_dname[1],"TEST-WHEELBASE"); g_ndev=2;
    for(int i=0;i<NROW;i++){ g_rowDev[i]=-1; g_rowBtn[i]=-1; }
    for(int i=0;i<6;i++){ g_rowDev[i]=0; g_rowBtn[i]=i; }
    g_rowDev[6]=0; g_rowBtn[6]=7;                 /* reverse */
    g_rowDev[7]=-1; g_rowBtn[7]=-1;               /* neutral = rest position */
    g_rowDev[ROW_MODEBTN]=1; g_rowBtn[ROW_MODEBTN]=38;
    g_rowBtn[ROW_KEYUP]=0x1E; g_rowBtn[ROW_KEYDOWN]=0x2C; g_rowBtn[ROW_KEYMODE]=0x30;
    g_holdOn=1;
    SCpy(g_gameExe,"C:\\Games\\Mafia\\Game.exe");

    SCat(g_stReport,"gearbox-setup self-test\r\n\r\n[write]\r\n");
    SaveSettings(ini);
    STCheck("the settings file is created",GetFileAttributesA(ini)!=INVALID_FILE_ATTRIBUTES,ini);

    SCat(g_stReport,"\r\n[every key the mod reads is present and correct]\r\n");
    struct { const char *sec,*key,*want; } K[] = {
        {"hook","device1","TEST-SHIFTER"}, {"hook","suppress1","0,1,2,3,4,5,7"},
        {"hook","device2","TEST-WHEELBASE"}, {"hook","suppress2","38"},
        {"shift","enable","1"}, {"shift","closed_loop","1"},
        {"shift","gear1","1:0"}, {"shift","gear6","1:5"}, {"shift","reverse","1:7"},
        {"shift","neutral","-1"}, {"shift","mode_btn","2:38"},
        {"shift","gearup_dik","0x1E"}, {"shift","geardown_dik","0x2C"}, {"shift","mode_dik","0x30"},
        {"shift","mode_hold","1"},
        {"shift","hold_ms","40"}, {"shift","gap_ms","60"}, {"shift","retry_ms","1200"},
        {"shift","neutral_delay_ms","1100"},
        {"shift","gameptr","0x63788C"}, {"shift","gear_ofs","0x5D0"}, {"shift","mode_ofs","0x53C"},
        {"app","game_exe","C:\\Games\\Mafia\\Game.exe"},
        {0,0,0} };
    for(int i=0;K[i].sec;i++){
        GetPrivateProfileStringA(K[i].sec,K[i].key,"<missing>",val,sizeof(val),ini);
        int ok=1; for(int j=0;;j++){ if(val[j]!=K[i].want[j]){ ok=0; break; } if(!val[j]) break; }
        wsprintfA(exp,"%s=%s (want %s)",K[i].key,val,K[i].want);
        STCheck(exp,ok,"");
    }

    /* the reload path: clear everything, load the file back, and the answers must return */
    SCat(g_stReport,"\r\n[reload]\r\n");
    for(int i=0;i<NROW;i++){ g_rowDev[i]=-1; g_rowBtn[i]=-1; }
    g_holdOn=0;
    g_gameExe[0]=0;
    LoadSettingsFrom(ini);
    STCheck("gear 1 comes back on the shifter",g_rowDev[0]==0&&g_rowBtn[0]==0,"");
    STCheck("gear 6 comes back",g_rowDev[5]==0&&g_rowBtn[5]==5,"");
    STCheck("reverse comes back",g_rowDev[6]==0&&g_rowBtn[6]==7,"");
    STCheck("neutral stays unbound (rest position)",g_rowBtn[7]<0,"");
    STCheck("the mode control comes back on the OTHER device",
            g_rowDev[ROW_MODEBTN]==1&&g_rowBtn[ROW_MODEBTN]==38,"");
    STCheck("the game keys come back",
            g_rowBtn[ROW_KEYUP]==0x1E&&g_rowBtn[ROW_KEYDOWN]==0x2C&&g_rowBtn[ROW_KEYMODE]==0x30,"");
    STCheck("the switch/toggle choice survives the round trip",g_holdOn==1,"");
    STCheck("the game path survives",g_gameExe[0]!=0,g_gameExe);

    char tail[128]; wsprintfA(tail,"\r\n%d failed\r\n",g_stFail); SCat(g_stReport,tail);
    char rep[MAX_PATH]; WorkPath(rep,"selftest-report.txt");
    HANDLE h=CreateFileA(rep,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h!=INVALID_HANDLE_VALUE){ DWORD w; WriteFile(h,g_stReport,(DWORD)SLen(g_stReport),&w,NULL); CloseHandle(h); }
    return g_stFail;
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR cmd,int show){
    (void)hp;
    int selftest=0;
    for(int i=0;cmd&&cmd[i];i++) if(cmd[i]=='-'&&cmd[i+1]=='-'&&cmd[i+2]=='s') selftest=1;
    GetModuleFileNameA(NULL,g_exeDir,MAX_PATH);
    { int n=SLen(g_exeDir); while(n>0&&g_exeDir[n-1]!='\\'&&g_exeDir[n-1]!='/') n--; g_exeDir[n]=0; }
    for(int i=0;i<NROW;i++){ g_rowDev[i]=-1; g_rowBtn[i]=-1; }
    /* the keys this install already uses - A / Z / B, overwritable by pressing others */
    g_rowBtn[ROW_KEYUP]=0x1E; g_rowBtn[ROW_KEYDOWN]=0x2C; g_rowBtn[ROW_KEYMODE]=0x30;

    SkinInit();
    WNDCLASSA wc; for(int i=0;i<(int)sizeof(wc);i++) ((BYTE*)&wc)[i]=0;
    wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName="MafiaGearboxSetup";
    wc.hCursor=LoadCursorA(NULL,(LPCSTR)IDC_ARROW);
    wc.hbrBackground=NULL;
    RegisterClassA(&wc);
    g_hwnd=CreateWindowExA(0,"MafiaGearboxSetup","Mafia - Gearbox H-shifter Setup   (tested on GOG v1.3)",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,714,822,NULL,NULL,hi,NULL);
    if(!g_hwnd) return 1;

    /* DirectInput after the window exists: the devices want an owner window */
    HMODULE di8=LoadLibraryA("dinput8.dll");
    DI8Create_t create=di8?(DI8Create_t)GetProcAddress(di8,"DirectInput8Create"):NULL;
    void *di=NULL;
    if(create&&create(hi,DI8_VERSION,&IID_IDirectInput8A,&di,NULL)>=0&&di){
        ((EnumDevices_t)VT(di,DI_ENUMDEVICES))(di,DI8DEVCLASS_GAMECTRL,(void*)EnumCb,NULL,DIEDFL_ATTACHEDONLY);
        g_objs[0].pguid=&GUID_XAxis; g_objs[0].dwOfs=0;
        g_objs[0].dwType=DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL;
        for(int i=0;i<NBTN;i++){ g_objs[1+i].pguid=NULL; g_objs[1+i].dwOfs=(DWORD)(4+i);
            g_objs[1+i].dwType=DIDFT_BUTTON|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL; }
        for(DWORD i=0;i<g_ndev;i++){
            g_dev[i]=NULL;
            if(((CreateDevice_t)VT(di,DI_CREATEDEVICE))(di,&g_guid[i],&g_dev[i],NULL)<0||!g_dev[i]){ g_dev[i]=NULL; continue; }
            ((SetDataFormat_t)VT(g_dev[i],DEV_SETDATAFORMAT))(g_dev[i],&g_fmt);
            ((SetCoop_t)VT(g_dev[i],DEV_SETCOOP))(g_dev[i],g_hwnd,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND);
            ((Acquire_t)VT(g_dev[i],DEV_ACQUIRE))(g_dev[i]);
        }
    }

    /* the self-test needs the controls to exist, but nobody should see a window flash by */
    if(selftest) return SelfTest();

    ShowWindow(g_hwnd,show); UpdateWindow(g_hwnd);
    LogLine("devices found:");
    for(DWORD i=0;i<g_ndev;i++){ char m[300]; wsprintfA(m,"   [%lu] %s%s",i,g_dname[i],g_dev[i]?"":"  (could not open)"); LogLine(m); }
    if(!g_ndev) LogLine("   none - is the wheel/shifter plugged in?");
    LoadSettings();
    CheckGameBuild();
    RefreshAll();

    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)>0){ TranslateMessage(&msg); DispatchMessageA(&msg); }
    return 0;
}
