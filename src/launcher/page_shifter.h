/* page_shifter.h - the H-shifter tab: the binding tool, ported out of src/spike/gearbox_gui.c.
 *
 * NOT a rewrite. Every binding row, the A/M pair, the latch-versus-button timing, the ini
 * writer and its per-path harvest are the same code they were in gearbox-setup.exe; what was
 * dropped is everything that made that file a whole PROGRAM - its WinMain, its frame, its own
 * copy of the skin, and the "find the game / install into it / rename the .asi to switch it
 * off" block, which the launcher's toggle replaces.
 *
 * Everything here is prefixed gb_. Two pages in one translation unit both calling something
 * g_val is a compile error at best and a silent alias at worst.
 *
 * THE PATH TRAP, and it is the reason this file is not a copy-paste. gearbox-setup.exe lived
 * IN gearbox hshifter setup\ and built its ini path from the folder its exe sat in. This
 * program sits in the GAME ROOT, so the path is built from g_gameDir plus the folder name. Get
 * it wrong and the bindings are written to a perfectly good file the mod never opens, with no
 * error anywhere.
 */
#ifndef ALXG_PAGE_SHIFTER_H
#define ALXG_PAGE_SHIFTER_H

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
static DIOBJDF gb_objs[1+NBTN];
static DIDF gb_fmt = { sizeof(DIDF), sizeof(DIOBJDF), DIDF_ABSAXIS, 4+NBTN, 1+NBTN, gb_objs };
typedef struct { LONG x; BYTE btn[NBTN]; } DSTATE;

#define MAXDEV 8
static GUID_ gb_guid[MAXDEV];
static char  gb_dname[MAXDEV][260];
static DWORD gb_ndev=0;
static void *gb_dev[MAXDEV];
static DSTATE gb_prev[MAXDEV];
static int gb_haveBase=0;

static void *VT(void *o,int i){ return (void*)((DWORD*)(*(DWORD*)o))[i]; }

static BOOL __stdcall gb_EnumCb(const DIDEVINST *d,void *c){
    (void)c; if(gb_ndev>=MAXDEV) return DIENUM_STOP;
    gb_guid[gb_ndev]=d->guidInstance;
    int i=0; for(;i<259&&d->tszProductName[i];i++) gb_dname[gb_ndev][i]=d->tszProductName[i];
    gb_dname[gb_ndev][i]=0; gb_ndev++; return DIENUM_CONTINUE;
}

/* ---------- the bindings ----------
 * Rows 0..8 capture DirectInput buttons from ANY attached device - the H-shifter and the
 * wheelbase are different devices and a real rig uses both. Rows 9..11 capture KEYBOARD keys:
 * the mod shifts by pressing the keys the game itself has bound, so it must know them. */
#define NROW 12
#define ROW_MODEBTN 8
#define ROW_KEYUP   9
#define ROW_KEYDOWN 10
#define ROW_KEYMODE 11
static const char *ROWNAME[NROW]={
    "Gear 1","Gear 2","Gear 3","Gear 4","Gear 5","Gear 6",
    /* Gearbox A/M mode button, per the instruction given 2026-08-05: the two buttons under this row
       are labelled "...switch A/M", so a label that does not say A/M leaves the reader joining
       them up themselves. */
    "Reverse","Neutral","Gearbox A/M mode button",
    "Game key: GEAR UP","Game key: GEAR DOWN","Game key: gearbox mode" };
/* ini key for each row, in the format the .asi reads */
static const char *ROWKEY[NROW]={
    "gear1","gear2","gear3","gear4","gear5","gear6",
    "reverse","neutral","mode_btn","gearup_dik","geardown_dik","mode_dik" };

static int gb_rowDev[NROW];     /* device index for rows 0..8, -1 = that device is not plugged in */
static int gb_rowBtn[NROW];     /* button number, or DIK scancode for rows 9..11 */
/* THE DEVICE A ROW IS BOUND TO, BY NAME, whether or not it is plugged in right now - and it is
 * the reason a row survives an unplugged wheel.
 *
 * Found 2026-08-01 by photographing this page with the wheel switched off: every gear read "click
 * to set" while gearbox.ini held `gear1=1:0`, because the load matched the saved device by name
 * against what was attached and, finding nothing, put -1 in BOTH fields. The row was not merely
 * unresolved, it was forgotten - and gb_SaveTo writes the ROWS out of this model. So one click on
 * anything (the A/M pair is enough) queued the 300 ms automatic write and a file holding eight
 * bindings came back with none.
 *
 * That is [[binder-clobbers-ini]] again, one layer along, and automatic saving is what sharpened
 * it: the old tool needed a deliberate Save press to do the same damage. The rule is the same as
 * the address-table harvest above - WHAT THE FILE ALREADY SAYS SURVIVES unless the user changed
 * that row - so the name is kept, the row still says what it is bound to, and a save re-emits it
 * with its own device slot. */
static char gb_rowDevName[NROW][64];

/* what a row is bound to: the live device when it is here, the remembered name when it is not */
static const char *gb_RowDevName(int r){
    return (gb_rowDev[r]>=0) ? gb_dname[gb_rowDev[r]] : gb_rowDevName[r];
}
static int gb_capture=-1;       /* row currently waiting for input, -1 = none */
static HWND gb_btn[NROW], gb_clr[NROW], gb_hold, gb_press;
static int  gb_holdOn=0;        /* owner-drawn, so we hold the state ourselves */
/* set while we watch how long the just-bound mode control stays down */
static int gb_watchDev=-1, gb_watchBtn=-1; static DWORD gb_watchDown=0;

/* the A/M pair is one choice shown as two controls, so neither is ever repainted alone */
static void gb_RefreshMode(void){
    if(gb_hold)  InvalidateRect(gb_hold,NULL,TRUE);
    if(gb_press) InvalidateRect(gb_press,NULL,TRUE);
}

static const char *gb_KeyName(int sc,char *buf){
    /* scancode -> a readable name, via the OS so a non-US layout still reads right */
    if(GetKeyNameTextA((LONG)(sc<<16),buf,32)>0) return buf;
    wsprintfA(buf,"scancode 0x%02X",sc); return buf;
}

static void gb_RefreshRow(int r){
    char t[320],kb[40];
    if(!gb_btn[r]) return;
    if(r>=ROW_KEYUP){
        if(gb_rowBtn[r]<0) SCpy(t,"click to set");
        else wsprintfA(t,"%s   (0x%02X)",gb_KeyName(gb_rowBtn[r],kb),gb_rowBtn[r]);
    } else if(gb_rowBtn[r]<0){
        SCpy(t, r==7 ? "none - neutral is the rest position" : "click to set");
    } else if(gb_rowDev[r]<0){
        /* bound, but that device is not attached right now. It must NOT read "click to set": that
           says the row is empty, invites a rebind, and the binding is still in the file. */
        wsprintfA(t,"button %d   -   %s   (not plugged in)",gb_rowBtn[r],gb_rowDevName[r]);
    } else {
        wsprintfA(t,"button %d   -   %s",gb_rowBtn[r],gb_dname[gb_rowDev[r]]);
    }
    if(gb_capture==r) SCpy(t, r>=ROW_KEYUP ? "press a KEY now..." : "press a button now...");
    SetWindowTextA(gb_btn[r],t);
}
static void gb_RefreshAll(void){ for(int i=0;i<NROW;i++) gb_RefreshRow(i); }

/* ---------- the settings file ----------
 * THE PATH. In the game root, in the mod's own folder - see the header of this file. */
static void gb_IniPath(char *out){
    SCpy(out,g_gameDir); SCat(out,GEARBOX_DIR); SCat(out,"\\gearbox.ini");
}

static void AppendStr(char *buf,const char *s){ SCat(buf,s); }

/* ---------- keys the mod reads that this GUI does not edit -----------------------------------
 * The address table and the shift timings used to be written back from these built-in constants
 * on EVERY save. That silently destroyed a hand-maintained config twice (2026-07-25), and the
 * sharp edge is that it flipped `closed_loop` to 1 at the same time - with closed_loop=0 the
 * very same wrong `gameptr` shifts fine, because open loop never reads the gear back. So the
 * wrong address only became load-bearing BECAUSE the tool "fixed" the other key. Nothing said a
 * word: the mod simply did nothing.
 *
 * The rule: these constants are the fallback for a FRESH file only. Whatever the file we are
 * about to overwrite already says wins, and it is harvested from THAT PATH rather than kept in a
 * global. Do not "simplify" the harvest out on the way past - it is the fix for the defect, and
 * a port is exactly when a fix like this gets lost. */
enum { P_ENABLE,P_CLOSED,P_HOLDMS,P_GAPMS,P_RETRYMS,P_NEUTMS,P_GAMEPTR,P_GEAROFS,P_MODEOFS,NPRES };
static const char *PKEY[NPRES]={
    "enable","closed_loop","hold_ms","gap_ms","retry_ms","neutral_delay_ms",
    "gameptr","gear_ofs","mode_ofs" };
/* the GOG build's numbers; the timings are measured, see neutral_delay_ms below */
static const char *PDEF[NPRES]={
    "1","1","40","60","1200","1100",
    "0x63788C","0x5D0","0x53C" };

static void gb_SaveTo(const char *path){
    char buf[8192]; buf[0]=0;
    char pv[NPRES][32]; int kept=0;
    for(int i=0;i<NPRES;i++){
        char v[32]; GetPrivateProfileStringA("shift",PKEY[i],PDEF[i],v,sizeof(v),path);
        SCpy(pv[i], v[0]?v:PDEF[i]);                 /* an empty "gameptr=" is not an answer */
        if(i>=P_GAMEPTR&&!SEq(pv[i],PDEF[i])) kept=1;
    }
    /* AND SAY IT WHEN THE MODULE IS SWITCHED OFF. The harvest above faithfully preserves whatever
       the file already says, which is the whole point of it - but `enable` has no control
       anywhere in this window, so a 0 that arrived in the shipped file could never be undone from
       here and every binding written afterwards went into a module that does nothing. That is
       what happened on 2026-08-12: every gear bound, the A/M control identified, and the gearbox
       inert. The shipped default is 1 now; this line is so the next way it reaches 0 is loud. */
    if(!SEq(pv[P_ENABLE],"1")){
        char m[200];
        wsprintfA(m,"WARNING: this gearbox.ini says enable=%s - the module will load and do "
                    "nothing. Set enable=1 in %s",pv[P_ENABLE],path);
        LogLine(m);
    }
    /* say it out loud - silence is what made the original defect cost two evenings */
    if(kept){
        char m[400];
        wsprintfA(m,"kept the address table already in this file: gameptr=%s gear_ofs=%s mode_ofs=%s",
                  pv[P_GAMEPTR],pv[P_GEAROFS],pv[P_MODEOFS]);
        LogLine(m);
    }
    AppendStr(buf,"; Mafia gearbox - written by " BRAND_NAME ", H-shifter tab\r\n"
                  "; lives in <game>\\" GEARBOX_DIR "\\ with the rest of the gearbox files\r\n\r\n[hook]\r\n");
    /* Device slots, in order of first use, and BY NAME rather than by device index. The index is
       only meaningful while a device is attached; the name is what the file stores and what the
       mod matches on. Going through the name is what lets a row bound to an unplugged wheel keep
       its slot instead of vanishing from the file - see gb_rowDevName. */
    char slot[MAXDEV][64]; int nslot=0;
    for(int r=0;r<=ROW_MODEBTN;r++){
        const char *nm=gb_RowDevName(r);
        if(gb_rowBtn[r]<0||!nm[0]) continue;
        int seen=0; for(int s=0;s<nslot;s++) if(SEq(slot[s],nm)) seen=1;
        if(!seen&&nslot<4) SCpy(slot[nslot++],nm);
    }
    char line[512];
    for(int s=0;s<nslot;s++){
        wsprintfA(line,"device%d=%s\r\nsuppress%d=",s+1,slot[s],s+1); AppendStr(buf,line);
        int first=1;
        for(int r=0;r<=ROW_MODEBTN;r++) if(gb_rowBtn[r]>=0&&SEq(gb_RowDevName(r),slot[s])){
            wsprintfA(line,"%s%d",first?"":",",gb_rowBtn[r]); AppendStr(buf,line); first=0; }
        AppendStr(buf,"\r\n");
    }
    wsprintfA(line,"\r\n[shift]\r\nenable=%s\r\nclosed_loop=%s\r\n\r\n",pv[P_ENABLE],pv[P_CLOSED]);
    AppendStr(buf,line);
    for(int r=0;r<=ROW_MODEBTN;r++){
        if(gb_rowBtn[r]<0||!gb_RowDevName(r)[0]){
            if(r==7) AppendStr(buf,"; neutral is the lever's rest position - no button\r\nneutral=-1\r\n");
            continue;
        }
        int s=0; for(int i=0;i<nslot;i++) if(SEq(slot[i],gb_RowDevName(r))) s=i;
        wsprintfA(line,"%s=%d:%d\r\n",ROWKEY[r],s+1,gb_rowBtn[r]); AppendStr(buf,line);
    }
    AppendStr(buf,"\r\n");
    for(int r=ROW_KEYUP;r<NROW;r++){
        wsprintfA(line,"%s=0x%02X\r\n",ROWKEY[r],gb_rowBtn[r]>=0?gb_rowBtn[r]:0); AppendStr(buf,line);
    }
    wsprintfA(line,"hold_ms=%s\r\ngap_ms=%s\r\nretry_ms=%s\r\n",pv[P_HOLDMS],pv[P_GAPMS],pv[P_RETRYMS]);
    AppendStr(buf,line);
    /* measured on a real drive: gate-to-gate takes 0.22-0.94 s, a deliberate neutral 1.1 s+ */
    wsprintfA(line,"neutral_delay_ms=%s\r\n",pv[P_NEUTMS]); AppendStr(buf,line);
    /* this one silently went missing once: the checkbox was ticked, the file said nothing, and
       the mod ran in button mode against a latching switch - one toggle in, one toggle out */
    wsprintfA(line,"mode_hold=%d\r\n",gb_holdOn?1:0); AppendStr(buf,line);
    /* NOT relabelled "GOG" - after a harvest these may be another build's numbers, and a comment
       that names the wrong build is how a hand-edited file gets "corrected" back to broken */
    wsprintfA(line,"\r\n; address table - edit by hand for a non-GOG build; this tool keeps what it finds\r\n"
                   "gameptr=%s\r\ngear_ofs=%s\r\nmode_ofs=%s\r\n",
              pv[P_GAMEPTR],pv[P_GEAROFS],pv[P_MODEOFS]);
    AppendStr(buf,line);

    HANDLE h=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){ char m[400]; wsprintfA(m,"could NOT write %s",path); LogLine(m); return; }
    DWORD w; WriteFile(h,buf,(DWORD)SLen(buf),&w,NULL); CloseHandle(h);
    char m[400]; wsprintfA(m,"saved %s",path); LogLine(m);
}

/* what the Save button and the self test both call */
static void ShifterSaveIni(void){
    char p[MAX_PATH],dir[MAX_PATH];
    /* Parent first: GEARBOX_DIR hangs under "ALXG mods\" since 2026-08-07 and CreateDirectory
       makes one level only, so without this line the ini goes nowhere on a fresh folder. */
    SCpy(dir,g_gameDir); SCat(dir,ALXG_DIR);   CreateDirectoryA(dir,NULL);
    SCpy(dir,g_gameDir); SCat(dir,GEARBOX_DIR);
    CreateDirectoryA(dir,NULL);      /* the mod's folder may not be there yet */
    gb_IniPath(p);
    gb_SaveTo(p);
    PageSaved(LTAB_SHIFTER);
}

/* what the shared Load button calls - the mirror of ShifterSaveIni */
static void gb_LoadFrom(const char *path);
static void ShifterLoadIni(void){
    char p[MAX_PATH]; gb_IniPath(p);
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES){
        LogLine("no gearbox.ini in the gearbox folder yet - switch the mod on, or save one");
        return;
    }
    gb_LoadFrom(p);
    gb_RefreshAll();
    gb_RefreshMode();
    PageSaved(LTAB_SHIFTER);
    LogLine("bindings reloaded from gearbox.ini");
}

static void gb_LoadFrom(const char *path){
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
            for(;v[i];i++){
                int d=-1;
                if(v[i]>='0'&&v[i]<='9') d=v[i]-'0';
                else if(v[i]>='a'&&v[i]<='f') d=v[i]-'a'+10;
                else if(v[i]>='A'&&v[i]<='F') d=v[i]-'A'+10;
                if(d<0) break;
                val=val*16+d;
            }
            gb_rowBtn[r]=val;
            continue;
        }
        /* "slot:button", and slot 0 or a missing device means the row is unset */
        int slot=0,btn=0,i=0;
        for(;v[i]>='0'&&v[i]<='9';i++) slot=slot*10+(v[i]-'0');
        if(v[i]==':'){ i++; for(;v[i]>='0'&&v[i]<='9';i++) btn=btn*10+(v[i]-'0'); }
        else { gb_rowDev[r]=-1; gb_rowBtn[r]=-1; continue; }
        if(slot<1||slot>nnames){ gb_rowDev[r]=-1; gb_rowBtn[r]=-1; continue; }
        /* match the saved device BY NAME against what is plugged in now: a slot number means
           nothing if the user unplugged something since */
        int dev=-1;
        for(DWORD dd=0;dd<gb_ndev;dd++) if(SEq(gb_dname[dd],names[slot-1])) dev=(int)dd;
        /* The BUTTON and the NAME are kept whether or not the device answered. An absent device
           makes the row unusable until it comes back - it does not make the binding untrue. */
        gb_rowDev[r]=dev; gb_rowBtn[r]=btn; SCpy(gb_rowDevName[r],names[slot-1]);
    }
    gb_holdOn=GetPrivateProfileIntA("shift","mode_hold",0,path)?1:0;
}

/* ---------- polling ---------- */
static void gb_Poll(DSTATE *out){
    for(DWORD i=0;i<gb_ndev;i++){
        for(int k=0;k<(int)sizeof(DSTATE);k++) ((BYTE*)&out[i])[k]=0;
        if(!gb_dev[i]) continue;
        ((Poll_t)VT(gb_dev[i],DEV_POLL))(gb_dev[i]);
        if(((GetState_t)VT(gb_dev[i],DEV_GETSTATE))(gb_dev[i],sizeof(DSTATE),&out[i])<0)
            ((Acquire_t)VT(gb_dev[i],DEV_ACQUIRE))(gb_dev[i]);
    }
}

static void PageShifterTimer(void){
    DSTATE cur[MAXDEV]; gb_Poll(cur);
    if(!gb_haveBase){ for(DWORD i=0;i<gb_ndev;i++) gb_prev[i]=cur[i]; gb_haveBase=1; return; }

    if(gb_capture>=0&&gb_capture<ROW_KEYUP){
        for(DWORD i=0;i<gb_ndev;i++){
            if(!gb_dev[i]) continue;
            for(int b=0;b<NBTN;b++){
                int now=(cur[i].btn[b]&0x80)!=0, was=(gb_prev[i].btn[b]&0x80)!=0;
                if(now&&!was){
                    int r=gb_capture; gb_capture=-1;
                    gb_rowDev[r]=(int)i; gb_rowBtn[r]=b;
                    SCpy(gb_rowDevName[r],gb_dname[i]);   /* the name is what gets written */
                    if(r==ROW_MODEBTN){ gb_watchDev=(int)i; gb_watchBtn=b; gb_watchDown=GetTickCount(); }
                    char m[400]; wsprintfA(m,"%s = button %d on %s",ROWNAME[r],b,gb_dname[i]);
                    LogLine(m); gb_RefreshRow(r); PageDirty(LTAB_SHIFTER);
                    for(DWORD k=0;k<gb_ndev;k++) gb_prev[k]=cur[k];
                    return;
                }
            }
        }
    } else if(gb_capture>=ROW_KEYUP){
        static int prevKey[256]; static int keyBase=0;
        if(!keyBase){ for(int v=0;v<256;v++) prevKey[v]=(GetAsyncKeyState(v)&0x8000)!=0; keyBase=1; }
        for(int vk=0x08;vk<=0xFE;vk++){
            if(vk==VK_LBUTTON||vk==VK_RBUTTON||vk==VK_MBUTTON) continue;
            int now=(GetAsyncKeyState(vk)&0x8000)!=0;
            if(now&&!prevKey[vk]){
                UINT sc=MapVirtualKeyA((UINT)vk,0);
                int r=gb_capture; gb_capture=-1; keyBase=0;
                if(sc){ gb_rowBtn[r]=(int)sc;
                    char m[400],kb[40]; wsprintfA(m,"%s = %s (scancode 0x%02X)",ROWNAME[r],gb_KeyName((int)sc,kb),sc);
                    LogLine(m); }
                gb_RefreshRow(r); PageDirty(LTAB_SHIFTER);
                for(int v=0;v<256;v++) prevKey[v]=0;
                for(DWORD k=0;k<gb_ndev;k++) gb_prev[k]=cur[k];
                return;
            }
            prevKey[vk]=now;
        }
    }
    /* A latching switch stays down; a button springs back. One second of daylight between the
       two is plenty, and getting this wrong INVERTS the gearbox mode, so it is worth deciding
       for the user rather than asking them to know.
       ONE threshold, named, used by both branches. They used to be 1000 on release and 3000
       while still down, so a switch held for two seconds was called nothing at all until it was
       let go - settled 2026-08-12: hold check set to one second, not three. Two numbers for one
       question is also how they drift apart. */
    #define GB_SWITCH_MS 1000u
    if(gb_watchDev>=0&&gb_watchBtn>=0){
        int down=(cur[gb_watchDev].btn[gb_watchBtn]&0x80)!=0;
        DWORD held=GetTickCount()-gb_watchDown;
        if(!down){
            char m[200];
            if(held>GB_SWITCH_MS){
                gb_holdOn=1;
                wsprintfA(m,"that control stayed down %lu ms - it is a switch, so 'Hold to switch A/M' is now chosen",held);
            } else {
                /* it also has to be SET, not merely reported: a profile loaded with hold=1 used
                   to survive this branch, so the log said button and the file still said switch */
                gb_holdOn=0;
                wsprintfA(m,"that control sprang back after %lu ms - it is a button, so 'One press to switch A/M' is now chosen",held);
            }
            gb_RefreshMode(); PageDirty(LTAB_SHIFTER);
            LogLine(m);
            gb_watchDev=-1; gb_watchBtn=-1;
        } else if(held>GB_SWITCH_MS){
            gb_holdOn=1; gb_RefreshMode(); PageDirty(LTAB_SHIFTER);
            LogLine("that control is still down after 1 s - it is a switch, so 'Hold to switch A/M' is now chosen");
            gb_watchDev=-1; gb_watchBtn=-1;
        }
    }
    for(DWORD i=0;i<gb_ndev;i++) gb_prev[i]=cur[i];
}

/* ---------- the page ---------- */
#define GB_ID_BTN0  4000
#define GB_ID_CLR0  4100
#define GB_ID_SAVE  4200
#define GB_ID_HOLD  4202
#define GB_ID_PRESS 4203
#define GB_TIMER    1

static int gb_pageH;

/* ---- THIS PAGE KEEPS ITS OWN WIDTH ----------------------------------------------------------
 * Settled 2026-08-01, looking at it in the launcher: no need to make the shifter this wide. The
 * shifter had a decent design. Keep it that way. It would be centred and adapted a little, but there is no need to
 * stretch it across the full width.
 *
 * The window is 1320 px wide because the FORCE FEEDBACK page needs two columns. This page has one
 * column of bindings and does not, so the rows that were laid out against `WINW` stretched into a
 * 1000 px field holding the words "button 0" - a control ten times the size of its own content.
 *
 * So the page is drawn at the width it was designed at - `gearbox_gui.c`'s client area, 714 px -
 * and CENTRED in whatever the frame happens to be. Every x below is `SH_X + <the original x>`,
 * which is why the offsets look familiar: they are the spike tool's, unchanged. If the frame ever
 * gets wider, this page moves rather than stretches. */
#define SH_W 714                       /* GBWINW in src/spike/gearbox_gui.c */
#define SH_X ((WINW-SH_W)/2)
#define SH_L (SH_X+26)                 /* the page's own left margin */
#define SH_IW (SH_W-52)                /* a full-width rule inside that margin */

static void PageShifterCreate(HWND h){
    int y;
    /* before PageHeader: the header line and the banner are drawn at the page's own margin */
    g_pageX[LTAB_SHIFTER]=SH_L;
    g_pageW[LTAB_SHIFTER]=SH_IW;
    PageHeader(h,LTAB_SHIFTER);
    y=PageBanner(h,LTAB_SHIFTER,PAGE_TOP);
    MkText(h,"Click a binding, then press the button or key you want. Click again to change it.",
           SH_L,y,SH_IW,20,0); y+=26;
    for(int i=0;i<NROW;i++){
        /* The mode row gets its own fenced block. The A/M pair belongs to THIS row alone -
           floating under the gear rows it read as a global setting - so a rule above, a rule
           below and some air make the scope unmistakable. */
        if(i==ROW_MODEBTN){
            y+=8;
            CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,SH_L,y,SH_IW,2,h,NULL,NULL,NULL);
            y+=10;
        }
        if(i==ROW_KEYUP){
            y+=8;
            CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,SH_L,y,SH_IW,2,h,NULL,NULL,NULL);
            y+=10;
            MkText(h,"The keys the GAME itself uses - set them in Mafia's Options, then press them here:",
                   SH_L,y,SH_IW,20,0); y+=24;
        }
        MkText(h,ROWNAME[i],SH_L,y+4,170,20,0);
        gb_btn[i]=MkBtn(h,"click to set",SH_X+200,y,SH_W-200-100-26,24,GB_ID_BTN0+i);
        gb_clr[i]=MkBtn(h,"clear",SH_X+SH_W-26-90,y,90,24,GB_ID_CLR0+i);
        if(i==ROW_MODEBTN){
            /* Users often do not know whether their own shifter latches or springs back, so each
               option says what the control DOES, and the tool decides for them by timing it when
               it is bound. A ticked box said neither which two things were on offer nor which
               one was live, so it is two buttons: the chosen one is green, the other plain
               black, and they are visibly one-or-the-other. */
            gb_hold =MkBtn(h,"Hold to switch A/M",SH_X+200,y+28,200,24,GB_ID_HOLD);
            gb_press=MkBtn(h,"One press to switch A/M",SH_X+410,y+28,236,24,GB_ID_PRESS);
            y+=32;
        }
        y+=28;
    }
    y+=14;
    /* No Apply and no Apply & Launch. The toggle at the top of this page IS the install, and
       saving IS applying now that the program lives in the game - what is left is the shared
       save row, which is identical on every tab. */
    y=PageSaveRow(h,LTAB_SHIFTER,y,ShifterSaveIni,ShifterLoadIni);
    y+=10;
    gb_pageH=y;
}

/* Returns 1 when it drew the item. Only the ids this page owns. */
static int PageShifterDraw(DRAWITEMSTRUCT *d,int id){
    char txt[320]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
    RECT r=d->rcItem;
    int pressed=(d->itemState&ODS_SELECTED)!=0;
    int off=!IsWindowEnabled(d->hwndItem);
    SetBkMode(d->hDC,TRANSPARENT);

    if(id>=GB_ID_BTN0&&id<GB_ID_BTN0+NROW){
        /* a binding sits in a thin-ruled white field, like Mafia's own controls screen */
        HBRUSH bf=CreateSolidBrush(off?RGB(238,236,230):FIELD);
        FillRect(d->hDC,&r,bf); DeleteObject(bf);
        HPEN pen=CreatePen(PS_SOLID,1,off?RGB(180,174,164):RGB(60,54,48));
        HGDIOBJ op=SelectObject(d->hDC,pen);
        HGDIOBJ ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);
        SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        int capturing=(gb_capture==id-GB_ID_BTN0);
        if(!off&&(capturing||(d->itemState&ODS_FOCUS))) RedArrow(d->hDC,r.left-12,(r.top+r.bottom)/2);
        HGDIOBJ of=SelectObject(d->hDC,g_fField);
        SetTextColor(d->hDC,off?INK_OFF:capturing?MAFIA_RED:INK);
        RECT t=r; t.left+=10;
        DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(d->hDC,of);
        return 1;
    }
    if(id>=GB_ID_CLR0&&id<GB_ID_CLR0+NROW){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,0);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id==GB_ID_HOLD||id==GB_ID_PRESS){
        /* one choice, two buttons: the live one is green, the other plain black. A tick box
           showed neither what the alternative was nor which of the two was in force. */
        int chosen=(id==GB_ID_HOLD)?gb_holdOn:!gb_holdOn;
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,chosen?g_fAction:g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,chosen&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    return 0;
}

static int PageShifterCommand(int id){
    if(id>=GB_ID_BTN0&&id<GB_ID_BTN0+NROW){
        int prev=gb_capture; gb_capture=id-GB_ID_BTN0; gb_haveBase=0;
        if(prev>=0) gb_RefreshRow(prev);
        gb_RefreshRow(gb_capture);
        return 1;
    }
    if(id>=GB_ID_CLR0&&id<GB_ID_CLR0+NROW){
        /* CLEARING is the one thing that forgets a row on purpose, so the remembered name goes
           too - otherwise the next save would put the binding back from it. */
        int r=id-GB_ID_CLR0; gb_rowDev[r]=-1; gb_rowBtn[r]=-1; gb_rowDevName[r][0]=0;
        if(gb_capture==r) gb_capture=-1;
        gb_RefreshRow(r);
        PageDirty(LTAB_SHIFTER);
        char m[200]; wsprintfA(m,"%s cleared",ROWNAME[r]); LogLine(m);
        return 1;
    }
    /* each button SETS its own answer - it does not toggle, so clicking the live one twice
       cannot leave the pair showing the opposite of what it says */
    if(id==GB_ID_HOLD){  gb_holdOn=1; gb_RefreshMode(); PageDirty(LTAB_SHIFTER);
                         LogLine("gearbox mode control: HOLD to switch A/M"); return 1; }
    if(id==GB_ID_PRESS){ gb_holdOn=0; gb_RefreshMode(); PageDirty(LTAB_SHIFTER);
                         LogLine("gearbox mode control: ONE PRESS to switch A/M"); return 1; }
    return 0;
}

/* DirectInput wants an owner window, so this runs after the frame exists. */
static void PageShifterOpenDevices(HWND owner){
    HMODULE di8=LoadSystemDll("dinput8.dll");
    DI8Create_t create=di8?(DI8Create_t)GetProcAddress(di8,"DirectInput8Create"):NULL;
    void *di=NULL;
    for(int i=0;i<NROW;i++){ gb_rowDev[i]=-1; gb_rowBtn[i]=-1; gb_rowDevName[i][0]=0; }
    /* the keys this install already uses - A / Z / M, overwritable by pressing others.
       0x32 is DIK_M. It was 0x30, DIK_B, until a fresh install on 2026-08-07 showed:
       By default the button for switching Automatic or Manual mode for the H-shifter is button
       M, not button B. B is not the game's gearbox key, so a bound mode button pressed
       something the game ignores - a failure with no symptom at all. */
    gb_rowBtn[ROW_KEYUP]=0x1E; gb_rowBtn[ROW_KEYDOWN]=0x2C; gb_rowBtn[ROW_KEYMODE]=0x32;

    if(create&&create(GetModuleHandleA(NULL),DI8_VERSION,&IID_IDirectInput8A,&di,NULL)>=0&&di){
        ((EnumDevices_t)VT(di,DI_ENUMDEVICES))(di,DI8DEVCLASS_GAMECTRL,(void*)gb_EnumCb,NULL,DIEDFL_ATTACHEDONLY);
        gb_objs[0].pguid=&GUID_XAxis; gb_objs[0].dwOfs=0;
        gb_objs[0].dwType=DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL;
        for(int i=0;i<NBTN;i++){ gb_objs[1+i].pguid=NULL; gb_objs[1+i].dwOfs=(DWORD)(4+i);
            gb_objs[1+i].dwType=DIDFT_BUTTON|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL; }
        for(DWORD i=0;i<gb_ndev;i++){
            gb_dev[i]=NULL;
            if(((CreateDevice_t)VT(di,DI_CREATEDEVICE))(di,&gb_guid[i],&gb_dev[i],NULL)<0||!gb_dev[i]){ gb_dev[i]=NULL; continue; }
            ((SetDataFormat_t)VT(gb_dev[i],DEV_SETDATAFORMAT))(gb_dev[i],&gb_fmt);
            ((SetCoop_t)VT(gb_dev[i],DEV_SETCOOP))(gb_dev[i],owner,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND);
            ((Acquire_t)VT(gb_dev[i],DEV_ACQUIRE))(gb_dev[i]);
        }
    }
    LogLine("devices found:");
    for(DWORD i=0;i<gb_ndev;i++){
        char m[300]; wsprintfA(m,"   [%lu] %s%s",i,gb_dname[i],gb_dev[i]?"":"  (could not open)");
        LogLine(m);
    }
    if(!gb_ndev) LogLine("   none - is the wheel/shifter plugged in?");

    { char p[MAX_PATH]; gb_IniPath(p); gb_LoadFrom(p); }
    gb_RefreshAll();
    gb_RefreshMode();
}

/* ---------- what the self test drives ---------------------------------------------------------
 * A rig like the reference one - six gears and reverse on the shifter, the mode switch on the wheelbase -
 * built from NAMES and never from device indices, so the checks mean the same thing on a machine
 * with no wheel attached as on the reference rig. That is not a convenience: the failure these guard is
 * precisely the device is not there, and a rig built out of live indices cannot express it.
 *
 * Lives here rather than in launcher.c because it touches this page's model, and a test reaching
 * into another file's statics is how a rename passes every check and breaks the tool. */
static void ShifterSelfTestRig(void){
    int i;
    for(i=0;i<NROW;i++){ gb_rowDev[i]=-1; gb_rowBtn[i]=-1; gb_rowDevName[i][0]=0; }
    for(i=0;i<6;i++){ gb_rowBtn[i]=i; SCpy(gb_rowDevName[i],"ALXG-TEST-SHIFTER"); }
    gb_rowBtn[6]=7; SCpy(gb_rowDevName[6],"ALXG-TEST-SHIFTER");      /* reverse */
    /* row 7, neutral, stays unbound on purpose - it is the lever's rest position */
    gb_rowBtn[ROW_MODEBTN]=38; SCpy(gb_rowDevName[ROW_MODEBTN],"ALXG-TEST-WHEELBASE");
    gb_rowBtn[ROW_KEYUP]=0x1E; gb_rowBtn[ROW_KEYDOWN]=0x2C; gb_rowBtn[ROW_KEYMODE]=0x32;
    gb_holdOn=1;
}
static void ShifterSelfTestClear(void){
    int i;
    for(i=0;i<NROW;i++){ gb_rowDev[i]=-1; gb_rowBtn[i]=-1; gb_rowDevName[i][0]=0; }
    gb_holdOn=0;
}
/* `name` NULL means the row must be unbound. */
static int ShifterRowIs(int r,const char *name,int btn){
    if(!name) return gb_rowBtn[r]<0;
    return gb_rowBtn[r]==btn&&SEq(gb_RowDevName(r),name);
}
static int ShifterKeysAre(int up,int down,int mode){
    return gb_rowBtn[ROW_KEYUP]==up&&gb_rowBtn[ROW_KEYDOWN]==down&&gb_rowBtn[ROW_KEYMODE]==mode;
}

#endif /* ALXG_PAGE_SHIFTER_H */
