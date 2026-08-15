/* ffb_devices.h - which wheel the force feedback mod talks to, and a force you can feel.
 *
 * Requested on 2026-08-01, naming the failure exactly: force feedback could latch onto
 * some rumble seat instead of the wheel, which would defeat the point.
 *
 * He is right, and it is the DEFAULT path. The mod's enum filter is
 * DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK, and a rumble seat or a pad IS a force-feedback
 * device as far as DirectInput is concerned. With no `device=` key the mod takes the FIRST one and
 * stops enumerating, so which device gets our forces is Windows' enumeration order - not a choice
 * anybody made.
 *
 * THE MOD SIDE ALREADY EXISTS - M3, commit 299a28b: `device=` in [ffb] takes an instance GUID, a
 * substitution is announced (diag 0x15), a mistyped GUID is told apart from a missing key (0x14),
 * and every offered device is logged with its full GUID (0x11/0x12). What was missing is any way
 * to SET it without reading a log and hand-editing an ini, which is not something to ask of
 * somebody who just wants their wheel to work.
 *
 * The GUID string this writes is the format mafia_ffb_v6.c:ParseGuid accepts -
 * {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}. Written with braces and uppercase because that is what
 * Windows prints; the parser takes it with or without the braces, but it will not take a format
 * this file invents, and a key the mod cannot parse is a control that does nothing while looking
 * like it worked.
 *
 * DirectInput declarations come from page_shifter.h, which is included first (launcher.c:279) and
 * already has GUID_, DIDEVINST, DIDF, VT() and most of the vtable indices. This file adds only
 * what a FORCE needs. One copy of each declaration, on purpose: two would be free to disagree.
 */
#ifndef ALXG_FFB_DEVICES_H
#define ALXG_FFB_DEVICES_H

#define DIEDFL_FORCEFEEDBACK    0x00000100
#define DISCL_EXCLUSIVE         0x00000001
#define DEV_SETPROPERTY         6
#define DEV_UNACQUIRE           8
#define DEV_CREATEEFFECT        18
#define EFF_SETPARAMETERS       6
#define EFF_START               7
#define EFF_STOP                8
#define DIEP_TYPESPECIFICPARAMS 0x00000100
#define DIEP_START              0x20000000
#define COM_RELEASE             2
#define DIEFF_OBJECTOFFSETS     0x00000002
#define DIEFF_CARTESIAN         0x00000010
#define DIEB_NOTRIGGER          0xFFFFFFFF
#define DI_INFINITE             0xFFFFFFFFu
#define DIPH_DEVICE             0
#define DIPROPAUTOCENTER_OFF    0
#define DIPROP_AUTOCENTER       ((const GUID_ *)3)

typedef struct {
    DWORD dwSize, dwFlags, dwDuration, dwSamplePeriod, dwGain;
    DWORD dwTriggerButton, dwTriggerRepeatInterval, cAxes;
    DWORD *rgdwAxes;
    LONG  *rglDirection;
    void  *lpEnvelope;
    DWORD cbTypeSpecificParams;
    void  *lpvTypeSpecificParams;
    DWORD dwStartDelay;
} DIEFF;
typedef struct { LONG lMagnitude; } DICONST;
typedef struct { DWORD dwSize, dwHeaderSize, dwObj, dwHow; } DIPROPHEADER;
typedef struct { DIPROPHEADER diph; DWORD dwData; } DIPROPDWORD;

typedef HRESULT (__stdcall *SetProperty_t)(void *, const GUID_ *, const DIPROPHEADER *);
typedef HRESULT (__stdcall *CreateEffect_t)(void *, const GUID_ *, const DIEFF *, void **, void *);
typedef HRESULT (__stdcall *EffSetParams_t)(void *, const DIEFF *, DWORD);
typedef HRESULT (__stdcall *EffStart_t)(void *, DWORD, DWORD);
/* Stop() takes NO ARGUMENTS. Calling it through a two-argument type pushes two DWORDs the callee
   never pops, and in __stdcall the CALLEE cleans up - so every such call walks the stack pointer
   8 bytes. That is what crashed FFB v7.24 and v7.25, and it is written down here because this
   file is the second place in the project to call Stop. */
typedef HRESULT (__stdcall *EffStop_t)(void *);
typedef HRESULT (__stdcall *Unacquire_t)(void *);
typedef ULONG   (__stdcall *Release_t)(void *);

static const GUID_ GUID_ConstantForce_ =
    {0x13541C20,0x8E33,0x11D0,{0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35}};

/* ---- what is out there ----------------------------------------------------------------------
 * A SEPARATE enumeration from the H-shifter page's, and it has to be: that one asks for every
 * game controller (DIEDFL_ATTACHEDONLY alone) because a shifter is not a force device, and it
 * keeps no instance GUID at all because it matches devices by NAME. This one asks the same
 * question the MOD asks, with the same flags, so the list on screen is the list the mod will be
 * offered. A picker built on a different question is a picker that can offer a device the mod
 * never sees. */
#define FFBDEV_MAX 8
static GUID_ ffbdev_guid[FFBDEV_MAX];
static char  ffbdev_name[FFBDEV_MAX][260];
static int   ffbdev_n = 0;

static BOOL __stdcall ffbdev_EnumCb(const DIDEVINST *d, void *c)
{
    (void)c;
    if (ffbdev_n >= FFBDEV_MAX) return DIENUM_STOP;
    ffbdev_guid[ffbdev_n] = d->guidInstance;
    { int i = 0; for (; i < 259 && d->tszProductName[i]; i++)
                     ffbdev_name[ffbdev_n][i] = d->tszProductName[i];
      ffbdev_name[ffbdev_n][i] = 0; }
    ffbdev_n++;
    return DIENUM_CONTINUE;
}

static void FfbDevEnum(void)
{
    HMODULE di8;
    DI8Create_t create;
    void *di = NULL;
    ffbdev_n = 0;
    di8 = LoadSystemDll("dinput8.dll");
    create = di8 ? (DI8Create_t)GetProcAddress(di8, "DirectInput8Create") : NULL;
    if (!create) return;
    if (create(GetModuleHandleA(NULL), DI8_VERSION, &IID_IDirectInput8A, &di, NULL) < 0 || !di)
        return;
    ((EnumDevices_t)VT(di, DI_ENUMDEVICES))(di, DI8DEVCLASS_GAMECTRL, (void *)ffbdev_EnumCb, NULL,
                                            DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    ((Release_t)VT(di, COM_RELEASE))(di);
}

/* {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}, the shape ParseGuid reads. */
static void FfbGuidStr(const GUID_ *g, char *out)
{
    static const char *H = "0123456789ABCDEF";
    int p = 0, i;
    out[p++] = '{';
    for (i = 28; i >= 0; i -= 4) out[p++] = H[(g->a >> i) & 0xF];
    out[p++] = '-';
    for (i = 12; i >= 0; i -= 4) out[p++] = H[(g->b >> i) & 0xF];
    out[p++] = '-';
    for (i = 12; i >= 0; i -= 4) out[p++] = H[(g->c >> i) & 0xF];
    out[p++] = '-';
    for (i = 0; i < 8; i++) {
        if (i == 2) out[p++] = '-';
        out[p++] = H[(g->d[i] >> 4) & 0xF];
        out[p++] = H[g->d[i] & 0xF];
    }
    out[p++] = '}';
    out[p] = 0;
}

/* Which row is lit, and THREE answers rather than two:
 *     >= 0            that device, attached and chosen
 *     FFBDEV_ANY  -1  nothing chosen - an EMPTY device=, which is what every install has had
 *                     until today and is a real choice with a real consequence
 *     FFBDEV_GONE -2  a GUID IS set and that device is not attached right now
 *
 * The third answer is the whole reason this is not a two-way test. Folding GONE into ANY would
 * light "First one offered" over a file that names a wheel - the same lie the H-shifter page was
 * fixed for on this very day, where a row read "click to set" over a real binding. A picker may
 * say the wheel is not present; it may not say that nothing was chosen.
 */
#define FFBDEV_ANY  (-1)
#define FFBDEV_GONE (-2)

static int FfbDevChosen(const char *want)
{
    char s[48];
    int i;
    if (!want || !want[0]) return FFBDEV_ANY;
    for (i = 0; i < ffbdev_n; i++) {
        FfbGuidStr(&ffbdev_guid[i], s);
        if (SEq(s, want)) return i;
    }
    return FFBDEV_GONE;
}

/* ---- the force you can feel ------------------------------------------------------------------
 * Two identical wheels have two identical product names, and no amount of text tells them apart.
 * A force does. Request, 2026-08-01: a button to trigger a test force.
 *
 * SYNCHRONOUS, and the window is frozen for the ~1.2 s it lasts. A worker thread would have to be
 * stopped when the page changes, when the device list is re-enumerated, and when the program
 * exits, and every one of those is a way to leave a force ON a wheel nobody is holding. A blocked
 * message loop cannot do that.
 *
 * IT REFUSES WHILE THE GAME IS RUNNING. Taking the wheel EXCLUSIVE is exactly what the mod does,
 * and two exclusive owners is a fight whose loser is whichever asked second. Refusing with the
 * reason is the only honest option - reporting no effect on the wheel would be indistinguishable from
 * broken hardware.
 *
 * Left, then right, then centre, so what is felt is unmistakably a direction and not a rumble.
 * Returns 0 and a reason on failure, because a picker that cannot say WHY nothing happened sends
 * the user to look at their wheel instead of at their choice.
 */
static int FfbTestForce(int devIdx, HWND owner, char *why, int whyCap)
{
    HMODULE di8;
    DI8Create_t create;
    void *di = NULL, *dev = NULL, *eff = NULL;
    DIEFF ep;
    DICONST cf;
    DIPROPDWORD ac;
    DWORD axes[1];
    LONG  dir[1];
    static DIOBJDF objs[1];
    static DIDF fmt;
    HRESULT hr;
    int ok = 0;

    why[0] = 0;
    if (devIdx < 0 || devIdx >= ffbdev_n) { SCpy(why, "pick a device first"); return 0; }

    di8 = LoadSystemDll("dinput8.dll");
    create = di8 ? (DI8Create_t)GetProcAddress(di8, "DirectInput8Create") : NULL;
    if (!create) { SCpy(why, "dinput8.dll did not load"); return 0; }
    if (create(GetModuleHandleA(NULL), DI8_VERSION, &IID_IDirectInput8A, &di, NULL) < 0 || !di) {
        SCpy(why, "DirectInput8Create failed"); return 0;
    }

    hr = ((CreateDevice_t)VT(di, DI_CREATEDEVICE))(di, &ffbdev_guid[devIdx], &dev, NULL);
    if (hr < 0 || !dev) { SCpy(why, "the device could not be opened"); goto out; }

    /* One axis at offset 0, because the effect below addresses its axis BY OFFSET
       (DIEFF_OBJECTOFFSETS). Our own format rather than the shifter page's: that one is filled in
       when its devices are opened, and this button has to work whether or not that happened. */
    objs[0].pguid = &GUID_XAxis;
    objs[0].dwOfs = 0;
    objs[0].dwType = DIDFT_ABSAXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL;
    objs[0].dwFlags = 0;
    fmt.dwSize = sizeof(DIDF); fmt.dwObjSize = sizeof(DIOBJDF); fmt.dwFlags = DIDF_ABSAXIS;
    fmt.dwDataSize = 4; fmt.dwNumObjs = 1; fmt.rgodf = objs;
    if (((SetDataFormat_t)VT(dev, DEV_SETDATAFORMAT))(dev, &fmt) < 0) {
        SCpy(why, "the device refused a data format"); goto out;
    }
    if (((SetCoop_t)VT(dev, DEV_SETCOOP))(dev, owner, DISCL_EXCLUSIVE | DISCL_BACKGROUND) < 0) {
        SCpy(why, "the device refused exclusive access - is something else using it?"); goto out;
    }

    /* Autocentring fights a constant force, and on a strong wheel it wins. Off for the test; the
       driver's own setting comes back when the device object is released below. */
    ac.diph.dwSize = sizeof(DIPROPDWORD);
    ac.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    ac.diph.dwObj = 0;
    ac.diph.dwHow = DIPH_DEVICE;
    ac.dwData = DIPROPAUTOCENTER_OFF;
    ((SetProperty_t)VT(dev, DEV_SETPROPERTY))(dev, DIPROP_AUTOCENTER, &ac.diph);

    if (((Acquire_t)VT(dev, DEV_ACQUIRE))(dev) < 0) {
        SCpy(why, "the device could not be acquired"); goto out;
    }

    axes[0] = 0;            /* the X axis, by offset */
    dir[0]  = 1;
    cf.lMagnitude = 0;
    ep.dwSize = sizeof(DIEFF);
    ep.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    ep.dwDuration = DI_INFINITE;
    ep.dwSamplePeriod = 0;
    ep.dwGain = 10000;
    ep.dwTriggerButton = DIEB_NOTRIGGER;
    ep.dwTriggerRepeatInterval = 0;
    ep.cAxes = 1;
    ep.rgdwAxes = axes;
    ep.rglDirection = dir;
    ep.lpEnvelope = NULL;
    ep.cbTypeSpecificParams = sizeof(DICONST);
    ep.lpvTypeSpecificParams = &cf;
    ep.dwStartDelay = 0;

    hr = ((CreateEffect_t)VT(dev, DEV_CREATEEFFECT))(dev, &GUID_ConstantForce_, &ep, &eff, NULL);
    if (hr < 0 || !eff) {
        SCpy(why, "this device took no constant force - it may rumble rather than steer");
        goto out;
    }

    /* 4000 of 10000. Firm enough to be unmistakable on a 12 Nm direct drive without yanking it out
       of a hand, and strong enough that a rumble pad does something visible. */
    {
        static const LONG mag[3] = { -4000, 4000, 0 };
        int i;
        for (i = 0; i < 3; i++) {
            cf.lMagnitude = mag[i];
            /* magnitude and start in ONE call - the effect is already created, so the only thing
               changing is the type-specific block it points at */
            ((EffSetParams_t)VT(eff, EFF_SETPARAMETERS))(
                eff, &ep, DIEP_TYPESPECIFICPARAMS | DIEP_START);
            Sleep(i == 2 ? 120 : 420);
        }
    }
    ok = 1;

out:
    /* Every exit path lets go of the wheel. A force left on a device by a program that has moved
       on is the one outcome worth writing a goto for. */
    if (eff) { ((EffStop_t)VT(eff, EFF_STOP))(eff); ((Release_t)VT(eff, COM_RELEASE))(eff); }
    if (dev) { ((Unacquire_t)VT(dev, DEV_UNACQUIRE))(dev); ((Release_t)VT(dev, COM_RELEASE))(dev); }
    if (di)  { ((Release_t)VT(di, COM_RELEASE))(di); }
    return ok;
}

#endif /* ALXG_FFB_DEVICES_H */
