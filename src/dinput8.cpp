/*
 * dinput8.dll proxy for Call of Juarez: Gunslinger (32-bit).
 *
 * The game loads dinput8.dll with LoadLibraryA and creates a buffered
 * GUID_SysMouse device (CInput::Initialize, FUN_00446110 / FUN_00446760), which
 * it reads every frame with GetDeviceData (FUN_00574050) and turns into camera
 * look with no check for an active pad. That native path ran next to the
 * mouse-to-stick injection, so the camera got the mouse twice.
 *
 * This proxy wraps IDirectInput8A and the mouse device only. While the
 * xinput1_3.dll proxy reports a connected pad and stick mode (MI_IsActive),
 * mouse motion is removed from what the game sees and handed to the xinput
 * proxy instead; buttons 0-2 are handled the same way when mapped
 * (MI_ButtonsMapped). With no pad, or after F10, the mouse passes through
 * untouched. Every other device and call is forwarded as is.
 */
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef int  (WINAPI *PFN_MI_Int)(void);
typedef void (WINAPI *PFN_MI_Move)(LONG, LONG);
typedef void (WINAPI *PFN_MI_Button)(int, int);

static HMODULE g_self, g_real;
static PFN_DI8Create p_Create;
static char g_dir[MAX_PATH];
static int g_passWheel = 1;

static PFN_MI_Int    mi_IsActive, mi_ButtonsMapped;
static PFN_MI_Move   mi_Move;
static PFN_MI_Button mi_Button;

static const GUID kSysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static bool ResolveMI()
{
    if (mi_IsActive) return true;
    char path[MAX_PATH];
    strcpy(path, g_dir);
    strcat(path, "xinput1_3.dll");
    HMODULE m = GetModuleHandleA(path);   /* only our proxy in the game folder, never the system one */
    if (!m) return false;
    mi_ButtonsMapped = (PFN_MI_Int)GetProcAddress(m, "MI_ButtonsMapped");
    mi_Move          = (PFN_MI_Move)GetProcAddress(m, "MI_MouseMove");
    mi_Button        = (PFN_MI_Button)GetProcAddress(m, "MI_MouseButton");
    PFN_MI_Int act   = (PFN_MI_Int)GetProcAddress(m, "MI_IsActive");
    if (!mi_ButtonsMapped || !mi_Move || !mi_Button || !act) return false;
    mi_IsActive = act;
    return true;
}

/* ---------------- mouse device wrapper ---------------- */

class MouseDev : public IDirectInputDevice8A {
public:
    explicit MouseDev(IDirectInputDevice8A *r) : m_r(r) {}

    /* IUnknown: the wrapper shares the real object's reference count */
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *ppv) {
        HRESULT hr = m_r->QueryInterface(riid, ppv);
        if (SUCCEEDED(hr) && ppv && *ppv == (LPVOID)m_r) *ppv = this;
        return hr;
    }
    STDMETHODIMP_(ULONG) AddRef() { return m_r->AddRef(); }
    STDMETHODIMP_(ULONG) Release() {
        ULONG n = m_r->Release();
        if (n == 0) delete this;
        return n;
    }

    STDMETHODIMP GetCapabilities(LPDIDEVCAPS a) { return m_r->GetCapabilities(a); }
    STDMETHODIMP EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA a, LPVOID b, DWORD c) { return m_r->EnumObjects(a, b, c); }
    STDMETHODIMP GetProperty(REFGUID a, LPDIPROPHEADER b) { return m_r->GetProperty(a, b); }
    STDMETHODIMP SetProperty(REFGUID a, LPCDIPROPHEADER b) { return m_r->SetProperty(a, b); }
    STDMETHODIMP Acquire() { return m_r->Acquire(); }
    STDMETHODIMP Unacquire() { return m_r->Unacquire(); }

    STDMETHODIMP GetDeviceState(DWORD cb, LPVOID data) {
        HRESULT hr = m_r->GetDeviceState(cb, data);
        if (FAILED(hr) || !data || cb < sizeof(DIMOUSESTATE) || !ResolveMI()) return hr;
        DIMOUSESTATE *s = (DIMOUSESTATE *)data;   /* DIMOUSESTATE2 shares the same prefix */
        if (mi_IsActive()) {
            mi_Move(s->lX, s->lY);
            s->lX = 0;
            s->lY = 0;
            if (!g_passWheel) s->lZ = 0;
        }
        if (mi_ButtonsMapped()) {
            for (int i = 0; i < 3; i++) {
                mi_Button(i, (s->rgbButtons[i] & 0x80) != 0);
                s->rgbButtons[i] = 0;
            }
        }
        return hr;
    }

    STDMETHODIMP GetDeviceData(DWORD cb, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags) {
        HRESULT hr = m_r->GetDeviceData(cb, rg, inout, flags);
        if (FAILED(hr) || !rg || !inout || (flags & DIGDD_PEEK) || cb < sizeof(DIDEVICEOBJECTDATA_DX3))
            return hr;
        if (!ResolveMI()) return hr;

        int stealMove = mi_IsActive();
        int mapButtons = mi_ButtonsMapped();
        if (!stealMove && !mapButtons) return hr;
        BYTE *base = (BYTE *)rg;
        DWORD n = *inout, out = 0;
        for (DWORD i = 0; i < n; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)(base + i * cb);
            bool keep = true;
            switch (e->dwOfs) {
            case DIMOFS_X: if (stealMove) { mi_Move((LONG)e->dwData, 0); keep = false; } break;
            case DIMOFS_Y: if (stealMove) { mi_Move(0, (LONG)e->dwData); keep = false; } break;
            case DIMOFS_Z: keep = !stealMove || g_passWheel != 0; break;
            case DIMOFS_BUTTON0: case DIMOFS_BUTTON1: case DIMOFS_BUTTON2:
                if (mapButtons) {
                    mi_Button((int)(e->dwOfs - DIMOFS_BUTTON0), (e->dwData & 0x80) != 0);
                    keep = false;
                }
                break;
            }
            if (keep) {
                if (out != i) memmove(base + out * cb, e, cb);
                out++;
            }
        }
        *inout = out;
        return hr;
    }

    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT a) { return m_r->SetDataFormat(a); }
    STDMETHODIMP SetEventNotification(HANDLE a) { return m_r->SetEventNotification(a); }
    STDMETHODIMP SetCooperativeLevel(HWND a, DWORD b) { return m_r->SetCooperativeLevel(a, b); }
    STDMETHODIMP GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA a, DWORD b, DWORD c) { return m_r->GetObjectInfo(a, b, c); }
    STDMETHODIMP GetDeviceInfo(LPDIDEVICEINSTANCEA a) { return m_r->GetDeviceInfo(a); }
    STDMETHODIMP RunControlPanel(HWND a, DWORD b) { return m_r->RunControlPanel(a, b); }
    STDMETHODIMP Initialize(HINSTANCE a, DWORD b, REFGUID c) { return m_r->Initialize(a, b, c); }
    STDMETHODIMP CreateEffect(REFGUID a, LPCDIEFFECT b, LPDIRECTINPUTEFFECT *c, LPUNKNOWN d) { return m_r->CreateEffect(a, b, c, d); }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKA a, LPVOID b, DWORD c) { return m_r->EnumEffects(a, b, c); }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOA a, REFGUID b) { return m_r->GetEffectInfo(a, b); }
    STDMETHODIMP GetForceFeedbackState(LPDWORD a) { return m_r->GetForceFeedbackState(a); }
    STDMETHODIMP SendForceFeedbackCommand(DWORD a) { return m_r->SendForceFeedbackCommand(a); }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK a, LPVOID b, DWORD c) { return m_r->EnumCreatedEffectObjects(a, b, c); }
    STDMETHODIMP Escape(LPDIEFFESCAPE a) { return m_r->Escape(a); }
    STDMETHODIMP Poll() { return m_r->Poll(); }
    STDMETHODIMP SendDeviceData(DWORD a, LPCDIDEVICEOBJECTDATA b, LPDWORD c, DWORD d) { return m_r->SendDeviceData(a, b, c, d); }
    STDMETHODIMP EnumEffectsInFile(LPCSTR a, LPDIENUMEFFECTSINFILECALLBACK b, LPVOID c, DWORD d) { return m_r->EnumEffectsInFile(a, b, c, d); }
    STDMETHODIMP WriteEffectToFile(LPCSTR a, DWORD b, LPDIFILEEFFECT c, DWORD d) { return m_r->WriteEffectToFile(a, b, c, d); }
    STDMETHODIMP BuildActionMap(LPDIACTIONFORMATA a, LPCSTR b, DWORD c) { return m_r->BuildActionMap(a, b, c); }
    STDMETHODIMP SetActionMap(LPDIACTIONFORMATA a, LPCSTR b, DWORD c) { return m_r->SetActionMap(a, b, c); }
    STDMETHODIMP GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA a) { return m_r->GetImageInfo(a); }

private:
    IDirectInputDevice8A *m_r;
};

/* ---------------- IDirectInput8A wrapper ---------------- */

class DI8 : public IDirectInput8A {
public:
    explicit DI8(IDirectInput8A *r) : m_r(r) {}

    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *ppv) {
        HRESULT hr = m_r->QueryInterface(riid, ppv);
        if (SUCCEEDED(hr) && ppv && *ppv == (LPVOID)m_r) *ppv = this;
        return hr;
    }
    STDMETHODIMP_(ULONG) AddRef() { return m_r->AddRef(); }
    STDMETHODIMP_(ULONG) Release() {
        ULONG n = m_r->Release();
        if (n == 0) delete this;
        return n;
    }

    STDMETHODIMP CreateDevice(REFGUID g, LPDIRECTINPUTDEVICE8A *dev, LPUNKNOWN outer) {
        HRESULT hr = m_r->CreateDevice(g, dev, outer);
        if (SUCCEEDED(hr) && dev && *dev && !outer && IsEqualGUID(g, kSysMouse))
            *dev = new MouseDev(*dev);
        return hr;
    }
    STDMETHODIMP EnumDevices(DWORD a, LPDIENUMDEVICESCALLBACKA b, LPVOID c, DWORD d) { return m_r->EnumDevices(a, b, c, d); }
    STDMETHODIMP GetDeviceStatus(REFGUID a) { return m_r->GetDeviceStatus(a); }
    STDMETHODIMP RunControlPanel(HWND a, DWORD b) { return m_r->RunControlPanel(a, b); }
    STDMETHODIMP Initialize(HINSTANCE a, DWORD b) { return m_r->Initialize(a, b); }
    STDMETHODIMP FindDevice(REFGUID a, LPCSTR b, LPGUID c) { return m_r->FindDevice(a, b, c); }
    STDMETHODIMP EnumDevicesBySemantics(LPCSTR a, LPDIACTIONFORMATA b, LPDIENUMDEVICESBYSEMANTICSCBA c, LPVOID d, DWORD e) { return m_r->EnumDevicesBySemantics(a, b, c, d, e); }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK a, LPDICONFIGUREDEVICESPARAMSA b, DWORD c, LPVOID d) { return m_r->ConfigureDevices(a, b, c, d); }

private:
    IDirectInput8A *m_r;
};

/* ---------------- exports ---------------- */

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    if (!p_Create) return E_FAIL;
    HRESULT hr = p_Create(inst, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out && !outer && IsEqualIID(riid, IID_IDirectInput8A))
        *out = static_cast<IDirectInput8A *>(new DI8((IDirectInput8A *)*out));
    return hr;
}

typedef HRESULT (WINAPI *PFN_Void)(void);
typedef HRESULT (WINAPI *PFN_GetClass)(REFCLSID, REFIID, LPVOID *);
typedef LPCDIDATAFORMAT (WINAPI *PFN_GetDf)(void);

extern "C" HRESULT WINAPI DllCanUnloadNow(void)
{
    PFN_Void f = g_real ? (PFN_Void)GetProcAddress(g_real, "DllCanUnloadNow") : NULL;
    return f ? f() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *p)
{
    PFN_GetClass f = g_real ? (PFN_GetClass)GetProcAddress(g_real, "DllGetClassObject") : NULL;
    return f ? f(c, i, p) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer(void)
{
    PFN_Void f = g_real ? (PFN_Void)GetProcAddress(g_real, "DllRegisterServer") : NULL;
    return f ? f() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer(void)
{
    PFN_Void f = g_real ? (PFN_Void)GetProcAddress(g_real, "DllUnregisterServer") : NULL;
    return f ? f() : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick(void)
{
    PFN_GetDf f = g_real ? (PFN_GetDf)GetProcAddress(g_real, "GetdfDIJoystick") : NULL;
    return f ? f() : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char sys[MAX_PATH];
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        char *slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;

        char ini[MAX_PATH];
        strcpy(ini, g_dir);
        strcat(ini, "xinput_mouse.ini");
        g_passWheel = GetPrivateProfileIntA("Mouse", "PassWheel", 1, ini);

        GetSystemDirectoryA(sys, MAX_PATH);          /* SysWOW64 for a 32-bit process */
        strcat(sys, "\\dinput8.dll");
        g_real = LoadLibraryA(sys);
        if (g_real) p_Create = (PFN_DI8Create)GetProcAddress(g_real, "DirectInput8Create");
    }
    return TRUE;
}
