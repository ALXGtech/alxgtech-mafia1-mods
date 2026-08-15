/* ffb_status.c - gog-patcher only, milestone M1b.
 *
 * The .asi's side of the connected-and-effective lamp. Included from mafia_ffb_v6.c after
 * GamePath, Log and the device globals exist, so it reads them directly and owns no line of
 * any other module's source - the merge discipline this project keeps.
 *
 * WHY TWO WORDS AND NOT ONE. The utility cannot tell from outside whether the mod is doing
 * anything. Three failures look identical on screen - the .asi never loaded, it loaded but
 * DirectInput handed the wheel to someone else, or it holds the device and every
 * SetParameters is failing. So the file reports them separately:
 *   found/acquired  = we have the device                 -> "connected"
 *   effect_hr >= 0  = the last force actually reached it -> "effective"
 * A lamp that says "connected" while the wheel is silent is the exact false positive this
 * file exists to prevent.
 *
 * It is written by the FFB thread, never read by it. The utility only reads. No locking:
 * a torn read costs the GUI one stale refresh, and it refreshes anyway.
 */

#define K_STATUS 7u   /* diag record: status written. K_SETTINGS=6 was the last one. */

/* Set by SetMag in mafia_ffb_v6.c - the ONE line M1b touches in their file. The last
   HRESULT from IDirectInputEffect::SetParameters, i.e. did the force reach the wheel.
   Starts at 1 = "nothing sent yet", which is not a failure and must not light the lamp red. */
static volatile LONG g_lastEffectHr = 1;

static DWORD g_statusGen = 0;      /* bumped on every write, so the GUI can spot a dead .asi */

/* ---- tiny formatters. -nostdlib: no sprintf, and nothing here may pull one in. ---- */

static int StCat(char *dst, int at, const char *s)
{
    while (*s) dst[at++] = *s++;
    return at;
}

static int StCatU(char *dst, int at, DWORD v)
{
    char tmp[12];
    int n = 0;
    if (!v) { dst[at++] = '0'; return at; }
    while (v) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n) dst[at++] = tmp[--n];
    return at;
}

static int StCatHex(char *dst, int at, DWORD v)
{
    const char *H = "0123456789ABCDEF";
    at = StCat(dst, at, "0x");
    for (int sh = 28; sh >= 0; sh -= 4) dst[at++] = H[(v >> sh) & 0xFu];
    return at;
}

static void StatusPath(char *out)
{
    GamePath(out, "ALXG mods\\mafia ffb setup\\mafia_ffb_status.ini");
}

/* Compose and write. Only writes when the text actually changed or 10 s have passed - the
   generation counter is what proves the .asi is alive, and a heartbeat that costs one small
   file write every 10 s is cheaper than one every second for a value nobody watches. */
static void WriteFFBStatus(void)
{
    static char  prev[1024];
    static int   prevLen = 0;
    static DWORD lastForced = 0;

    char buf[1024];
    int  n = 0;

    HRESULT acq = EnsureAcquired();
    int haveDev = g_dev != NULL;
    int effective = haveDev && g_eff && acq >= 0 && g_lastEffectHr >= 0;

    n = StCat(buf, n, "; written by mafia_ffb.asi - do not edit, every field is overwritten\r\n");
    n = StCat(buf, n, "[status]\r\n");
    n = StCat(buf, n, "build=");
    n = StCat(buf, n, (g_buildIdx >= 0 && g_buildIdx < N_BUILDS)
                          ? g_builds[g_buildIdx].name : "unknown");
    n = StCat(buf, n, "\r\ndevice=");
    n = StCat(buf, n, g_found ? g_wheelName : "<none enumerated>");
    n = StCat(buf, n, "\r\nfound=");        n = StCatU(buf, n, (DWORD)(g_found ? 1 : 0));
    n = StCat(buf, n, "\r\nacquired_hr=");  n = StCatHex(buf, n, (DWORD)acq);
    n = StCat(buf, n, "\r\neffect_hr=");    n = StCatHex(buf, n, (DWORD)g_lastEffectHr);
    n = StCat(buf, n, "\r\nconstant=");     n = StCatU(buf, n, (DWORD)(g_eff    ? 1 : 0));
    n = StCat(buf, n, "\r\nspring=");       n = StCatU(buf, n, (DWORD)(g_spring ? 1 : 0));
    n = StCat(buf, n, "\r\ndamper=");       n = StCatU(buf, n, (DWORD)(g_damper ? 1 : 0));
    n = StCat(buf, n, "\r\neffective=");    n = StCatU(buf, n, (DWORD)(effective ? 1 : 0));
    n = StCat(buf, n, "\r\nin_vehicle=");   n = StCatU(buf, n, (DWORD)(g_car ? 1 : 0));
    n = StCat(buf, n, "\r\nsettings_generation="); n = StCatU(buf, n, (DWORD)g_settingsGen);
    n = StCat(buf, n, "\r\n");

    /* Everything above this line is STATE. Everything below it changes on its own every
       call, so it is excluded from the "did anything happen" comparison - otherwise the
       heartbeat would defeat its own write-suppression. */
    const int cmpLen = n;

    DWORD now = GetTickCount();
    int same = (cmpLen == prevLen);
    if (same) for (int i = 0; i < cmpLen; i++) if (buf[i] != prev[i]) { same = 0; break; }
    if (same && (now - lastForced) < 10000u) return;

    DWORD gen = g_statusGen + 1;
    n = StCat(buf, n, "generation=");  n = StCatU(buf, n, gen);
    n = StCat(buf, n, "\r\npid=");     n = StCatU(buf, n, GetCurrentProcessId());
    n = StCat(buf, n, "\r\ntick=");    n = StCatU(buf, n, now);
    n = StCat(buf, n, "\r\n");

    char path[MAX_PATH];
    StatusPath(path);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    /* No setup folder yet = the utility was never installed. Return WITHOUT touching the
       suppression cache or the counter, and this is the whole reason the commit is down here:
       caching a state that was never written meant the first SUCCESSFUL write got suppressed
       as a duplicate for up to 10 s. The harness caught exactly that - the .asi starts before
       the folder exists far more often than not, so the lamp would have been dark on the one
       run where it matters most. */
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, buf, (DWORD)n, &wrote, NULL);
    CloseHandle(h);
    if (!ok || wrote != (DWORD)n) return;   /* same rule: only a real write counts */

    g_statusGen = gen;
    lastForced  = now;
    if (cmpLen <= (int)sizeof(prev)) {
        for (int i = 0; i < cmpLen; i++) prev[i] = buf[i];
        prevLen = cmpLen;
    } else {
        prevLen = -1;   /* cannot cache it: always rewrite rather than lie about being fresh */
    }

    Log(K_STATUS, gen, (DWORD)(effective ? 1 : 0), (DWORD)acq, (DWORD)g_lastEffectHr);
}
