/* gearbox_hook8.c - SPIKE v3. Everything v7 did, PLUS the translation layer:
 * an H-pattern lever position becomes the game's own sequential shift.
 *
 * v7 proved the first half - specific wheelbase buttons can be hidden from the game
 * (344 measured suppressions). It could not shift anything, because nothing emitted a
 * gear change. This file adds that, via the route chosen on 2026-07-24:
 *
 *   KEY INJECTION FIRST, action injection later as an upgrade.
 *
 * WHY A KEY. LS3DF reads the keyboard through DINPUT8 (Game.exe reads the wheel through
 * IJoy on DirectInput 7 - a different stack, already confirmed). The
 * shared-vtable patch that proved suppression on the DX7 joystick works identically on the
 * DI8 keyboard: create our own keyboard device purely to obtain the class vtable, patch
 * GetDeviceState, and OR the bit of the key the user has bound to GEARUP/GEARDOWN into the
 * 256-byte buffer the game is about to read. Setting a bit is the same code path as the
 * zeroing that is already proven. The alternative - poking action id 0x2D into the input
 * manager - has a better UX but its injection point is not yet derived.
 *
 * THE LOOP IS CLOSED. The gear is readable at [car+0x58]+0x5D0 on GOG (confirmed: 13 gear
 * changes over a 124 s drive, every one commanded, zero self-shifts). So this does not
 * count its own shifts like the AHK script does - it reads the game's gear, emits ONE
 * sequential step toward the lever position, and waits for the gear to actually move. A car
 * with fewer gears than the lever simply stops changing: the timeout is recorded as that
 * car's ceiling and further up-shifts are refused instead of desynchronising.
 *
 * The game's own MANUAL mode is what this drives ([car+0x58]+0x53C, 0=manual - measured, not
 * assumed: on the first drive every key-commanded shift landed at 0 and the single failure at
 * 1). The flag is only ever READ. When the box is in automatic and the lever asks for a gear,
 * the mode KEY is pressed once per vehicle rather than the flag being written - the game does
 * its own bookkeeping and we do not have to guess at it.
 *
 * Config: <game>\gearbox hshifter setup\gearbox.ini - written by gearbox-setup.exe, which sits
 * in that same folder with everything else this module owns.
 *     [hook]
 *     device=SIMAGIC
 *     suppress=6,7,2,3,1,0,4,5,38
 *     [shift]
 *     enable=1
 *     gear1=6 ... gear6=0, reverse=4, neutral=5   (-1 = neutral is the rest position)
 *     gearup_dik=0x1E     ; DIK of the key bound to GEARUP in the game's options (A)
 *     geardown_dik=0x2C   ; DIK for GEARDOWN (Z)
 *     mode_dik=0x32       ; optional, MOTORSWITCH / manual-auto toggle (M)
 *     mode_btn=38         ; wheel button that should fire mode_dik
 *     hold_ms=40  gap_ms=60  closed_loop=1
 *     gameptr=0x63788C  gear_ofs=0x5D0  mode_ofs=0x53C     (GOG defaults)
 *
 * Log: gearbox_hook.bin, decode with src/readhook.py.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *memset(void *d,int c,size_t n){ BYTE*p=(BYTE*)d; while(n--)*p++=(BYTE)c; return d; }
void *memcpy(void *d,const void *s,size_t n){ BYTE*p=(BYTE*)d; const BYTE*q=(const BYTE*)s; while(n--)*p++=*q++; return d; }
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }

#define DIDFT_PSHBUTTON 0x00000004
#define DIDFT_TGLBUTTON 0x00000008
#define DIDFT_BUTTON    (DIDFT_PSHBUTTON|DIDFT_TGLBUTTON)
#define DIDFT_GETINSTANCE(t) (((t)>>8)&0xFFFF)

/* IDirectInput(2/7) root slots */
#define DI_CREATEDEVICE    3
#define DI_CREATEDEVICEEX  9
/* IDirectInputDevice(2) slots - same numbering as DX8 */
#define DEV_QI             0
#define DEV_GETSTATE       9
#define DEV_GETDATA        10
#define DEV_SETDATAFORMAT  11
#define DEV_GETDEVINFO     15

typedef struct { DWORD a; WORD b,c; BYTE d[8]; } GUID_;
typedef struct {
    DWORD dwSize; GUID_ guidInstance, guidProduct; DWORD dwDevType;
    CHAR tszInstanceName[260], tszProductName[260]; GUID_ guidFFDriver;
    WORD wUsagePage, wUsage;
} DIDEVINST;
typedef struct { const GUID_ *pguid; DWORD dwOfs,dwType,dwFlags; } DIOBJDF;
typedef struct { DWORD dwSize,dwObjSize,dwFlags,dwDataSize,dwNumObjs; DIOBJDF *rgodf; } DIDF;
typedef struct { DWORD dwOfs,dwData,dwTimeStamp,dwSequence; UINT_PTR uAppData; } DIDEVOBJDATA;

typedef HRESULT (WINAPI *DICreateEx_t)(HINSTANCE,DWORD,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *CreateDevice_t)(void *,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *CreateDeviceEx_t)(void *,const GUID_ *,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *QI_t)(void *,const GUID_ *,void **);
typedef HRESULT (__stdcall *GetDevInfo_t)(void *,DIDEVINST *);
typedef HRESULT (__stdcall *SetDataFormat_t)(void *,const DIDF *);
typedef HRESULT (__stdcall *GetState_t)(void *,DWORD,void *);
typedef HRESULT (__stdcall *GetData_t)(void *,DWORD,DIDEVOBJDATA *,DWORD *,DWORD);

static const GUID_ IID_IDirectInput7A =
    {0x9A4CB684,0x236D,0x11D3,{0x8E,0x9D,0x00,0xC0,0xF0,0x3F,0x15,0x00}};
static const GUID_ IID_IDirectInput2A =
    {0x5944E662,0xAA8A,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID_ IID_IDirectInputDevice2A =
    {0x5944E682,0xAA8A,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};

typedef struct { DWORD tick,code,a,b,c,d; } REC;
static HANDLE g_log=INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs; static volatile LONG g_csReady=0;
static void L(DWORD code,DWORD a,DWORD b,DWORD c,DWORD d){
    if(g_log==INVALID_HANDLE_VALUE||!g_csReady) return;
    REC r; r.tick=GetTickCount(); r.code=code; r.a=a; r.b=b; r.c=c; r.d=d; DWORD w;
    EnterCriticalSection(&g_cs); WriteFile(g_log,&r,sizeof(r),&w,NULL); LeaveCriticalSection(&g_cs);
}

static char g_dir[MAX_PATH];
static void InitDir(void){
    HMODULE self=NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)&InitDir,&self);
    DWORD n=GetModuleFileNameA(self,g_dir,MAX_PATH);
    while(n>0&&g_dir[n-1]!='\\'&&g_dir[n-1]!='/') n--; g_dir[n]=0;
}
static void GamePath(char *o,const char *b){ int i=0,j=0; while(g_dir[i]){o[i]=g_dir[i];i++;} while(b[j]){o[i+j]=b[j];j++;} o[i+j]=0; }
/* EVERY gearbox file except this .asi lives in one folder inside the game - the setup tool,
   the settings and this log. The .asi itself cannot move: Ultimate ASI Loader only scans the
   game directory plus `scripts\` and `plugins\`, so a custom folder is never read. */
/* Under "ALXG mods\" since 2026-08-07 - one folder of ours in a game directory, not three.
   This string and install_core.h's GEARBOX_DIR are the same path spelled twice, in two
   programs that cannot include each other's headers; they move together or the module reads a
   file nobody writes. */
#define GBDIR "ALXG mods\\gearbox hshifter setup"
static void GearboxDir(char *o){ GamePath(o,GBDIR); }
/* Settings, written by gearbox-setup.exe sitting in that same folder. One location only: the
   development-era names (`mafia_gearbox_hook.ini`, `mafia_gearbox\gearbox.ini`) are gone, and
   keeping a fallback to them would only let a stale file no player should have quietly win. */
static void IniPath(char *o){ GamePath(o,GBDIR "\\gearbox.ini"); }

/* ini readers. GetPrivateProfileIntA cannot read "0x1E", and every DIK in the config is
   naturally written in hex, so the numeric reader goes through a string. */
static int ParseNum(const char *s,int dflt){
    int i=0,neg=0,v=0,any=0;
    while(s[i]==' '||s[i]=='\t') i++;
    if(s[i]=='-'){ neg=1; i++; }
    if(s[i]=='0'&&(s[i+1]=='x'||s[i+1]=='X')){
        i+=2;
        for(;;i++){ char c=s[i]; int d;
            if(c>='0'&&c<='9') d=c-'0'; else if(c>='a'&&c<='f') d=c-'a'+10;
            else if(c>='A'&&c<='F') d=c-'A'+10; else break;
            v=v*16+d; any=1; }
    } else {
        for(;;i++){ char c=s[i]; if(c<'0'||c>'9') break; v=v*10+(c-'0'); any=1; }
    }
    if(!any) return dflt;
    return neg?-v:v;
}
static int IniNum(const char *sec,const char *key,int dflt){
    char ini[MAX_PATH],buf[64]; IniPath(ini);
    GetPrivateProfileStringA(sec,key,"",buf,sizeof(buf),ini);
    if(!buf[0]) return dflt;
    return ParseNum(buf,dflt);
}

/* UP TO FOUR DEVICES, because a real rig is not one box. The H-shifter is its own
   DirectInput device (ODDOR-GEAR) while the gearbox-mode button lives on the wheelbase, so a
   single `device=` could see the gears or the mode button but never both. Every binding is
   therefore `<device index>:<button>`, and each device carries its own suppress list. */
#define MAXDEVCFG 4
static char g_devSubstr[MAXDEVCFG][64];
static BYTE g_sup[MAXDEVCFG][128]; static int g_nSup=0, g_nDevCfg=0;

/* ---- shift configuration ---------------------------------------------------------- */
#define NPOS 8                       /* index: 0=reverse 1=neutral 2..7 = gears 1..6 */
static int g_posDev[NPOS];           /* which configured device the position sits on */
static int g_posBtn[NPOS];           /* button index on that device, -1 = unbound */
static int g_posGear[NPOS]={-1,0,1,2,3,4,5,6};
static int g_shiftEnable=0, g_closedLoop=1;
static int g_dikUp=0x1E, g_dikDown=0x2C, g_dikMode=0, g_modeBtn=-1, g_modeDev=0, g_modeHold=0;
static int g_holdMs=40, g_gapMs=60, g_retryMs=1200, g_neutralDelayMs=1100;
static DWORD g_vaGamePtr=0x63788C; static int g_gearOfs=0x5D0, g_modeOfs=0x53C;

/* "2:38" -> the device named by device2=, button 38. The device number matches the ini key,
   so what you read in the file is what you configured. A bare "38" means device1, which keeps
   the single-device configs from the first drives working unchanged. */
static void ParseBind(const char *s,int *dev,int *btn){
    int i=0,a=0,any=0;
    while(s[i]==' ') i++;
    for(;s[i]>='0'&&s[i]<='9';i++){ a=a*10+(s[i]-'0'); any=1; }
    if(!any){ *dev=0; *btn=-1; return; }
    if(s[i]==':'){ i++; int b=0,any2=0;
        for(;s[i]>='0'&&s[i]<='9';i++){ b=b*10+(s[i]-'0'); any2=1; }
        *dev=(a>0)?a-1:0; *btn=any2?b:-1; return; }
    *dev=0; *btn=a;
}
static void IniBind(const char *key,int *dev,int *btn){
    char ini[MAX_PATH],buf[64]; IniPath(ini);
    GetPrivateProfileStringA("shift",key,"",buf,sizeof(buf),ini);
    if(!buf[0]){ *dev=0; *btn=-1; return; }
    ParseBind(buf,dev,btn);
    if(*dev<0||*dev>=MAXDEVCFG){ *dev=0; *btn=-1; }
}

static void ReadConfig(void){
    char ini[MAX_PATH]; IniPath(ini);
    for(int d=0;d<MAXDEVCFG;d++){
        char kn[16],ks[16];
        /* device1/suppress1 are the same slot as the legacy device/suppress */
        kn[0]='d';kn[1]='e';kn[2]='v';kn[3]='i';kn[4]='c';kn[5]='e';kn[6]=(char)('1'+d);kn[7]=0;
        ks[0]='s';ks[1]='u';ks[2]='p';ks[3]='p';ks[4]='r';ks[5]='e';ks[6]='s';ks[7]='s';ks[8]=(char)('1'+d);ks[9]=0;
        GetPrivateProfileStringA("hook",kn,"",g_devSubstr[d],sizeof(g_devSubstr[d]),ini);
        char list[256]; GetPrivateProfileStringA("hook",ks,"",list,sizeof(list),ini);
        if(d==0&&!g_devSubstr[0][0]) GetPrivateProfileStringA("hook","device","",g_devSubstr[0],sizeof(g_devSubstr[0]),ini);
        if(d==0&&!list[0])           GetPrivateProfileStringA("hook","suppress","",list,sizeof(list),ini);
        if(!g_devSubstr[d][0]) continue;
        g_nDevCfg=d+1;
        int v=-1;
        for(int i=0;;i++){ char c=list[i];
            if(c>='0'&&c<='9'){ if(v<0)v=0; v=v*10+(c-'0'); }
            else { if(v>=0&&v<128){ g_sup[d][v]=1; g_nSup++; } v=-1; if(!c) break; } }
    }
    L(0x80,(DWORD)g_nSup,(DWORD)g_nDevCfg,0,0);

    static const char *keys[NPOS]={"reverse","neutral","gear1","gear2","gear3","gear4","gear5","gear6"};
    for(int i=0;i<NPOS;i++) IniBind(keys[i],&g_posDev[i],&g_posBtn[i]);
    g_shiftEnable = IniNum("shift","enable",0);
    g_closedLoop  = IniNum("shift","closed_loop",1);
    g_dikUp       = IniNum("shift","gearup_dik",0x1E);
    g_dikDown     = IniNum("shift","geardown_dik",0x2C);
    g_dikMode     = IniNum("shift","mode_dik",0);
    IniBind("mode_btn",&g_modeDev,&g_modeBtn);
    g_modeHold    = IniNum("shift","mode_hold",0);
    g_holdMs      = IniNum("shift","hold_ms",40);
    g_gapMs       = IniNum("shift","gap_ms",60);
    g_retryMs     = IniNum("shift","retry_ms",1200);
    g_neutralDelayMs = IniNum("shift","neutral_delay_ms",1100);
    g_vaGamePtr   = (DWORD)IniNum("shift","gameptr",0x63788C);
    g_gearOfs     = IniNum("shift","gear_ofs",0x5D0);
    g_modeOfs     = IniNum("shift","mode_ofs",0x53C);
    L(0xA4,(DWORD)g_shiftEnable,(DWORD)g_dikUp,(DWORD)g_dikDown,(DWORD)g_closedLoop);
}

static void *VT(void *o,int i){ return (void*)((DWORD*)(*(DWORD*)o))[i]; }
static int PatchSlot(void *obj,int idx,void *fn,void **saved){
    DWORD *vt=*(DWORD**)obj, old;
    if(!VirtualProtect(&vt[idx],4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    if(saved&&!*saved) *saved=(void*)vt[idx];
    vt[idx]=(DWORD)fn; VirtualProtect(&vt[idx],4,old,&old); return 1;
}
/* which configured device this product name is, or -1 */
static int DevIndexOf(const char *n){
    for(int d=0;d<g_nDevCfg;d++){
        const char *s=g_devSubstr[d]; if(!s[0]) continue;
        for(int i=0;n[i];i++){ int k=0; while(s[k]&&n[i+k]==s[k]) k++; if(!s[k]) return d; }
    }
    return -1;
}

/* several pointers can denote the same physical device (QI to IDirectInputDevice2), and there
   are now several devices, so a target carries which configured device it belongs to */
#define MAXT 8
static void *g_tgt[MAXT]; static int g_tgtCfg[MAXT]; static int g_ntgt=0;
static int TargetCfg(void *p){ for(int i=0;i<g_ntgt;i++) if(g_tgt[i]==p) return g_tgtCfg[i]; return -1; }
static void AddTarget(void *p,int cfg){
    if(g_ntgt>=MAXT||TargetCfg(p)>=0) return;
    g_tgt[g_ntgt]=p; g_tgtCfg[g_ntgt]=cfg; g_ntgt++;
}

static DWORD g_btnOfs[MAXDEVCFG][128]; static int g_fmtDone[MAXDEVCFG];
static SetDataFormat_t o_SetFmt=NULL; static GetState_t o_GetState=NULL; static GetData_t o_GetData=NULL;
static CreateDevice_t o_CreateDevice=NULL; static CreateDeviceEx_t o_CreateDeviceEx=NULL;

static HRESULT __stdcall h_SetFmt(void *self,const DIDF *fmt){
    HRESULT hr=o_SetFmt(self,fmt);
    int cfg=TargetCfg(self);
    if(cfg>=0&&hr>=0&&fmt&&fmt->rgodf){
        for(int i=0;i<128;i++) g_btnOfs[cfg][i]=0xFFFFFFFF;
        int nb=0;
        /* With DIDFT_ANYINSTANCE (c_dfDIJoystick2) the instance field reads 0xFFFF for every
           button, so the button index is the ORDINAL of the button object in the format.
           Reading the field instead mapped no buttons at all and cost the first run. */
        DWORD ord = 0;
        for(DWORD k=0;k<fmt->dwNumObjs;k++){
            DIOBJDF *o=&fmt->rgodf[k];
            if(!(o->dwType&DIDFT_BUTTON)) continue;
            DWORD in=DIDFT_GETINSTANCE(o->dwType);
            if(in>=128) in=ord;
            ord++;
            if(in<128){ g_btnOfs[cfg][in]=o->dwOfs; nb++; }
        }
        g_fmtDone[cfg]=1;
        L(0x84,fmt->dwNumObjs,(DWORD)nb,(DWORD)cfg,fmt->dwDataSize);
    }
    return hr;
}
static HRESULT __stdcall h_GetState(void *self,DWORD cb,void *ptr){
    HRESULT hr=o_GetState(self,cb,ptr);
    int cfg=TargetCfg(self);
    if(cfg>=0&&hr>=0&&ptr&&g_fmtDone[cfg]){
        BYTE *b=(BYTE*)ptr; DWORD raw=0,z=0;
        for(int i=0;i<128;i++){
            DWORD o=g_btnOfs[cfg][i]; if(o==0xFFFFFFFF||o>=cb) continue;
            if(b[o]&0x80){ if(i<32) raw|=(1u<<i); if(g_sup[cfg][i]){ b[o]=0; z++; } }
        }
        static DWORD lr[MAXDEVCFG],lz[MAXDEVCFG]; static int init=0;
        if(!init){ for(int i=0;i<MAXDEVCFG;i++){ lr[i]=0xFFFFFFFF; lz[i]=0; } init=1; }
        if(raw!=lr[cfg]||z!=lz[cfg]){ L(0x85,cb,raw,z,(DWORD)cfg); lr[cfg]=raw; lz[cfg]=z; }
    }
    return hr;
}
static HRESULT __stdcall h_GetData(void *self,DWORD sz,DIDEVOBJDATA *rg,DWORD *pn,DWORD fl){
    HRESULT hr=o_GetData(self,sz,rg,pn,fl);
    int cfg=TargetCfg(self);
    if(cfg>=0&&hr>=0&&rg&&pn&&*pn&&g_fmtDone[cfg]&&!(fl&1)){
        DWORD in=*pn,out=0;
        for(DWORD e=0;e<in;e++){
            DIDEVOBJDATA *d=(DIDEVOBJDATA*)((BYTE*)rg+(DWORD)e*sz);
            int drop=0; for(int i=0;i<128;i++) if(g_sup[cfg][i]&&g_btnOfs[cfg][i]==d->dwOfs){ drop=1; break; }
            if(!drop){ if(out!=e) memcpy((BYTE*)rg+(DWORD)out*sz,d,sz); out++; }
        }
        if(out!=in){ *pn=out; L(0x86,in,out,in-out,(DWORD)self); }
    }
    return hr;
}

static void HookDevice(void *dev){
    PatchSlot(dev,DEV_SETDATAFORMAT,(void*)h_SetFmt,(void**)&o_SetFmt);
    PatchSlot(dev,DEV_GETSTATE,(void*)h_GetState,(void**)&o_GetState);
    PatchSlot(dev,DEV_GETDATA,(void*)h_GetData,(void**)&o_GetData);
}

static void Adopt(void *dev){
    DIDEVINST di; memset(&di,0,sizeof(di)); di.dwSize=sizeof(di);
    HRESULT hi=((GetDevInfo_t)VT(dev,DEV_GETDEVINFO))(dev,&di);
    int cfg=(hi>=0)?DevIndexOf(di.tszProductName):-1;
    L(0x82,(DWORD)hi,(DWORD)dev,(DWORD)(cfg+1),di.dwDevType);
    if(cfg<0) return;
    AddTarget(dev,cfg); HookDevice(dev);
    void *d2=NULL;
    if(((QI_t)VT(dev,DEV_QI))(dev,&IID_IDirectInputDevice2A,&d2)>=0&&d2){
        AddTarget(d2,cfg); HookDevice(d2);
        L(0x83,(DWORD)dev,(DWORD)d2,(DWORD)g_ntgt,(DWORD)cfg);
    } else L(0x83,(DWORD)dev,0,(DWORD)g_ntgt,(DWORD)cfg);
}

static HRESULT __stdcall h_CreateDevice(void *self,const GUID_ *g,void **dev,void *o){
    HRESULT hr=o_CreateDevice(self,g,dev,o);
    if(hr>=0&&dev&&*dev) Adopt(*dev);
    return hr;
}
static HRESULT __stdcall h_CreateDeviceEx(void *self,const GUID_ *g,const GUID_ *iid,void **dev,void *o){
    HRESULT hr=o_CreateDeviceEx(self,g,iid,dev,o);
    if(hr>=0&&dev&&*dev) Adopt(*dev);
    return hr;
}

static DWORD WINAPI Init(LPVOID p){
    (void)p;
    HMODULE d=LoadLibraryA("dinput.dll");
    if(!d){ L(0x81,1,0,0,0); return 0; }
    DICreateEx_t cx=(DICreateEx_t)GetProcAddress(d,"DirectInputCreateEx");
    if(!cx){ L(0x81,2,0,0,0); return 0; }
    void *di=NULL; HRESULT hr=cx(GetModuleHandleA(NULL),0x0700,&IID_IDirectInput7A,&di,NULL);
    if(hr<0||!di){ hr=cx(GetModuleHandleA(NULL),0x0700,&IID_IDirectInput2A,&di,NULL); }
    if(hr<0||!di){ L(0x81,3,(DWORD)hr,0,0); return 0; }
    int a=PatchSlot(di,DI_CREATEDEVICE,(void*)h_CreateDevice,(void**)&o_CreateDevice);
    int b=PatchSlot(di,DI_CREATEDEVICEEX,(void*)h_CreateDeviceEx,(void**)&o_CreateDeviceEx);
    L(0x81,0,(DWORD)di,(DWORD)*(DWORD*)di,(DWORD)(a|(b<<1)));
    return 0;
}

/* ===================== INPUT TELEMETRY =====================================================
   Every input source uses a shared 24-byte record and timebase, so the two
   projects read each other's logs. What the DEVICE sent (0x91/0x92) and what the GAME received
   (0x85) are separate observations - the distinction that settled two false alarms.

   Codes: 0x90 device enumerated (a=idx, b=1 if this is the configured shifter device)
          0x91 axis change    (a=idx, b=axis index, c=value, d=previous)
          0x92 button change  (a=idx, b=bitmask now (0..31), c=changed bits, d=bits 32..63)
          0x93 keyboard       (a=vk, b=1 down / 0 up)
          0x94 mouse buttons  (a=bitmask L|R|M, b=changed) */
#define DI8_VERSION_T        0x0800
#define DI8DEVCLASS_GAMECTRL 4
#define DIEDFL_ATTACHEDONLY  0x00000001
#define DIENUM_CONTINUE      1
#define DIENUM_STOP          0
#define DIDF_ABSAXIS         0x00000001
#define DIDFT_ABSAXIS        0x00000002
#define DIDFT_ANYINSTANCE    0x00FFFF00
#define DIDFT_OPTIONAL       0x80000000
#define DISCL_BACKGROUND     0x00000008
#define DISCL_NONEXCLUSIVE   0x00000002
#define DIPH_DEVICE          0
#define DIPROP_RANGE_T       ((const GUID_ *)4)
#define T_CREATEDEVICE 3
#define T_ENUMDEVICES  4
#define T_SETPROPERTY  6
#define T_ACQUIRE      7
#define T_GETSTATE     9
#define T_SETDATAFORMAT 11
#define T_SETCOOP      13
#define T_POLL         25

typedef HRESULT (WINAPI *DI8Create_T)(HINSTANCE,DWORD,const GUID_ *,void **,void *);
typedef HRESULT (__stdcall *EnumDevices_T)(void *,DWORD,void *,void *,DWORD);
typedef HRESULT (__stdcall *SetProperty_T)(void *,const GUID_ *,const void *);
typedef HRESULT (__stdcall *SetCoop_T)(void *,HWND,DWORD);
typedef HRESULT (__stdcall *Acquire_T)(void *);
typedef HRESULT (__stdcall *Poll_T)(void *);
typedef struct { DWORD dwSize,dwHeaderSize,dwObj,dwHow; } DIPROPHDR;
typedef struct { DIPROPHDR diph; LONG lMin,lMax; } DIPROPRANGE_T;

#define TNAX 7
static const GUID_ TAXG[TNAX] = {
    {0xA36D02E0,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02F4,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02F5,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02F8,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02F9,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02FA,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
    {0xA36D02E4,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}},
};
static const GUID_ IID_IDirectInput8A_T =
    {0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const GUID_ GUID_SysKeyboard_T =
    {0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};

/* 64 buttons, not 32: the mode-toggle binding is button 38 on the wheelbase. */
#define TNBTN 64
static DIOBJDF t_objs[TNAX+TNBTN];
static DIDF t_fmt = { sizeof(DIDF), sizeof(DIOBJDF), DIDF_ABSAXIS, TNAX*4+TNBTN, TNAX+TNBTN, t_objs };
typedef struct { LONG ax[TNAX]; BYTE btn[TNBTN]; } TSTATE;

#define TMAXDEV 8
static GUID_ t_guid[TMAXDEV];

/* v7.64: freshness, tracked apart from health. See 0x97 at the poll loop. */
#define STALE_MS        6000u   /* still for this long while another device moves = frozen */
#define STALE_FRESH_MS  3000u   /* how recently the liveliest device must have moved       */
#define STALE_SAY_MS   30000u   /* keep saying it, so a long freeze is not one lost line   */
static unsigned long long t_lastBits[TMAXDEV];
static LONG  t_lastAx0[TMAXDEV];
static DWORD t_lastMove[TMAXDEV];
static DWORD t_saidStale[TMAXDEV];
static DWORD t_ndev=0; static void *t_dev[TMAXDEV];
static int t_cfg[TMAXDEV];
/* live button state of each CONFIGURED device, published for the shift loop */
static volatile LONG g_btnLo[MAXDEVCFG], g_btnHi[MAXDEVCFG]; static volatile LONG g_shiftSeen=0;
/* our own DI8 device pointers - they go through the patched vtable too and must pass clean */
static void *g_ours[TMAXDEV+2]; static int g_nOurs=0;
static void AddOurs(void *p){ if(g_nOurs<(int)(sizeof(g_ours)/sizeof(g_ours[0]))) g_ours[g_nOurs++]=p; }
static int IsOurs(void *p){ for(int i=0;i<g_nOurs;i++) if(g_ours[i]==p) return 1; return 0; }

static BOOL __stdcall TEnumCb(const DIDEVINST *d,void *c){
    (void)c; if(t_ndev>=TMAXDEV) return DIENUM_STOP;
    t_guid[t_ndev]=d->guidInstance;
    t_cfg[t_ndev]=DevIndexOf(d->tszProductName);
    t_ndev++; return DIENUM_CONTINUE;
}

/* ===================== KEY INJECTION =======================================================
   The DI8 keyboard's GetDeviceState is patched on the SHARED class vtable, exactly as the DX7
   joystick was for suppression. We create a keyboard device of our own only to reach that
   vtable - it is never polled. The game's keyboard buffer is 256 bytes (DIJOYSTATE2 is 272 and
   the mouse 16/20), so cb==256 identifies it, and our own device pointers are excluded.

   Codes: 0x87 keyboard state read while a key was injected (a=cb, b=dik, c=running count)
          0x88 keyboard read through the BUFFERED path (a=events) - if this is the only channel
               that ever appears, the game does not use GetDeviceState and injection must move
               to GetDeviceData. Logging it is how we find that out instead of guessing. */
static volatile LONG g_holdDik=0;      /* DIK currently being injected, 0 = none */
static volatile LONG g_injCount=0;
static GetState_t o8_GetState=NULL; static GetData_t o8_GetData=NULL;

static HRESULT __stdcall h8_GetState(void *self,DWORD cb,void *ptr){
    HRESULT hr=o8_GetState(self,cb,ptr);
    if(hr>=0&&ptr&&cb==256&&!IsOurs(self)){
        LONG k=g_holdDik;
        /* every transition is logged, release included - the FIRST record (k=0) is on its own
           the proof that the game reads its keyboard through this call at all */
        static LONG lastK=-1;
        if(k!=lastK){ L(0x87,cb,(DWORD)k,(DWORD)g_injCount,(DWORD)self); lastK=k; }
        if(k>0&&k<256){ ((BYTE*)ptr)[k]|=0x80; InterlockedIncrement(&g_injCount); }
    }
    return hr;
}
static HRESULT __stdcall h8_GetData(void *self,DWORD sz,DIDEVOBJDATA *rg,DWORD *pn,DWORD fl){
    HRESULT hr=o8_GetData(self,sz,rg,pn,fl);
    if(hr>=0&&pn&&*pn&&!IsOurs(self)&&sz==sizeof(DIDEVOBJDATA)){
        static DWORD n=0; n++;
        if(n<=5||(n%500)==0) L(0x88,*pn,n,(DWORD)fl,(DWORD)self);
    }
    return hr;
}

/* ===================== THE CLOSED LOOP =====================================================
   Read the game's own gear, step toward the lever position one sequential shift at a time.

   Codes: 0xA0 lever target changed (a=target gear (biased +8), b=current, c=low button bits)
          0xA1 emit                 (a=dik, b=direction, c=target+8, d=current+8)
          0xA2 gear or mode changed (a=gear+8, b=mode byte, c=car ptr, d=frame ptr)
          0xA3 ceiling learned      (a=gear+8 the car refused to leave)
          0xA4 config               (a=enable, b=dik up, c=dik down, d=closed loop)
          0xA5 loop status          (a=step 1 chain ok / 2 unreadable, b=car, c=frame) */
#define GBIAS 8                        /* gears are signed; the log carries unsigned fields */
static int RdOk(DWORD addr,DWORD *out){
    if(addr<0x10000||addr>=0x80000000) return 0;
    if(IsBadReadPtr((void*)addr,4)) return 0;
    *out=*(DWORD*)addr; return 1;
}
static DWORD g_car=0,g_frame=0;
/* 0x96 - WHICH LINK OF THE CHAIN BROKE, logged on change only.
   Same defect report as 0x95: this returned a bare 0 for five different reasons, so a dead
   pointer chain was indistinguishable from a player who is not in a car - and after the fact
   nobody could tell which. `a` is the step that failed:
     1 = the game pointer          2 = game+0x24 (the car)
     3 = car+0x58 (the frame)      4 = frame+gear_ofs
     5 = the gear read but is out of range, i.e. the chain resolved to the wrong thing
     0 = the chain is healthy again
   b and c carry the last two values read, so a wrong-but-plausible address is visible. */
static int g_chainWhy=-1;
static void ChainWhy(int why,DWORD a,DWORD b){
    if(why==g_chainWhy) return;
    g_chainWhy=why;
    L(0x96,(DWORD)why,a,b,(DWORD)g_gearOfs);
}
static int ReadGear(int *gear,int *mode){
    DWORD p,car,frame,v;
    if(!RdOk(g_vaGamePtr,&p)){    ChainWhy(1,g_vaGamePtr,0); return 0; }
    if(!RdOk(p+0x24,&car)){       ChainWhy(2,p,0);           return 0; }
    if(!RdOk(car+0x58,&frame)){   ChainWhy(3,car,0);         return 0; }
    if(!RdOk(frame+(DWORD)g_gearOfs,&v)){ ChainWhy(4,frame,0); return 0; }
    g_car=car; g_frame=frame;
    *gear=(int)v;
    DWORD m; *mode = RdOk(frame+(DWORD)g_modeOfs,&m) ? (int)(m&0xFF) : -1;
    /* a plausible gear keeps a stale or wrong chain from driving the loop */
    if(*gear<-2||*gear>10){ ChainWhy(5,frame,v); return 0; }
    ChainWhy(0,car,v);
    return 1;
}
static void Emit(int dik,int dir,int target,int cur){
    L(0xA1,(DWORD)dik,(DWORD)dir,(DWORD)(target+GBIAS),(DWORD)(cur+GBIAS));
    g_holdDik=dik; Sleep(g_holdMs); g_holdDik=0; Sleep(g_gapMs);
}
/* THE DECISION CORE LIVES IN gearbox_logic.h AND IS TESTED WITHOUT THE GAME.
 *
 * Every bug this module has had was a logic bug - a momentary refusal recorded as a permanent
 * limit, a lever transit taken literally, a user's choice of automatic undone 300 ms later -
 * and each one cost a real drive with a real human to find. So the deciding is pure C with no
 * Windows in it, and `tests/test_gearbox.c` drives THIS EXACT CODE against a model of Mafia's
 * gearbox built from the recorded drives. What is left here is only the side effects: press a
 * key, wait, write a record. */
#include "gearbox_logic.h"

static void BuildCfg(GBCfg *c){
    for(int i=0;i<GB_NPOS;i++){ c->posDev[i]=g_posDev[i]; c->posBtn[i]=g_posBtn[i]; c->posGear[i]=g_posGear[i]; }
    c->modeDev=g_modeDev; c->modeBtn=g_modeBtn; c->modeHold=g_modeHold;
    c->dikUp=g_dikUp; c->dikDown=g_dikDown; c->dikMode=g_dikMode;
    c->neutralDelayMs=g_neutralDelayMs; c->retryMs=g_retryMs; c->closedLoop=g_closedLoop;
}

static DWORD WINAPI ShiftLoop(LPVOID p){
    (void)p;
    if(!g_shiftEnable) return 0;
    GBCfg cfg; BuildCfg(&cfg);
    GBState st; GBInit(&st);
    int lastGear=-99,lastMode=-99,lastTarget=-99,chainLogged=0;
    DWORD lastCar=0;
    for(;;){
        int gear=0,mode=-1,ok=ReadGear(&gear,&mode);
        if(ok&&!chainLogged){ L(0xA5,1,g_car,g_frame,0); chainLogged=1; }
        if(ok&&g_car!=lastCar){ lastCar=g_car; GBInit(&st); }   /* a new vehicle knows nothing */
        if(ok&&(gear!=lastGear||mode!=lastMode)){
            L(0xA2,(DWORD)(gear+GBIAS),(DWORD)mode,g_car,g_frame);
            lastGear=gear; lastMode=mode;
        }
        if(!g_shiftSeen){ Sleep(8); continue; }

        GBIn in;
        for(int d=0;d<GB_MAXDEV;d++)
            in.bits[d]=((unsigned long long)(DWORD)g_btnHi[d]<<32)|(DWORD)g_btnLo[d];
        in.gear=gear; in.mode=mode; in.gearValid=ok; in.now=(gb_ms)GetTickCount();

        GBOut o=GBStep(&cfg,&st,&in);

        if(o.known&&o.target!=lastTarget){
            L(0xA0,(DWORD)(o.target+GBIAS),(DWORD)(ok?gear+GBIAS:0),(DWORD)g_btnLo[0],(DWORD)g_btnLo[1]);
            lastTarget=o.target;
        }
        if(o.blockedAuto){ Sleep(50); continue; }

        if(o.act==GB_MODE_KEY){
            L(0xA6,st.pendAuto?3:1,(DWORD)g_dikMode,(DWORD)(o.target+GBIAS),(DWORD)mode);
            Emit(g_dikMode,0,gear,gear);
        } else if(o.act==GB_UP||o.act==GB_DOWN){
            int dir=(o.act==GB_UP)?1:-1, before=gear;
            Emit(dir>0?g_dikUp:g_dikDown,dir,o.target,before);
            /* wait for the game to actually move - not moving is information, not a failure */
            DWORD t0=GetTickCount(); int moved=0,g2,m2;
            while(GetTickCount()-t0<300){
                if(ReadGear(&g2,&m2)&&g2!=before){ moved=1; break; }
                Sleep(8);
            }
            GBAfterShift(&cfg,&st,dir,before,moved,(gb_ms)GetTickCount());
            if(!moved) L(0xA3,(DWORD)(before+GBIAS),(DWORD)(dir>0),(DWORD)g_retryMs,(DWORD)(o.target+GBIAS));
        }
        Sleep(8);
    }
}


static DWORD WINAPI Telemetry(LPVOID p){
    (void)p;
    HMODULE h=LoadLibraryA("dinput8.dll"); if(!h) return 0;
    DI8Create_T cr=(DI8Create_T)GetProcAddress(h,"DirectInput8Create"); if(!cr) return 0;
    void *di=NULL;
    if(cr(GetModuleHandleA(NULL),DI8_VERSION_T,&IID_IDirectInput8A_T,&di,NULL)<0||!di) return 0;

    /* Reach the shared IDirectInputDevice8A vtable through a keyboard device of our own and
       patch it. The game's keyboard - created by LS3DF, possibly long before or after this -
       uses the same vtable, so the patch covers it either way. */
    void *kb=NULL;
    if(((CreateDevice_t)VT(di,T_CREATEDEVICE))(di,&GUID_SysKeyboard_T,&kb,NULL)>=0&&kb){
        AddOurs(kb);
        int a=PatchSlot(kb,DEV_GETSTATE,(void*)h8_GetState,(void**)&o8_GetState);
        int b=PatchSlot(kb,DEV_GETDATA,(void*)h8_GetData,(void**)&o8_GetData);
        L(0x89,(DWORD)kb,(DWORD)*(DWORD*)kb,(DWORD)(a|(b<<1)),0);
    } else L(0x89,0,0,0,1);

    ((EnumDevices_T)VT(di,T_ENUMDEVICES))(di,DI8DEVCLASS_GAMECTRL,(void*)TEnumCb,NULL,DIEDFL_ATTACHEDONLY);

    for(int i=0;i<TNAX;i++){ t_objs[i].pguid=&TAXG[i]; t_objs[i].dwOfs=(DWORD)(i*4);
        t_objs[i].dwType=DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL; t_objs[i].dwFlags=0; }
    for(int i=0;i<TNBTN;i++){ t_objs[TNAX+i].pguid=NULL; t_objs[TNAX+i].dwOfs=(DWORD)(TNAX*4+i);
        t_objs[TNAX+i].dwType=DIDFT_BUTTON|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL; t_objs[TNAX+i].dwFlags=0; }

    HWND w=GetForegroundWindow();
    for(DWORD i=0;i<t_ndev;i++){
        t_dev[i]=NULL;
        if(((CreateDevice_t)VT(di,T_CREATEDEVICE))(di,&t_guid[i],&t_dev[i],NULL)<0||!t_dev[i]){ t_dev[i]=NULL; continue; }
        AddOurs(t_dev[i]);
        ((SetDataFormat_t)VT(t_dev[i],T_SETDATAFORMAT))(t_dev[i],&t_fmt);
        ((SetCoop_T)VT(t_dev[i],T_SETCOOP))(t_dev[i],w,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND);
        DIPROPRANGE_T r; r.diph.dwSize=sizeof(r); r.diph.dwHeaderSize=sizeof(DIPROPHDR);
        r.diph.dwObj=0; r.diph.dwHow=DIPH_DEVICE; r.lMin=0; r.lMax=65535;
        ((SetProperty_T)VT(t_dev[i],T_SETPROPERTY))(t_dev[i],DIPROP_RANGE_T,&r.diph);
        ((Acquire_T)VT(t_dev[i],T_ACQUIRE))(t_dev[i]);
        L(0x90,i,(DWORD)(t_cfg[i]+1),0,0);
    }

    static HRESULT t_lastHr[TMAXDEV]; static LONG t_fail[TMAXDEV];
    static LONG lastAx[TMAXDEV][TNAX]; static unsigned long long lastBtn[TMAXDEV];
    static BYTE lastKey[256]; static DWORD lastMouse=0;
    for(DWORD i=0;i<TMAXDEV;i++){ for(int a=0;a<TNAX;a++) lastAx[i][a]=0x7FFFFFFF; lastBtn[i]=0; }

    for(;;){
        for(DWORD i=0;i<t_ndev;i++){
            if(!t_dev[i]) continue;
            ((Poll_T)VT(t_dev[i],T_POLL))(t_dev[i]);
            TSTATE s; memset(&s,0,sizeof(s));
            /* 0x95 - THE DEVICE'S OWN HEALTH, logged on CHANGE only.
               Reported on 2026-07-27 from an 18-minute play session on the custom
               install: the matched lever froze 0.22 s in, stuck on `buttons=00000080`, and never
               reported again - while two other devices on the SAME DirectInput instance and the
               same window kept reporting for the full session. The module emitted nothing all
               evening and the log said nothing about why, because this branch re-Acquired and
               `continue`d in silence. A permanently dead device was indistinguishable from a
               lever nobody touched.
               a = device index, b = the GetState HRESULT, c = the Acquire HRESULT,
               d = how many consecutive failures this device has had. */
            HRESULT ghr=((GetState_t)VT(t_dev[i],T_GETSTATE))(t_dev[i],sizeof(s),&s);
            if(ghr<0){
                HRESULT ahr=((Acquire_T)VT(t_dev[i],T_ACQUIRE))(t_dev[i]);
                if(t_fail[i]<0x7FFFFFFF) t_fail[i]++;
                if(ghr!=t_lastHr[i] || t_fail[i]==1){
                    L(0x95,i,(DWORD)ghr,(DWORD)ahr,(DWORD)t_fail[i]);
                    t_lastHr[i]=ghr;
                }
                continue;
            }
            if(t_fail[i]){   /* it came back - say so, or a recovery reads as if nothing happened */
                L(0x95,i,0u,0u,(DWORD)t_fail[i]);
                t_fail[i]=0; t_lastHr[i]=0;
            }
            for(int a=0;a<TNAX;a++){
                LONG v=s.ax[a], o=lastAx[i][a];
                if(o==0x7FFFFFFF || (v>o?v-o:o-v) > 400){ L(0x91,i,(DWORD)a,(DWORD)v,(DWORD)o); lastAx[i][a]=v; }
            }
            unsigned long long bits=0;
            for(int b=0;b<TNBTN;b++) if(s.btn[b]&0x80) bits|=(1ull<<b);
            if(t_cfg[i]>=0){
                g_btnLo[t_cfg[i]]=(LONG)(DWORD)(bits&0xFFFFFFFF);
                g_btnHi[t_cfg[i]]=(LONG)(DWORD)(bits>>32);
                g_shiftSeen=1;
            }
            if(bits!=lastBtn[i]){
                L(0x92,i,(DWORD)(bits&0xFFFFFFFF),(DWORD)((bits^lastBtn[i])&0xFFFFFFFF),(DWORD)(bits>>32));
                lastBtn[i]=bits;
            }
            /* 0x97 - A DEVICE THAT SUCCEEDS AND LIES. This is the hole 0x95 does not cover.
               The 18-minute episode did NOT fail: GetState kept returning S_OK and kept
               returning the SAME frozen state, buttons=00000080, for the whole session. No
               HRESULT changed, no failure counter moved, so the branch above stayed silent and
               a dead lever was indistinguishable from one nobody touched.
               Freshness is therefore tracked separately from health, and the distinction that
               makes it a REPORT rather than a guess is comparative: a device is only called
               frozen if it has been still while ANOTHER device on the same loop has moved.
               All quiet = nobody is driving. One quiet while others move = that one is dead. */
            if(bits!=t_lastBits[i] || s.ax[0]!=t_lastAx0[i]){
                t_lastBits[i]=bits; t_lastAx0[i]=s.ax[0];
                t_lastMove[i]=GetTickCount();
                t_saidStale[i]=0;
            }
        }
        {
            DWORD nowT=GetTickCount(), newest=0;
            for(DWORD i=0;i<t_ndev;i++) if(t_dev[i] && t_lastMove[i]>newest) newest=t_lastMove[i];
            for(DWORD i=0;i<t_ndev;i++){
                if(!t_dev[i] || !t_lastMove[i]) continue;
                DWORD still=nowT-t_lastMove[i];
                /* another device moved recently, this one has not, and the gap is long enough
                   that it cannot be ordinary driving. a = device, b = ms since it last moved,
                   c = ms since the LIVELIEST device moved, d = 1 the first time, then every
                   STALE_SAY_MS so a long freeze keeps saying so. */
                if(still>=STALE_MS && (nowT-newest)<STALE_FRESH_MS){
                    if(!t_saidStale[i] || (nowT-t_saidStale[i])>=STALE_SAY_MS){
                        L(0x97,i,still,nowT-newest,t_saidStale[i]?2u:1u);
                        t_saidStale[i]=nowT?nowT:1;
                    }
                }
            }
        }
        for(int vk=0x08;vk<=0xFE;vk++){
            if(vk==0x01||vk==0x02||vk==0x04) continue;
            BYTE d=(GetAsyncKeyState(vk)&0x8000)?1:0;
            if(d!=lastKey[vk]){ L(0x93,(DWORD)vk,(DWORD)d,0,0); lastKey[vk]=d; }
        }
        DWORD mb=0;
        if(GetAsyncKeyState(0x01)&0x8000) mb|=1;
        if(GetAsyncKeyState(0x02)&0x8000) mb|=2;
        if(GetAsyncKeyState(0x04)&0x8000) mb|=4;
        if(mb!=lastMouse){ L(0x94,mb,mb^lastMouse,0,0); lastMouse=mb; }
        Sleep(8);
    }
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID rr){
    (void)h;(void)rr;
    if(reason==DLL_PROCESS_ATTACH){
        InitializeCriticalSection(&g_cs); g_csReady=1;
        HANDLE m=CreateMutexA(NULL,FALSE,"MafiaGearboxHook8_Spike_v1");
        if(m&&WaitForSingleObject(m,0)!=WAIT_OBJECT_0) return TRUE;
        InitDir();
        /* the log joins the rest of the gearbox files in their own folder */
        char nm[MAX_PATH]; GearboxDir(nm); CreateDirectoryA(nm,NULL);
        GamePath(nm,GBDIR "\\gearbox_hook.bin");
        /* CREATE_ALWAYS truncates, so every launch used to destroy the previous run's evidence -
           and the way this module is used, the launch AFTER the interesting drive is the one that
           happens while you are still working out what went wrong. Keep one generation back. */
        {
            char prev[MAX_PATH]; GamePath(prev,GBDIR "\\gearbox_hook.prev.bin");
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if(GetFileAttributesExA(nm,GetFileExInfoStandard,&fad) &&
               (fad.nFileSizeLow>24u || fad.nFileSizeHigh)){
                DeleteFileA(prev);
                MoveFileA(nm,prev);
            }
        }
        g_log=CreateFileA(nm,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        /* a read-only or missing folder must not cost us the log entirely */
        if(g_log==INVALID_HANDLE_VALUE){
            GamePath(nm,"gearbox_hook.bin");
            g_log=CreateFileA(nm,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        }
        /* config is read HERE, not on the Init thread: the shift loop reads g_shiftEnable the
           moment it starts and would have raced a config still being parsed */
        ReadConfig();
        DWORD t; CreateThread(NULL,0,Init,NULL,0,&t);
        CreateThread(NULL,0,Telemetry,NULL,0,&t);
        CreateThread(NULL,0,ShiftLoop,NULL,0,&t);
    } else if(reason==DLL_PROCESS_DETACH){
        if(g_log!=INVALID_HANDLE_VALUE){ FlushFileBuffers(g_log); CloseHandle(g_log); }
    }
    return TRUE;
}
