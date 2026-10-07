#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>

#if !defined(_M_IX86)
#error MafiaAimHost must be built for Win32/x86.
#endif

typedef void (__cdecl *AimMouseFn)(LONG *lx, LONG *ly);
typedef void (__cdecl *AimInitFn)(void);
typedef int (__cdecl *AimCrouchFn)(uintptr_t human, int requested);
typedef struct HostVector3 { float x, y, z; } HostVector3;
typedef int (__cdecl *AimShotFn)(uintptr_t game, uintptr_t shooter, HostVector3 *origin,
                                HostVector3 *direction, float *power, int *count);
static AimShotFn volatile g_shot;
typedef float (__cdecl *AimHitFn)(uintptr_t game, uintptr_t victim, uintptr_t source, int type, float damage);
static AimHitFn volatile g_hitDamage;
typedef int (__cdecl *AimWheelHitFn)(uintptr_t game, uintptr_t car, uintptr_t source,
                                    int type, uintptr_t frame, uint32_t shot, uintptr_t *wheel, int *index);
static AimWheelHitFn volatile g_wheelHit;
static uint32_t g_playerShotSequence;
static SRWLOCK g_logicLock = SRWLOCK_INIT;
typedef void (__fastcall *HostShootFn)(void *game, void *unused, void *shooter,
                                     HostVector3 origin, HostVector3 direction,
                                     float power, int effect, void *frame, int count);
static HostShootFn g_originalShoot;
static void TraceNativeShot(void *game, void *shooter);

static void __fastcall ArcadeShootHook(void *game, void *unused, void *shooter,
                                     HostVector3 origin, HostVector3 direction,
                                     float power, int effect, void *frame, int count)
{
    HostVector3 adjustedOrigin = origin, adjustedDirection = direction;
    float adjustedPower = power;
    int adjustedCount = count, accepted = 0;
    AimShotFn callback;
    (void)unused;
    AcquireSRWLockShared(&g_logicLock);
    callback = g_shot;
    if (callback)
    {
        __try
        {
            accepted = callback((uintptr_t)game, (uintptr_t)shooter, &adjustedOrigin,
                                &adjustedDirection, &adjustedPower, &adjustedCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_shot = NULL;
        }
    }
    ReleaseSRWLockShared(&g_logicLock);
    if (accepted)
    {
        origin = adjustedOrigin;
        direction = adjustedDirection;
        power = adjustedPower;
        count = adjustedCount;
    }
    if (game && *(void **)((BYTE *)game + 0xE4u) == shooter && ++g_playerShotSequence == 0)
        ++g_playerShotSequence;
    g_originalShoot(game, NULL, shooter, origin, direction, power, effect, frame, count);
    if (accepted)
        TraceNativeShot(game, shooter);
}

typedef void (__fastcall *HumanCrouchFn)(void *human, void *unused, int requested);
typedef HRESULT (STDMETHODCALLTYPE *GetDeviceStateFn)(void *self, DWORD size, LPVOID data);
typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

static HINSTANCE g_self;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static wchar_t g_logicPath[MAX_PATH];
static wchar_t g_loadedCopy[MAX_PATH];
static HMODULE g_logic;
static AimMouseFn volatile g_aim;
static AimCrouchFn volatile g_crouch;
static HumanCrouchFn g_originalCrouch;
typedef void (__stdcall *FreerideStoreFn)(int model, int flags, float cars,
                                        float pedestrians, float police);
static FreerideStoreFn g_freerideStore;
typedef void (__fastcall *PoliceStateFn)(void *manager, void *unused, int enabled,
                                       int vehicleTier, int cacheFlag);
typedef void (__cdecl *GameLoopFn)(void);
static PoliceStateFn g_policeState;
static GameLoopFn g_gameLoop;
static int g_tutorialFreeride;
static volatile LONG g_inFlight;
static volatile LONG g_mouseCalls;
static GetDeviceStateFn g_stateA, g_stateW;
static int g_generation;

static void Log(const char *format, ...)
{
    char line[256];
    DWORD written;
    int length;
    va_list args;
    if (g_log == INVALID_HANDLE_VALUE)
    {
        char path[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, path);
        if (!n || n > MAX_PATH - 32)
            return;
        lstrcatA(path, "MafiaAimHost.log");
        g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log == INVALID_HANDLE_VALUE)
            return;
    }
    va_start(args, format);
    length = _vsnprintf(line, sizeof(line) - 3, format, args);
    va_end(args);
    if (length < 0 || length > (int)sizeof(line) - 3)
        length = (int)sizeof(line) - 3;
    line[length++] = '\r';
    line[length++] = '\n';
    WriteFile(g_log, line, (DWORD)length, &written, NULL);
    FlushFileBuffers(g_log);
}

static void TraceNativeShot(void *game, void *shooter)
{
    static int traced;
    uintptr_t first, end, record;
    HostVector3 position, velocity;
    float damage, range;
    if (traced >= 24)
        return;
    __try
    {
        first = *(uintptr_t *)((BYTE *)game + 0x1D8u);
        end = *(uintptr_t *)((BYTE *)game + 0x1DCu);
        if (!first || end <= first || (end - first) % 0x5Cu || end - first > 0x10000u)
            return;
        record = end - 0x5Cu;
        if (*(void **)(record + 0x2Cu) != shooter)
            return;
        position = *(HostVector3 *)record;
        velocity = *(HostVector3 *)(record + 0xCu);
        damage = *(float *)(record + 0x28u);
        range = *(float *)(record + 0x18u);
        ++traced;
        Log("native bullet owner=%p damage=%.1f range=%.2f pos=%.2f,%.2f,%.2f vel=%.3f,%.3f,%.3f",
            shooter, damage, range, position.x, position.y, position.z, velocity.x, velocity.y, velocity.z);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("native bullet trace unavailable");
    }
}

/* ---- hot-reloadable logic ---------------------------------------------- */

static void CallAim(LONG *lx, LONG *ly)
{
    AimMouseFn fn;
    AcquireSRWLockShared(&g_logicLock);
    InterlockedIncrement(&g_inFlight);
    fn = g_aim;
    if (fn)
    {
        __try
        {
            fn(lx, ly);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_aim = NULL;
            Log("logic crashed; disabled until the next reload");
        }
    }
    InterlockedDecrement(&g_inFlight);
    ReleaseSRWLockShared(&g_logicLock);
}

static int ReadStamp(FILETIME *time, DWORD *size)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(g_logicPath, GetFileExInfoStandard, &data) || data.nFileSizeLow == 0)
        return 0;
    *time = data.ftLastWriteTime;
    *size = data.nFileSizeLow;
    return 1;
}

static int ReloadLogic(void)
{
    wchar_t temp[MAX_PATH], copy[MAX_PATH];
    HMODULE module, old;
    AimMouseFn fn;
    AimInitFn init;
    AimCrouchFn crouch;
    AimShotFn shot;
    AimHitFn hitDamage;
    AimWheelHitFn wheelHit;

    GetTempPathW(MAX_PATH, temp);
    swprintf(copy, MAX_PATH, L"%lsMafiaAimLogic_%lu_%d.dll", temp, GetCurrentProcessId(), ++g_generation);
    if (!CopyFileW(g_logicPath, copy, FALSE))
        return 0;
    module = LoadLibraryW(copy);
    if (!module)
    {
        DeleteFileW(copy);
        return 0;
    }
    fn = (AimMouseFn)GetProcAddress(module, "AimMouse");
    init = (AimInitFn)GetProcAddress(module, "AimInit");
    crouch = (AimCrouchFn)GetProcAddress(module, "AimCrouch");
    shot = (AimShotFn)GetProcAddress(module, "AimArcadeShot");
    hitDamage = (AimHitFn)GetProcAddress(module, "AimArcadeHitDamage");
    wheelHit = (AimWheelHitFn)GetProcAddress(module, "AimArcadeWheelHit");
    if (!fn)
    {
        Log("logic has no AimMouse export");
        FreeLibrary(module);
        DeleteFileW(copy);
        return 0;
    }
    if (init)
        init();

    AcquireSRWLockExclusive(&g_logicLock);
    old = g_logic;
    g_logic = module;
    g_aim = fn;
    g_crouch = crouch;
    g_shot = shot;
    g_hitDamage = hitDamage;
    g_wheelHit = wheelHit;
    if (old)
    {
        FreeLibrary(old);
        DeleteFileW(g_loadedCopy);
    }
    lstrcpyW(g_loadedCopy, copy);
    ReleaseSRWLockExclusive(&g_logicLock);
    Log("logic loaded (generation %d)", g_generation);
    return 1;
}

static DWORD WINAPI WatchThread(void *unused)
{
    FILETIME loadedTime = {0, 0}, now, again;
    DWORD loadedSize = 0, nowSize, againSize;
    (void)unused;
    for (;;)
    {
        if (ReadStamp(&now, &nowSize) &&
            (CompareFileTime(&now, &loadedTime) != 0 || nowSize != loadedSize))
        {
            Sleep(300); /* let the linker finish writing */
            if (ReadStamp(&again, &againSize) && CompareFileTime(&now, &again) == 0 &&
                nowSize == againSize && ReloadLogic())
            {
                loadedTime = now;
                loadedSize = nowSize;
            }
        }
        Sleep(500);
    }
}

/* ---- DirectInput hook --------------------------------------------------- */

static void OnDeviceState(DWORD size, LPVOID data)
{
    if (data && (size == 16u || size == 20u))
    {
        LONG *axes = (LONG *)data;
        if (InterlockedIncrement(&g_mouseCalls) == 1)
            Log("first mouse state call (size %lu)", (unsigned long)size);
        CallAim(&axes[0], &axes[1]);
    }
}

static HRESULT STDMETHODCALLTYPE StateHookA(void *self, DWORD size, LPVOID data)
{
    HRESULT result = g_stateA(self, size, data);
    if (result == DI_OK)
        OnDeviceState(size, data);
    return result;
}

static HRESULT STDMETHODCALLTYPE StateHookW(void *self, DWORD size, LPVOID data)
{
    HRESULT result = g_stateW(self, size, data);
    if (result == DI_OK)
        OnDeviceState(size, data);
    return result;
}

static void *PatchVtableSlot(void **vtable, int index, void *hook)
{
    DWORD old;
    void *previous;
    void **slot = &vtable[index];
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
        return NULL;
    previous = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return previous;
}

static int InstallHooks(void)
{
    HMODULE module = GetModuleHandleA("dinput8.dll");
    DirectInput8CreateFn create;
    IDirectInput8A *inputA = NULL;
    IDirectInput8W *inputW = NULL;
    IDirectInputDevice8A *deviceA = NULL;
    IDirectInputDevice8W *deviceW = NULL;
    void **tableA = NULL, **tableW = NULL;

    if (!module)
        return 0;
    create = (DirectInput8CreateFn)GetProcAddress(module, "DirectInput8Create");
    if (!create)
        return -1;
    if (create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&inputA, NULL) == DI_OK &&
        inputA->lpVtbl->CreateDevice(inputA, &GUID_SysMouse, &deviceA, NULL) == DI_OK)
        tableA = *(void ***)deviceA;
    if (create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&inputW, NULL) == DI_OK &&
        inputW->lpVtbl->CreateDevice(inputW, &GUID_SysMouse, &deviceW, NULL) == DI_OK)
        tableW = *(void ***)deviceW;

    if (tableA)
    {
        g_stateA = (GetDeviceStateFn)PatchVtableSlot(tableA, 9, (void *)StateHookA);
        Log("GetDeviceState hooked (ANSI vtable %p)", (void *)tableA);
    }
    if (tableW && tableW != tableA)
    {
        g_stateW = (GetDeviceStateFn)PatchVtableSlot(tableW, 9, (void *)StateHookW);
        Log("GetDeviceState hooked (wide vtable %p)", (void *)tableW);
    }
    if (deviceA) deviceA->lpVtbl->Release(deviceA);
    if (deviceW) deviceW->lpVtbl->Release(deviceW);
    if (inputA) inputA->lpVtbl->Release(inputA);
    if (inputW) inputW->lpVtbl->Release(inputW);
    return (g_stateA || g_stateW) ? 1 : -1;
}

static void __fastcall PlayerCrouchHook(void *human, void *unused, int requested)
{
    AimCrouchFn callback;
    (void)unused;
    AcquireSRWLockShared(&g_logicLock);
    InterlockedIncrement(&g_inFlight);
    callback = g_crouch;
    if (callback)
    {
        __try
        {
            requested = callback((uintptr_t)human, requested);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_crouch = NULL;
            Log("crouch callback failed; original game control retained");
        }
    }
    InterlockedDecrement(&g_inFlight);
    ReleaseSRWLockShared(&g_logicLock);
    g_originalCrouch(human, NULL, requested);
}

static void InstallCrouchHook(void)
{
    static const BYTE callSignature[] = {0xE8, 0x68, 0x6C, 0xFD, 0xFF};
    static const BYTE entrySignature[] = {
        0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
        0xF8, 0x06, 0x00, 0x00, 0x85, 0xC0
    };
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    BYTE *callSite = (BYTE *)(base + 0x0C9183u);
    BYTE *original = (BYTE *)(base + 0x09FDF0u);
    DWORD protection, restored;
    int32_t displacement;
    if (memcmp(callSite, callSignature, sizeof(callSignature)) != 0 ||
        memcmp(original, entrySignature, sizeof(entrySignature)) != 0)
    {
        Log("crouch hook disabled: Mafia 1.0 signatures did not match");
        return;
    }
    g_originalCrouch = (HumanCrouchFn)original;
    if (!VirtualProtect(callSite, 5, PAGE_EXECUTE_READWRITE, &protection))
        return;
    displacement = (int32_t)((uintptr_t)PlayerCrouchHook - (uintptr_t)(callSite + 5));
    memcpy(callSite + 1, &displacement, sizeof(displacement));
    VirtualProtect(callSite, 5, protection, &restored);
    FlushInstructionCache(GetCurrentProcess(), callSite, 5);
    Log("Mafia 1.0 player crouch call hooked");
}

typedef BYTE (__fastcall *HostHumanHitFn)(void *human, void *unused, int type,
                                        const HostVector3 *position, const HostVector3 *normal,
                                        const HostVector3 *direction, float damage, void *source,
                                        DWORD bodyPart, void *frame);
static HostHumanHitFn g_originalHumanHit;

static BYTE __fastcall TracePoliceHit(void *human, void *unused, int type,
                                     const HostVector3 *position, const HostVector3 *normal,
                                     const HostVector3 *direction, float damage, void *source,
                                     DWORD bodyPart, void *frame)
{
    static int traced;
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t mission = *(uintptr_t *)(base + 0x25115Cu);
    uintptr_t game = mission ? *(uintptr_t *)(mission + 0x24u) : 0;
    int trace = game && *(void **)(game + 0xE4u) == source &&
        *(DWORD *)((BYTE *)human + 0xF4Cu) == 2 && traced < 24;
    float before = *(float *)((BYTE *)human + 0x644u);
    AimHitFn callback;
    BYTE result;
    static int playerTraced;
    int tracePlayer = game && *(void **)(game + 0xE4u) == human && playerTraced < 12;
    (void)unused;
    if (tracePlayer)
    {
        BYTE *h = (BYTE *)human;
        Log("player Hit entry type=%d damage=%.2f hp=%.1f src=%p part=%lu state=%lu f1E5=%d f1EC=%d f1FE=%d f21C=%d fAF5=%d f581=%d",
            type, damage, before, source, (unsigned long)bodyPart, (unsigned long)*(DWORD *)(h + 0x40Cu),
            h[0x1E5], h[0x1EC], h[0x1FE], h[0x21C], h[0xAF5], h[0x581]);
    }
    if (trace)
        Log("police Hit entry actor=%p type=%d damage=%.2f hp=%.2f alive=%d part=%lu flags94=%lu",
            human, type, damage, before, *((BYTE *)human + 0x5Du),
            (unsigned long)bodyPart, (unsigned long)*(DWORD *)((BYTE *)human + 0x94u));
    AcquireSRWLockShared(&g_logicLock);
    callback = g_hitDamage;
    if (callback)
    {
        __try
        {
            float adjusted = callback(game, (uintptr_t)human, (uintptr_t)source, type, damage);
            if (_finite(adjusted) && adjusted >= 0.0f)
                damage = adjusted;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_hitDamage = NULL;
        }
    if (tracePlayer)
    {
        BYTE *h = (BYTE *)human;
        ++playerTraced;
        Log("player Hit exit hp=%.1f state=%lu f1E5=%d f1EC=%d f1FE=%d f21C=%d fAF5=%d f581=%d weapon=%lu",
            *(float *)(h + 0x644u), (unsigned long)*(DWORD *)(h + 0x40Cu),
            h[0x1E5], h[0x1EC], h[0x1FE], h[0x21C], h[0xAF5], h[0x581], (unsigned long)*(DWORD *)(h + 0x1E8u));
    }
    }
    ReleaseSRWLockShared(&g_logicLock);
    result = g_originalHumanHit(human, NULL, type, position, normal, direction, damage, source, bodyPart, frame);
    if (trace)
    {
        ++traced;
        Log("police Hit exit actor=%p hp=%.2f alive=%d death=%d", human,
            *(float *)((BYTE *)human + 0x644u), *((BYTE *)human + 0x5Du), *((BYTE *)human + 0x5Eu));
    }
    return result;
}

static void InstallPoliceHitTrace(void)
{
    static const BYTE call[] = {0xE8, 0xD0, 0x44, 0x08, 0x00};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    BYTE *site = (BYTE *)(base + 0x01223Bu);
    DWORD protection, restored;
    int32_t displacement;
    if (memcmp(site, call, 5) != 0 ||
        memcmp((void *)(base + 0x096710u), "\x8B\x44\x24\x04\x83\xEC\x34", 7) != 0)
    {
        Log("Police hit trace rejected: call or entry signature mismatch");
        return;
    }
    g_originalHumanHit = (HostHumanHitFn)(base + 0x096710u);
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection))
        return;
    displacement = (int32_t)((uintptr_t)TracePoliceHit - (uintptr_t)(site + 5));
    memcpy(site + 1, &displacement, 4);
    VirtualProtect(site, 5, protection, &restored);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("Police actual-hit trace installed; native damage arguments retained");
}

static HostHumanHitFn g_originalCarHit;
typedef void (__fastcall *HostDetachWheelFn)(void *car, int index,
                                            const HostVector3 *position, const HostVector3 *impulse);
static HostDetachWheelFn g_detachWheel;

static BYTE __fastcall ArcadeCarHit(void *car, void *unused, int type,
                                   const HostVector3 *position, const HostVector3 *normal,
                                   const HostVector3 *direction, float damage, void *source,
                                   DWORD bodyPart, void *frame)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t mission = *(uintptr_t *)(base + 0x25115Cu);
    uintptr_t game = mission ? *(uintptr_t *)(mission + 0x24u) : 0;
    uintptr_t wheel = 0;
    int index = -1, stage = 0;
    AimWheelHitFn callback;
    BYTE result;
    (void)unused;
    AcquireSRWLockShared(&g_logicLock);
    callback = g_wheelHit;
    if (callback)
    {
        __try
        {
            stage = callback(game, (uintptr_t)car, (uintptr_t)source, type,
                             (uintptr_t)frame, g_playerShotSequence, &wheel, &index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_wheelHit = NULL;
        }
    }
    ReleaseSRWLockShared(&g_logicLock);
    result = g_originalCarHit(car, NULL, type, position, normal, direction, stage ? 0.0f : damage,
                             source, bodyPart, frame);
    if (stage == 1 && wheel)
        *(DWORD *)(wheel + 0x120u) |= 0x80000000u;
    else if (stage == 2 && wheel && index >= 0 && g_detachWheel)
    {
        HostVector3 releaseMotion = {0, 0, 0};
        g_detachWheel(car, index, &releaseMotion, NULL);
        Log("player tyre detachment: car=%p index=%d flags=0x%08lX", car, index,
            (unsigned long)*(DWORD *)(wheel + 0x120u));
    }
    return result;
}

static void InstallArcadeWheelHitHook(void)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    void **primary = (void **)(base + 0x23BC08u);
    void **extended = (void **)(base + 0x23BD68u);
    if (primary[31] != (void *)(base + 0x06A670u) ||
        extended[31] != primary[31] ||
        memcmp((void *)(base + 0x06A670u), "\x81\xEC\xF0\x00\x00\x00", 6) != 0 ||
        memcmp((void *)(base + 0x06E0C0u), "\x83\xEC\x2C\x8B\x81\x24\x0D\x00\x00", 9) != 0)
    {
        Log("Arcade wheel hit hook rejected: native signatures mismatch");
        return;
    }
    g_originalCarHit = (HostHumanHitFn)primary[31];
    g_detachWheel = (HostDetachWheelFn)(base + 0x06E0C0u);
    if (!PatchVtableSlot(primary, 31, (void *)ArcadeCarHit) ||
        !PatchVtableSlot(extended, 31, (void *)ArcadeCarHit))
        return;
    Log("Arcade confirmed wheel hits installed: puncture then native detachment");
}

static void InstallArcadeShotHook(void)
{
    static const BYTE signatures[2][5] = {
        {0xE8, 0xA6, 0xEC, 0x13, 0x00},
        {0xE8, 0x01, 0xE1, 0x13, 0x00}
    };
    static const uintptr_t sites[2] = {0x0A5145u, 0x0A5CEAu};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    int index;
    if (memcmp((const void *)(base + 0x1E3DF0u), "\x81\xEC\xD4\x00\x00\x00", 6) != 0)
        return;
    for (index = 0; index < 2; ++index)
        if (memcmp((const void *)(base + sites[index]), signatures[index], 5) != 0)
            return;
    g_originalShoot = (HostShootFn)(base + 0x1E3DF0u);
    for (index = 0; index < 2; ++index)
    {
        BYTE *site = (BYTE *)(base + sites[index]);
        DWORD protection, restored;
        int32_t displacement;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection))
            return;
        displacement = (int32_t)((uintptr_t)ArcadeShootHook - (uintptr_t)(site + 5));
        memcpy(site + 1, &displacement, sizeof(displacement));
        VirtualProtect(site, 5, protection, &restored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Log("Player car arcade shot hook installed; NPC and on-foot shots forwarded");
}

static int __cdecl TutorialFreerideAction(void)
{
    g_freerideStore(1, 0, 1.0f, 1.0f, 0.0f);
    g_tutorialFreeride = 1;
    Log("Tutorial redirected to FreeItaly: police disabled for this session");
    return 0x1C;
}

static void __fastcall TutorialPoliceState(void *manager, void *unused, int enabled,
                                          int vehicleTier, int cacheFlag)
{
    (void)unused;
    g_policeState(manager, NULL, g_tutorialFreeride ? 0 : enabled, vehicleTier, cacheFlag);
}

typedef void (__fastcall *HostEntityTickFn)(void *entity, void *unused, DWORD delta);
typedef void *(__fastcall *HostCreateActorFn)(void *mission, void *unused, int kind);
typedef int (__fastcall *HostModelOpenFn)(void *cache, void *unused, void *model,
                                        const char *name, int, int, int, int);
typedef void (__fastcall *HostObjectFn)(void *object, void *unused);
typedef void (__fastcall *HostObjectArgFn)(void *object, void *unused, void *argument);
typedef void (__fastcall *HostFlagFn)(void *object, void *unused, int flag);
typedef void (__fastcall *HostPositionFn)(void *frame, void *unused, const HostVector3 *position);
typedef int (__fastcall *HostPlayAnimFn)(void *human, void *unused, const char *name, int loop, int simple);
typedef unsigned char (__fastcall *HostCanSeeFn)(void *sensors, void *unused, void *actor);
typedef int (__fastcall *HostInventoryFn)(void *inventory, void *unused, const DWORD *item, int select);
typedef void *(__stdcall *HostCreateFrameFn)(void *driver, int kind);
typedef void (__stdcall *HostFrameNameFn)(void *frame, const char *name);
typedef void (__stdcall *HostFrameLinkFn)(void *frame, void *parent, int flags);
typedef void (__stdcall *HostFrameDirFn)(void *frame, const HostVector3 *direction, int flags);
typedef void (__stdcall *HostFrameOnFn)(void *frame, int enabled);
typedef void (__stdcall *HostFrameReleaseFn)(void *frame);

static HostEntityTickFn g_originalEntityTick;
static struct {
    void *prefix;
    void *methods[39];
} g_enemyVtable;
static uintptr_t g_tutorialEnemy, g_tutorialCigarette;
static int g_enemyAlerted;
static DWORD g_enemyAnimElapsed;
static float g_enemyHealth;

static void ConfigureEnemyPersonality(uintptr_t entity)
{
    *(float *)(entity + 0x660u) = 1.0f;
    *(float *)(entity + 0x67Cu) = 1.0f;
    *(float *)(entity + 0xECCu) = 0.0f;
    *(float *)(entity + 0xEC4u) = 0.0f;
}

static int EnemyWasHurt(float health, float previousHealth)
{
    return _finite(health) && _finite(previousHealth) &&
           health > 0.01f && previousHealth > health;
}

static void *HostVirtual(uintptr_t object, unsigned int byteOffset)
{
    return (void *)*(uintptr_t *)(*(uintptr_t *)object + byteOffset);
}

static uintptr_t CreateEnemyModel(const char *name, const char *modelName)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t mission = *(uintptr_t *)(base + 0x25115Cu);
    uintptr_t scene = *(uintptr_t *)(mission + 0x10u);
    uintptr_t driver = *(uintptr_t *)(base + 0x2F9520u);
    uintptr_t model = (uintptr_t)((HostCreateFrameFn)HostVirtual(driver, 0x4C))( (void *)driver, 9);
    if (!model)
        return 0;
    ((HostFrameNameFn)HostVirtual(model, 0x28))((void *)model, name);
    if (((HostModelOpenFn)(base + 0x048940u))((void *)(base + 0x2F9418u), NULL,
                                          (void *)model, modelName, 0, 0, 0, 0) < 0)
    {
        ((HostFrameReleaseFn)HostVirtual(model, 0))((void *)model);
        return 0;
    }
    ((HostFrameLinkFn)HostVirtual(model, 0x2C))((void *)model,
                                             *(void **)(scene + 0x210u), 0);
    return model;
}

static int EnemySpawnPose(HostVector3 playerPosition, HostVector3 playerDirection,
                          HostVector3 *position, HostVector3 *direction)
{
    float lengthSquared = playerDirection.x * playerDirection.x +
                          playerDirection.z * playerDirection.z;
    float inverse;
    if (!_finite(lengthSquared) || lengthSquared < 0.0001f ||
        !_finite(playerPosition.x) || !_finite(playerPosition.y) || !_finite(playerPosition.z))
        return 0;
    inverse = 1.0f / sqrtf(lengthSquared);
    direction->x = playerDirection.x * inverse;
    direction->y = 0.0f;
    direction->z = playerDirection.z * inverse;
    position->x = playerPosition.x + direction->x * 6.0f;
    position->y = playerPosition.y;
    position->z = playerPosition.z + direction->z * 6.0f;
    return 1;
}

static int EnemyMayNotice(HostVector3 position, HostVector3 facing, HostVector3 playerPosition)
{
    float deltaX = playerPosition.x - position.x;
    float deltaZ = playerPosition.z - position.z;
    return deltaX * facing.x + deltaZ * facing.z > 0.0f;
}

static void SpawnTutorialEnemy(void)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t mission = *(uintptr_t *)(base + 0x25115Cu);
    uintptr_t game = *(uintptr_t *)(mission + 0x24u);
    uintptr_t player = *(uintptr_t *)(game + 0xE4u);
    uintptr_t frame, entity, hand;
    HostVector3 position, direction;
    DWORD weapon[6] = {9, 7, 35, 0, 0, 0};
    if (!player || !EnemySpawnPose(*(HostVector3 *)(player + 0x24u),
                                  *(HostVector3 *)(player + 0x30u), &position, &direction))
        return;
    frame = CreateEnemyModel("MafiaRpgEnemy", "SamHIGH.i3d");
    if (!frame)
    {
        Log("Enemy spawn failed: SamHIGH model unavailable");
        return;
    }
    ((HostPositionFn)(base + 0x05F000u))((void *)frame, NULL, &position);
    ((HostFrameDirFn)HostVirtual(frame, 0x0C))((void *)frame, &direction, 0);
    entity = (uintptr_t)((HostCreateActorFn)(base + 0x1FED70u))((void *)mission, NULL, 0x1B);
    if (!entity)
    {
        ((HostFrameReleaseFn)HostVirtual(frame, 0))((void *)frame);
        return;
    }
    if (!((HostCanSeeFn)HostVirtual(entity, 0x48))((void *)entity, NULL, (void *)frame))
    {
        Log("Enemy spawn failed: entity model initialization rejected");
        ((HostFrameReleaseFn)HostVirtual(frame, 0))((void *)frame);
        return;
    }
    *(DWORD *)(entity + 0x5FCu) = 0x10;
    ((HostObjectArgFn)(base + 0x1E3220u))((void *)game, NULL, (void *)entity);
    ConfigureEnemyPersonality(entity);
    ((HostInventoryFn)(base + 0x15B2D0u))((void *)(entity + 0x480u), NULL, weapon, 0);
    ((HostObjectFn)(base + 0x09F180u))((void *)entity, NULL);
    ((HostFlagFn)(base + 0x1C9010u))((void *)entity, NULL, 1);
    g_tutorialEnemy = entity;
    g_enemyAlerted = 0;
    g_enemyAnimElapsed = 0;
    g_enemyHealth = *(float *)(entity + 0x644u);
    hand = *(uintptr_t *)(entity + 0x570u);
    if (hand)
    {
        g_tutorialCigarette = CreateEnemyModel("MafiaRpgCigarette", "2cigaro.i3d");
        if (g_tutorialCigarette)
        {
            HostVector3 origin = {0.0f, 0.0f, 0.0f};
            ((HostFrameLinkFn)HostVirtual(g_tutorialCigarette, 0x2C))(
                (void *)g_tutorialCigarette, (void *)hand, 0);
            ((HostPositionFn)(base + 0x05F000u))((void *)g_tutorialCigarette, NULL, &origin);
        }
    }
    ((HostPlayAnimFn)(base + 0x0A63F0u))((void *)entity, NULL, "KoureniAutoStativ.i3d", 1, 0);
    ((HostFrameReleaseFn)HostVirtual(frame, 0))((void *)frame);
    Log("Mafioso spawned: actor=%p group=%lu position=%.2f,%.2f,%.2f; awaiting visual contact",
        (void *)entity, *(DWORD *)(entity + 0xF4Cu), position.x, position.y, position.z);
}

static void __fastcall TutorialEnemyTick(void *actor, void *unused, DWORD delta)
{
    uintptr_t entity = (uintptr_t)actor;
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    float health;
    int hurt;
    (void)unused;
    g_originalEntityTick(actor, NULL, delta);
    if (!g_tutorialFreeride || entity != g_tutorialEnemy)
        return;
    health = *(float *)(entity + 0x644u);
    hurt = EnemyWasHurt(health, g_enemyHealth);
    g_enemyHealth = health;
    if (!_finite(health) || health <= 0.01f || (g_enemyAlerted && !hurt))
        return;
    ((HostObjectFn)(base + 0x0107D0u))((void *)(entity + 0xB04u), NULL);
    {
        uintptr_t mission = *(uintptr_t *)(base + 0x25115Cu);
        uintptr_t game = *(uintptr_t *)(mission + 0x24u);
        uintptr_t player = *(uintptr_t *)(game + 0xE4u);
        if (player && (hurt || (EnemyMayNotice(*(HostVector3 *)(entity + 0x24u),
                                    *(HostVector3 *)(entity + 0x30u),
                                    *(HostVector3 *)(player + 0x24u)) &&
            ((HostCanSeeFn)(base + 0x0101B0u))((void *)(entity + 0xBB4u), NULL, (void *)player))))
        {
            g_enemyAlerted = 1;
            if (g_tutorialCigarette)
                ((HostFrameOnFn)HostVirtual(g_tutorialCigarette, 0x24))((void *)g_tutorialCigarette, 0);
            ((HostObjectFn)(base + 0x09D840u))((void *)entity, NULL);
            ((HostFlagFn)(base + 0x1C9010u))((void *)entity, NULL, 0);
            Log("Mafioso %s: smoking stopped, native target/state selection resumed",
                hurt ? "was hurt" : "saw the player");
            return;
        }
    }
    g_enemyAnimElapsed += delta;
    if (g_enemyAnimElapsed >= 7000)
    {
        g_enemyAnimElapsed = 0;
        ((HostPlayAnimFn)(base + 0x0A63F0u))((void *)entity, NULL,
                                          "KoureniAutoPotahnuti.i3d", 1, 0);
    }
}

static int BindEnemyInstanceAi(uintptr_t entity)
{
    void **original = *(void ***)entity;
    if (!g_originalEntityTick || original[13] != (void *)g_originalEntityTick)
        return 0;
    g_enemyVtable.prefix = original[-1];
    memcpy(g_enemyVtable.methods, original, sizeof(g_enemyVtable.methods));
    g_enemyVtable.methods[13] = (void *)TutorialEnemyTick;
    *(void ***)entity = g_enemyVtable.methods;
    return 1;
}

static HostFlagFn g_pedestrianMegaPanic;

static void __fastcall TutorialPedestrianPanic(void *pedestrian, void *unused, int severity)
{
    (void)unused;
    if (!g_tutorialFreeride)
        g_pedestrianMegaPanic(pedestrian, NULL, severity);
}

static void InstallPedestrianPanicHook(void)
{
    static const BYTE signatures[2][5] = {
        {0xE8, 0x2F, 0x5C, 0x00, 0x00},
        {0xE8, 0xBA, 0x3A, 0x00, 0x00}
    };
    static const uintptr_t sites[2] = {0x0B837Cu, 0x0BA4F1u};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    int index;
    for (index = 0; index < 2; ++index)
        if (memcmp((const void *)(base + sites[index]), signatures[index], 5) != 0)
            return;
    g_pedestrianMegaPanic = (HostFlagFn)(base + 0x0BDFB0u);
    for (index = 0; index < 2; ++index)
    {
        BYTE *site = (BYTE *)(base + sites[index]);
        DWORD protection, restored;
        int32_t displacement;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection))
            return;
        displacement = (int32_t)((uintptr_t)TutorialPedestrianPanic - (uintptr_t)(site + 5));
        memcpy(site + 1, &displacement, sizeof(displacement));
        VirtualProtect(site, 5, protection, &restored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Log("Tutorial crowd: collapse gesture skipped; native movement and damage retained");
}

static void __cdecl TutorialGameLoop(void)
{
    if (g_tutorialFreeride && g_originalEntityTick)
    {
        __try
        {
            SpawnTutorialEnemy();
            if (g_tutorialEnemy && !BindEnemyInstanceAi(g_tutorialEnemy))
                Log("Enemy instance AI binding rejected");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("Enemy spawn exception 0x%08lX", GetExceptionCode());
        }
    }
    g_gameLoop();
    if (g_tutorialCigarette)
        ((HostFrameReleaseFn)HostVirtual(g_tutorialCigarette, 0))((void *)g_tutorialCigarette);
    g_tutorialCigarette = 0;
    g_tutorialEnemy = 0;
    g_enemyHealth = 0.0f;
    g_tutorialFreeride = 0;
}

static void InstallTutorialHook(void)
{
    static const BYTE actionSignature[] = {0xB8, 0x11, 0x00, 0x00, 0x00};
    static const BYTE storeSignature[] = {
        0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08,
        0x8B, 0x54, 0x24, 0x0C, 0xA3, 0xD0, 0x1E, 0x67, 0x00
    };
    static const BYTE policeCall[] = {0xE8, 0xE2, 0xC7, 0x00, 0x00};
    static const BYTE loopCall[] = {0xE8, 0x67, 0xF5, 0xFF, 0xFF};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    BYTE *action = (BYTE *)(base + 0x176AA7u);
    BYTE *store = (BYTE *)(base + 0x1608F0u);
    struct { BYTE *address; uintptr_t callback; } patches[3];
    DWORD protection, restored;
    int32_t displacement;
    int index;
    if (memcmp(action, actionSignature, sizeof(actionSignature)) != 0 ||
        memcmp(store, storeSignature, sizeof(storeSignature)) != 0 ||
        memcmp((const void *)(base + 0x1C01F9u), policeCall, sizeof(policeCall)) != 0 ||
        memcmp((const void *)(base + 0x1F9FD4u), loopCall, sizeof(loopCall)) != 0 ||
        *(const DWORD *)(base + 0x1FA260u) != base + 0x1F9C8Au ||
        memcmp((const void *)(base + 0x251338u), "tutorial", 9) != 0 ||
        memcmp((const void *)(base + 0x2512CCu), "freeitaly", 10) != 0 ||
        *(const DWORD *)(base + 0x1FA28Cu) != base + 0x1F9D7Au)
    {
        Log("tutorial redirect disabled: Mafia 1.0 signatures did not match");
        return;
    }
    g_freerideStore = (FreerideStoreFn)store;
    g_policeState = (PoliceStateFn)(base + 0x1CC9E0u);
    g_gameLoop = (GameLoopFn)(base + 0x1F9540u);
    patches[0].address = (BYTE *)(base + 0x1C01F9u);
    patches[0].callback = (uintptr_t)TutorialPoliceState;
    patches[1].address = (BYTE *)(base + 0x1F9FD4u);
    patches[1].callback = (uintptr_t)TutorialGameLoop;
    patches[2].address = action;
    patches[2].callback = (uintptr_t)TutorialFreerideAction;
    for (index = 0; index < 3; ++index)
    {
        BYTE *address = patches[index].address;
        if (!VirtualProtect(address, 5, PAGE_EXECUTE_READWRITE, &protection))
            return;
        displacement = (int32_t)(patches[index].callback - (uintptr_t)(address + 5));
        address[0] = 0xE8;
        memcpy(address + 1, &displacement, sizeof(displacement));
        VirtualProtect(address, 5, protection, &restored);
        FlushInstructionCache(GetCurrentProcess(), address, 5);
    }
    Log("Tutorial menu redirected to Little Italy freeride");
}

static void InstallSandboxHooks(int enabled)
{
    if (!enabled)
    {
        Log("Experimental sandbox disabled: native Tutorial, police and NPCs retained");
        return;
    }
    InstallTutorialHook();
    InstallPedestrianPanicHook();
}

static DWORD WINAPI InstallThread(void *unused)
{
    int attempt;
    char iniPath[MAX_PATH];
    DWORD length;
    (void)unused;
    InstallCrouchHook();
    InstallArcadeShotHook();
    InstallPoliceHitTrace();
    InstallArcadeWheelHitHook();
    length = GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    if (length && length < MAX_PATH - 32)
    {
        while (length && iniPath[length - 1] != '\\' && iniPath[length - 1] != '/')
            --length;
        iniPath[length] = '\0';
        lstrcatA(iniPath, "MafiaAimAssist.ini");
        InstallSandboxHooks(GetPrivateProfileIntA("sandbox", "enabled", 0, iniPath) != 0);
    }
    {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        void **table = (void **)(base + 0x23B488u);
        if (table[13] == (void *)(base + 0x012480u) &&
            memcmp((const void *)(base + 0x1FED70u), "\x64\xA1\x00\x00\x00\x00", 6) == 0 &&
            memcmp((const void *)(base + 0x0A63F0u), "\x56\x8B\xF1\x33\xC0", 5) == 0)
        {
            g_originalEntityTick = (HostEntityTickFn)table[13];
            if (g_originalEntityTick)
                Log("Mafioso AI bound; shared entity vtable unchanged");
        }
    }
    for (attempt = 0; attempt < 100; ++attempt)
    {
        int result = InstallHooks();
        if (result != 0)
        {
            if (result < 0)
                Log("DirectInput hook failed");
            return 0;
        }
        Sleep(100);
    }
    Log("dinput8.dll never appeared");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DWORD length;
        HANDLE thread;
        DisableThreadLibraryCalls(instance);
        g_self = instance;
        length = GetModuleFileNameW(instance, g_logicPath, MAX_PATH);
        while (length && g_logicPath[length - 1] != L'\\' && g_logicPath[length - 1] != L'/')
            --length;
        if (length && length + 20 < MAX_PATH)
        {
            HANDLE guard = CreateMutexA(NULL, FALSE, "Local\\MafiaAimAssistHost");
            if (guard && GetLastError() == ERROR_ALREADY_EXISTS)
                return TRUE; /* another copy of the host already owns the hook */
            g_logicPath[length] = L'\0';
            lstrcatW(g_logicPath, L"MafiaAimLogic.dll");
            thread = CreateThread(NULL, 0, InstallThread, NULL, 0, NULL);
            if (thread)
                CloseHandle(thread);
            thread = CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);
            if (thread)
                CloseHandle(thread);
        }
    }
    return TRUE;
}

#ifdef BUILD_PROXY
/* Built as dinput8.dll: the game loads this copy from its own folder and it forwards to the real one. */
#pragma comment(linker, "/EXPORT:DirectInput8Create=_ProxyDirectInput8Create@20")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=_ProxyDllCanUnloadNow@0")
#pragma comment(linker, "/EXPORT:DllGetClassObject=_ProxyDllGetClassObject@12")

static HMODULE g_realDinput;

static FARPROC RealProc(const char *name)
{
    if (!g_realDinput)
    {
        char path[MAX_PATH];
        UINT length = GetSystemDirectoryA(path, MAX_PATH);
        if (!length || length > MAX_PATH - 16)
            return NULL;
        lstrcatA(path, "\\dinput8.dll");
        g_realDinput = LoadLibraryA(path);
    }
    return g_realDinput ? GetProcAddress(g_realDinput, name) : NULL;
}

HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID *out,
                                       LPUNKNOWN outer)
{
    DirectInput8CreateFn fn = (DirectInput8CreateFn)RealProc("DirectInput8Create");
    return fn ? fn(instance, version, iid, out, outer) : E_FAIL;
}

HRESULT WINAPI ProxyDllCanUnloadNow(void)
{
    HRESULT (WINAPI *fn)(void) = (HRESULT (WINAPI *)(void))RealProc("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID *out)
{
    HRESULT (WINAPI *fn)(REFCLSID, REFIID, LPVOID *) =
        (HRESULT (WINAPI *)(REFCLSID, REFIID, LPVOID *))RealProc("DllGetClassObject");
    return fn ? fn(clsid, iid, out) : E_FAIL;
}
#endif
