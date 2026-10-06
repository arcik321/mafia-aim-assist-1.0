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

/* ---- hot-reloadable logic ---------------------------------------------- */

static void CallAim(LONG *lx, LONG *ly)
{
    AimMouseFn fn;
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
    int i;

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
    if (!fn)
    {
        Log("logic has no AimMouse export");
        FreeLibrary(module);
        DeleteFileW(copy);
        return 0;
    }
    if (init)
        init();

    old = g_logic;
    g_logic = module;
    g_aim = fn;
    g_crouch = crouch;
    for (i = 0; i < 100 && g_inFlight > 0; ++i)
        Sleep(10);
    if (old)
    {
        FreeLibrary(old);
        DeleteFileW(g_loadedCopy);
    }
    lstrcpyW(g_loadedCopy, copy);
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

typedef struct HostVector3 { float x, y, z; } HostVector3;
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
