/*
 * xinput1_3.dll proxy for Call of Juarez: Gunslinger (32-bit).
 *
 * Keeps the gamepad as the main input and lets the mouse aim at the same time.
 * The game imports XInputGetState/SetState/GetCapabilities from XINPUT1_3.dll by
 * ordinal; every export is forwarded to the real system DLL, only
 * XInputGetState is modified.
 *
 * Mouse data comes from the companion dinput8.dll proxy, which takes it out of
 * the game's own DirectInput mouse device and calls the MI_* exports below.
 * MouseMode 0 feeds the mouse through the right stick while that stick is
 * centred; a deflected stick always wins (the game then sees only the pad); MouseMode 1 leaves mouse movement with the game's native mouse look and
 * only maps the mouse buttons. No Raw Input registration is made, because
 * DirectInput itself uses Raw Input and a second registration in the same
 * process steals its data.
 *
 * Steam Input (the overlay, gameoverlayrenderer.dll) detours the exported
 * XInputGetState of whatever xinput1_3.dll the game loaded - i.e. this one - to
 * supply its virtual controller, so code in the export never runs. Therefore the
 * export is a plain pass-through, and the game's import table entry is pointed
 * at MixedGetState instead, which calls the (Steam-detoured) export for the pad
 * state and then mixes the mouse in. MI_Install does the patch; it is called
 * from a short-lived thread and again by the dinput8 proxy.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *PFN_SetState)(DWORD, XINPUT_VIBRATION *);
typedef DWORD (WINAPI *PFN_GetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES *);
typedef void  (WINAPI *PFN_Enable)(BOOL);
typedef DWORD (WINAPI *PFN_Guids)(DWORD, LPWSTR, UINT *, LPWSTR, UINT *);
typedef DWORD (WINAPI *PFN_Battery)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);
typedef DWORD (WINAPI *PFN_Keystroke)(DWORD, DWORD, XINPUT_KEYSTROKE *);

static HMODULE g_real, g_self;
static char g_log[MAX_PATH];
static volatile LONG g_installed;

static void Log(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    if (!g_log[0] || !(f = fopen(g_log, "a"))) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
static PFN_GetState  p_GetState;
static PFN_SetState  p_SetState;
static PFN_GetCaps   p_GetCaps;
static PFN_Enable    p_Enable;
static PFN_Guids     p_Guids;
static PFN_Battery   p_Battery;
static PFN_Keystroke p_Keystroke;

/* settings (xinput_mouse.ini next to the DLL) */
static char  g_ini[MAX_PATH];
static float g_sens      = 0.3f;   /* 1.0 = ~1000 counts/s gives full stick */
static float g_sensY     = 1.0f;   /* extra vertical multiplier */
static float g_deadComp  = 0.10f;  /* fraction of full range added to skip the game's dead zone */
static float g_windowMs  = 30.0f;  /* mouse speed is averaged over this window */
static float g_curve     = 1.0f;   /* response exponent: <1 boosts small moves, >1 damps them */
static int   g_invertY   = 0;
static int   g_buttons   = 1;      /* 1: LMB -> RT, RMB -> LT, MMB -> RB */
static int   g_enabled   = 1;
static int   g_mode      = 0;      /* 0: mouse -> right stick, 1: mouse goes to the game natively */
static int   g_toggleVk  = 'P';    /* mouse aim on/off */
static int   g_modeVk    = 'O';    /* switch g_mode */
static int   g_slowerVk  = 'K';    /* Sensitivity -10% (saved to the ini) */
static int   g_fasterVk  = 'L';    /* Sensitivity +10% (saved to the ini) */
static float g_override  = 0.27f;  /* right stick past this deflection: mouse is ignored */
static int   g_menuMouse = 1;      /* 1: while a game menu is open the mouse goes to the game natively */

static volatile LONG g_dx, g_dy;
static volatile LONG g_btn[3];
static volatile LONG g_padOk;      /* pad 0 answered the last XInputGetState */
static volatile LONG g_stickBusy;  /* right stick deflected past g_override */

static void LoadSettings(HMODULE self)
{
    char buf[64];
    GetModuleFileNameA(self, g_ini, MAX_PATH);
    char *dot = strrchr(g_ini, '\\');
    if (!dot) { g_ini[0] = 0; return; }
    strcpy(dot + 1, "xinput_mouse.ini");
#define GETF(key, var, def) do { GetPrivateProfileStringA("Mouse", key, def, buf, sizeof buf, g_ini); var = (float)atof(buf); } while (0)
    GETF("Sensitivity", g_sens, "0.3");
    GETF("SensitivityY", g_sensY, "1.0");
    GETF("DeadzoneComp", g_deadComp, "0.10");
    GETF("SmoothingMs", g_windowMs, "30");
    GETF("Curve", g_curve, "1.0");
    GETF("StickOverride", g_override, "0.27");
    if (g_curve <= 0.05f) g_curve = 1.0f;
    if (g_windowMs < 5.0f) g_windowMs = 5.0f;
    if (g_windowMs > 200.0f) g_windowMs = 200.0f;
    g_invertY  = GetPrivateProfileIntA("Mouse", "InvertY", 0, g_ini);
    g_buttons  = GetPrivateProfileIntA("Mouse", "MouseButtons", 1, g_ini);
    g_enabled  = GetPrivateProfileIntA("Mouse", "Enabled", 1, g_ini);
    g_mode     = GetPrivateProfileIntA("Mouse", "MouseMode", 0, g_ini);
    g_menuMouse = GetPrivateProfileIntA("Mouse", "MenuMouse", 1, g_ini);
    g_toggleVk = GetPrivateProfileIntA("Mouse", "ToggleKey", 'P', g_ini);
    g_modeVk   = GetPrivateProfileIntA("Mouse", "ModeKey", 'O', g_ini);
    g_slowerVk = GetPrivateProfileIntA("Mouse", "SlowerKey", 'K', g_ini);
    g_fasterVk = GetPrivateProfileIntA("Mouse", "FasterKey", 'L', g_ini);
}

static void SaveSetting(const char *key, const char *fmt, double v)
{
    char buf[64];
    if (!g_ini[0]) return;
    sprintf(buf, fmt, v);
    WritePrivateProfileStringA("Mouse", key, buf, g_ini);
}

/* ---- menu detection ----
   CoJGunslinger.exe (no ASLR, base 0x400000) keeps two menu module singletons:
   s_MainMenuModule at 0x02E65F90 and s_GameMenuModule (in-game / pause menu) at
   0x02E65F94. GameDI::CanShowGameMenu (0xA1ED10) and CanPauseGame (0xA1F330)
   treat byte +0x59C of either as "module is active". Checked at runtime:
   title screen 0, main menu 1. */
#define MENU_MAIN_PTR  0x02E65F90
#define MENU_GAME_PTR  0x02E65F94
#define MENU_ACTIVE    0x59C

static int ModuleActive(DWORD global)
{
    __try {
        BYTE *m = *(BYTE **)global;
        return m && m[MENU_ACTIVE] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int g_inMenu = -1;

static int InMenu(void)
{
    if (!g_menuMouse || GetModuleHandleA(NULL) != (HMODULE)0x400000) return 0;
    int m = ModuleActive(MENU_MAIN_PTR) ? 1 : ModuleActive(MENU_GAME_PTR) ? 2 : 0;
    if (m != g_inMenu) {
        Log("%lu ms: %s", GetTickCount(), m == 1 ? "main menu" : m == 2 ? "game menu" : "gameplay");
        g_inMenu = m;
        /* a button released while the game had it natively would stay latched */
        for (int i = 0; i < 3; i++) InterlockedExchange(&g_btn[i], 0);
    }
    return m != 0;
}

/* ---- interface for the dinput8.dll proxy ---- */

/* 1 when mouse movement should be taken from the game: always in stick mode, and
   in native mode while the right stick is deflected (the stick wins). */
int WINAPI MI_IsActive(void)
{
    return g_enabled && g_padOk && (g_mode == 0 || g_stickBusy) && g_inMenu <= 0;
}

/* 1 when mouse buttons are mapped to pad triggers (and hidden from the game). */
int WINAPI MI_ButtonsMapped(void)
{
    return g_enabled && g_padOk && g_buttons && g_inMenu <= 0;
}

void WINAPI MI_MouseMove(LONG dx, LONG dy)
{
    InterlockedAdd(&g_dx, dx);
    InterlockedAdd(&g_dy, dy);
}

void WINAPI MI_MouseButton(int index, int down)
{
    if (index >= 0 && index < 3) InterlockedExchange(&g_btn[index], down ? 1 : 0);
}

static short ClampS(float v)
{
    if (v > 32767.0f) return 32767;
    if (v < -32767.0f) return -32767;
    return (short)v;
}

/* Mouse speed (counts/second) = counts received in the last g_windowMs / window.
   A fixed window gives a steady value even when the game reads the mouse and the
   pad at uneven points of a frame, so the stick no longer jitters between 0 and
   double the real speed (that jitter upset the game's stick acceleration). */
#define NSAMP 128
static struct { double t; LONG dx, dy; } g_samp[NSAMP];
static int g_head, g_count;
static LARGE_INTEGER g_freq;
static float g_velX, g_velY;

static void ResetMouse(void)
{
    InterlockedExchange(&g_dx, 0);
    InterlockedExchange(&g_dy, 0);
    g_count = 0;
    g_velX = g_velY = 0.0f;
}

static void UpdateMouseState(void)
{
    LARGE_INTEGER now;
    if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&now);
    double t = (double)now.QuadPart / (double)g_freq.QuadPart;
    double win = g_windowMs / 1000.0;

    LONG dx = InterlockedExchange(&g_dx, 0);
    LONG dy = InterlockedExchange(&g_dy, 0);
    if (dx || dy) {
        g_samp[g_head].t = t; g_samp[g_head].dx = dx; g_samp[g_head].dy = dy;
        g_head = (g_head + 1) % NSAMP;
        if (g_count < NSAMP) g_count++;
    }
    LONG sx = 0, sy = 0;
    int kept = 0;
    for (int i = 0; i < g_count; i++) {
        int k = (g_head - 1 - i + NSAMP) % NSAMP;
        if (t - g_samp[k].t > win) break;
        sx += g_samp[k].dx; sy += g_samp[k].dy; kept++;
    }
    g_count = kept;
    g_velX = (float)(sx / win);
    g_velY = (float)(sy / win);
}

static void HandleHotkeys(void)
{
    static int prev[4];
    int vk[4] = { g_toggleVk, g_modeVk, g_slowerVk, g_fasterVk };
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    int focused = pid == GetCurrentProcessId();
    for (int i = 0; i < 4; i++) {
        int down = focused && vk[i] && (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
        if (down && !prev[i]) {
            switch (i) {
            case 0: g_enabled = !g_enabled; ResetMouse(); break;
            case 1: g_mode = !g_mode; ResetMouse(); SaveSetting("MouseMode", "%.0f", g_mode); break;
            case 2: g_sens *= 0.9f; if (g_sens < 0.02f) g_sens = 0.02f; SaveSetting("Sensitivity", "%.3f", g_sens); break;
            case 3: g_sens *= 1.1f; if (g_sens > 10.0f) g_sens = 10.0f; SaveSetting("Sensitivity", "%.3f", g_sens); break;
            }
        }
        prev[i] = down;
    }
}

/* The export, as seen through Steam's detour (or not, without Steam). */
static PFN_GetState p_PadState;
static LONG g_calls;

static DWORD WINAPI MixedGetState(DWORD user, XINPUT_STATE *st)
{
    DWORD r = p_PadState ? p_PadState(user, st) : ERROR_DEVICE_NOT_CONNECTED;
    if (user == 0 && ++g_calls == 600)
        Log("MixedGetState: 600 calls, pad 0 %s", r == ERROR_SUCCESS ? "connected" : "not connected");
    if (user != 0) return r;
    InterlockedExchange(&g_padOk, r == ERROR_SUCCESS);
    if (r != ERROR_SUCCESS || !st) return r;

    HandleHotkeys();
    if (!g_enabled || InMenu()) {
        ResetMouse();
        return r;
    }

    /* real stick, 0..1 */
    float sx = st->Gamepad.sThumbRX / 32767.0f;
    float sy = st->Gamepad.sThumbRY / 32767.0f;
    int busy = sqrtf(sx * sx + sy * sy) > g_override;
    InterlockedExchange(&g_stickBusy, busy);

    if (g_mode == 0) {
        UpdateMouseState();
        if (busy) {
            /* the right stick always wins: pass it through untouched and throw away
               mouse motion made meanwhile, so it doesn't jump in after release */
            ResetMouse();
        } else {
            /* mouse as a stick vector (full deflection = 1); replaces the idle stick */
            float mx = g_velX / 1000.0f * g_sens;
            float my = g_velY / 1000.0f * g_sens * g_sensY * (g_invertY ? 1.0f : -1.0f);
            float mm = sqrtf(mx * mx + my * my);
            if (mm > 0.01f) {
                float m = mm;
                if (g_curve != 1.0f) m = powf(m, g_curve);
                m = m * (1.0f - g_deadComp) + g_deadComp;   /* lift past the game's dead zone */
                if (m > 1.0f) m = 1.0f;
                st->Gamepad.sThumbRX = ClampS(mx * m / mm * 32767.0f);
                st->Gamepad.sThumbRY = ClampS(my * m / mm * 32767.0f);
            }
        }
    } else {
        ResetMouse();   /* native mode: movement stays with the game (dropped while the stick is busy) */
    }

    if (g_buttons) {
        if (g_btn[0]) st->Gamepad.bRightTrigger = 255;
        if (g_btn[1]) st->Gamepad.bLeftTrigger = 255;
        if (g_btn[2]) st->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    }
    return r;
}

DWORD WINAPI XInputGetState(DWORD user, XINPUT_STATE *st)
{
    return p_GetState ? p_GetState(user, st) : ERROR_DEVICE_NOT_CONNECTED;
}

/* Point the exe's XINPUT1_3 import of XInputGetState at MixedGetState. */
int WINAPI MI_Install(void)
{
    if (g_installed) return 1;
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    void *exp = (void *)GetProcAddress(g_self, "XInputGetState");
    if (!exe || !exp) return 0;
    p_PadState = (PFN_GetState)exp;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return 0;
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(exe + dir->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(exe + d->Name), "xinput1_3.dll") != 0) continue;
        for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(exe + d->FirstThunk); t->u1.Function; t++) {
            if ((void *)t->u1.Function != exp) continue;
            DWORD old;
            if (VirtualProtect(&t->u1.Function, sizeof t->u1.Function, PAGE_READWRITE, &old)) {
                t->u1.Function = (DWORD_PTR)MixedGetState;
                VirtualProtect(&t->u1.Function, sizeof t->u1.Function, old, &old);
                patched++;
            }
        }
    }
    if (patched) {
        InterlockedExchange(&g_installed, 1);
        BYTE *code = (BYTE *)exp;
        Log("installed: %d import entr%s patched; export starts with %02x (%s)", patched,
            patched == 1 ? "y" : "ies", code[0], code[0] == 0xE9 ? "detoured, likely Steam Input" : "not detoured");
    }
    return patched != 0;
}

static DWORD WINAPI InstallThread(LPVOID unused)
{
    for (int i = 0; i < 100 && !g_installed; i++) {
        Sleep(100);
        MI_Install();
    }
    if (!g_installed) Log("install failed: XInputGetState import not found in the exe");
    return 0;
}

DWORD WINAPI XInputSetState(DWORD user, XINPUT_VIBRATION *v)
{
    return p_SetState ? p_SetState(user, v) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES *c)
{
    return p_GetCaps ? p_GetCaps(user, flags, c) : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI XInputEnable(BOOL e)
{
    if (p_Enable) p_Enable(e);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, LPWSTR a, UINT *an, LPWSTR b, UINT *bn)
{
    return p_Guids ? p_Guids(user, a, an, b, bn) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION *b)
{
    return p_Battery ? p_Battery(user, type, b) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, XINPUT_KEYSTROKE *k)
{
    return p_Keystroke ? p_Keystroke(user, reserved, k) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char sys[MAX_PATH];
        DisableThreadLibraryCalls(inst);
        GetSystemDirectoryA(sys, MAX_PATH);          /* SysWOW64 for a 32-bit process */
        strcat(sys, "\\xinput1_3.dll");
        g_real = LoadLibraryA(sys);
        if (g_real) {
            p_GetState  = (PFN_GetState)GetProcAddress(g_real, "XInputGetState");
            p_SetState  = (PFN_SetState)GetProcAddress(g_real, "XInputSetState");
            p_GetCaps   = (PFN_GetCaps)GetProcAddress(g_real, "XInputGetCapabilities");
            p_Enable    = (PFN_Enable)GetProcAddress(g_real, "XInputEnable");
            p_Guids     = (PFN_Guids)GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids");
            p_Battery   = (PFN_Battery)GetProcAddress(g_real, "XInputGetBatteryInformation");
            p_Keystroke = (PFN_Keystroke)GetProcAddress(g_real, "XInputGetKeystroke");
        }
        g_self = inst;
        LoadSettings(inst);
        if (g_ini[0]) {
            strcpy(g_log, g_ini);
            strcpy(strrchr(g_log, '.'), ".log");
            DeleteFileA(g_log);
        }
        Log("xinput_mouse loaded (mode %d, sensitivity %.3f)", g_mode, g_sens);
        CloseHandle(CreateThread(NULL, 0, InstallThread, NULL, 0, NULL));
    }
    return TRUE;
}
