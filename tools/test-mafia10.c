#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static BYTE *testImage;
static WORD testButtons;
static int testConnected = 1;
static LONGLONG testClock;

static HMODULE WINAPI TestModuleHandle(LPCSTR name)
{
    return name ? NULL : (HMODULE)testImage;
}

static HMODULE WINAPI TestLoadLibrary(LPCSTR name)
{
    (void)name;
    return (HMODULE)1;
}

static DWORD WINAPI TestPadState(DWORD slot, void *state)
{
    (void)slot;
    memset(state, 0, 16);
    *(WORD *)((BYTE *)state + 4) = testButtons;
    return testConnected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

static FARPROC WINAPI TestProcAddress(HMODULE module, LPCSTR name)
{
    (void)module;
    return strcmp(name, "XInputGetState") == 0 ? (FARPROC)TestPadState : NULL;
}

static BOOL WINAPI TestCounter(LARGE_INTEGER *counter)
{
    testClock += 100000;
    counter->QuadPart = testClock;
    return TRUE;
}

static BOOL WINAPI TestFrequency(LARGE_INTEGER *frequency)
{
    frequency->QuadPart = 1000000;
    return TRUE;
}

#define GetModuleHandleA TestModuleHandle
#define LoadLibraryA TestLoadLibrary
#define GetProcAddress TestProcAddress
#define QueryPerformanceCounter TestCounter
#define QueryPerformanceFrequency TestFrequency
#include "../src/MafiaAimLogic.c"
#define Log HostLog
#define g_log g_hostLog
#define DllMain TestHostDllMain
#include "../src/MafiaAimHost.c"
#undef Log
#undef g_log

static int failures;

static void Check(int condition, const char *name)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

static int __fastcall TestLine(void *collision, void *unused, const Vector3 *from,
                              const Vector3 *delta, Vector3 *hit, Vector3 *normal,
                              int ignore, uint32_t mask)
{
    (void)unused;
    (void)from;
    (void)delta;
    (void)hit;
    (void)normal;
    Check(collision == testImage + COLLISION_OBJECT_RVA && ignore == -1 && mask == 0,
          "LOS receiver and six stack arguments");
    return 0;
}

int main(void)
{
    static const BYTE setter[] = {
        0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
        0xF8, 0x06, 0x00, 0x00, 0x85, 0xC0,
        0x8B, 0x44, 0x24, 0x20, 0x88, 0x86, 0xE4, 0x01, 0x00, 0x00,
        0x5E, 0x83, 0xC4, 0x18, 0xC2, 0x04, 0x00
    };
    static const BYTE caller[] = {
        0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x54, 0x24, 0x08, 0x52,
        0xE8, 0x68, 0x6C, 0xFD, 0xFF, 0xC2, 0x08, 0x00
    };
    static const BYTE accessor[] = {0x8B, 0x81, 0xE4, 0, 0, 0, 0xC3};
    static const BYTE line[] = {0xE9, 0x6B, 0x2E, 0xFF, 0xFF};
    BYTE mission[0x100] = {0}, game[0x100] = {0}, player[0xB00] = {0};
    BYTE npc[0x1000] = {0}, camera[0x100] = {0};
    uintptr_t actors[2], worldAddress, playerAddress;
    Vector3 position, forward;
    void (__stdcall *runCrouch)(void *, int);
    testImage = (BYTE *)VirtualAlloc(NULL, 0x300000, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
    if (!testImage)
        return 2;
    memcpy(testImage + 0x09FDF0, setter, sizeof(setter));
    memcpy(testImage + 0x0C917A, caller, sizeof(caller));
    memcpy(testImage + 0x025530, accessor, sizeof(accessor));
    memcpy(testImage + COLLISION_LINE_WRAPPER_RVA, line, sizeof(line));
    *(uintptr_t *)(testImage + WORLD_POINTER_RVA) = (uintptr_t)mission;
    *(uintptr_t *)(mission + MISSION_GAME_OFFSET) = (uintptr_t)game;
    *(uintptr_t *)(game + WORLD_PLAYER_OFFSET) = (uintptr_t)player;
    *(uintptr_t *)(game + WORLD_CAMERA_OFFSET) = (uintptr_t)camera;
    game[0x40] = 1;
    *(uint32_t *)(player + ENTITY_KIND_OFFSET) = 2;
    *(uint32_t *)(npc + ENTITY_KIND_OFFSET) = SCRIPTABLE_NPC_KIND;
    *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
    *(float *)(npc + ENTITY_POSITION_OFFSET + 8) = 10.0f;
    *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
    *(float *)(camera + CAMERA_FORWARD_OFFSET + 8) = 1.0f;
    actors[0] = (uintptr_t)player;
    actors[1] = (uintptr_t)npc;
    *(uintptr_t *)(mission + WORLD_LIST_BEGIN_OFFSET) = (uintptr_t)actors;
    *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(actors + 2);
    strcpy(g_iniPath, "Z:\\mafia-test-missing.ini");
    Check(GetWorld(&worldAddress, &playerAddress) && worldAddress == (uintptr_t)mission &&
          playerAddress == (uintptr_t)player, "1.0 mission and player pointer chain");
    Check(ReadCamera(&position, &forward) && forward.z == 1.0f, "1.0 camera read");
    Check(IsLivePed((uintptr_t)npc, &position) && IsPriorityEnemyPed((uintptr_t)npc),
          "living mission enemy recognized");
    Check(BindLineTest(), "1.0 LOS wrapper signature");
    g_lineTest = TestLine;
    position.x = position.y = position.z = 0.0f;
    forward.x = forward.y = 0.0f;
    forward.z = 10.0f;
    Check(HasLineOfSight(position, forward), "clear LOS");
    Check(FindNearestPed((uintptr_t)mission, (uintptr_t)player, position,
                        (Vector3){0.0f, 0.0f, 1.0f}, &forward) == (uintptr_t)npc,
          "target acquisition from mission actor list");
    InstallCrouchHook();
    Check(g_originalCrouch != NULL, "verified player callsite installed");
    g_crouch = AimCrouch;
    runCrouch = (void (__stdcall *)(void *, int))(testImage + 0x0C917A);
    runCrouch(player, 0);
    testButtons = GAMEPAD_B_BUTTON;
    runCrouch(player, 1);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 1, "B press crouches");
    runCrouch(player, 1);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 1, "held B does not retoggle");
    testButtons = 0;
    runCrouch(player, 0);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 1, "B release keeps crouch");
    testButtons = GAMEPAD_B_BUTTON;
    runCrouch(player, 1);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 0, "second B press stands");
    testButtons = 0;
    runCrouch(player, 0);
    testButtons = GAMEPAD_B_BUTTON;
    runCrouch(player, 1);
    testConnected = 0;
    runCrouch(player, 0);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 0, "disconnect restores native input");
    testConnected = 1;
    testButtons = 0;
    g_crouchToggleEnabled = 0;
    g_configNext = NowMs() + 100000;
    runCrouch(player, 1);
    Check(player[EXPERIMENTAL_STANCE_BYTE_OFFSET] == 1, "disabled toggle retains native crouch");
    *(uintptr_t *)(player + 0x98) = (uintptr_t)npc;
    Check(!GetWorld(&worldAddress, &playerAddress), "vehicle link blocks on-foot mod");
    g_crouchPlayer = (uintptr_t)player;
    g_crouchDesired = 1;
    {
        LONG mouseX = 0, mouseY = 0;
        HandleMouse(&mouseX, &mouseY);
    }
    Check(g_crouchPlayer == 0 && g_crouchDesired == -1, "driving resets crouch latch");
    game[0x40] = 0;
    Check(!GetWorld(&worldAddress, &playerAddress), "unloaded world rejected");
    VirtualFree(testImage, 0, MEM_RELEASE);
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}