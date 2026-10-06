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

static int testFreerideModel, testFreerideFlags, testPoliceEnabled, testLoopSession;
static float testFreerideCars, testFreeridePedestrians, testFreeridePolice;

static void __stdcall TestFreerideStore(int model, int flags, float cars,
                                       float pedestrians, float police)
{
    testFreerideModel = model;
    testFreerideFlags = flags;
    testFreerideCars = cars;
    testFreeridePedestrians = pedestrians;
    testFreeridePolice = police;
}

static void __fastcall TestPoliceState(void *manager, void *unused, int enabled,
                                      int tier, int cacheFlag)
{
    (void)manager;
    (void)unused;
    (void)tier;
    (void)cacheFlag;
    testPoliceEnabled = enabled;
}

static void __cdecl TestGameLoop(void)
{
    testLoopSession = g_tutorialFreeride;
}

static int testOrdinaryPanic, testMegaPanic, testMegaSeverity;

static void __fastcall TestOrdinaryPanic(void *pedestrian, void *unused)
{
    (void)pedestrian;
    (void)unused;
    ++testOrdinaryPanic;
}

static void __fastcall TestMegaPanic(void *pedestrian, void *unused, int severity)
{
    (void)pedestrian;
    (void)unused;
    ++testMegaPanic;
    testMegaSeverity = severity;
}

static void __fastcall TestEntityTick(void *entity, void *unused, DWORD delta)
{
    (void)entity;
    (void)unused;
    (void)delta;
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
    BYTE mission[0x100] = {0}, game[0x200] = {0}, player[0xB00] = {0};
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
        {
          uintptr_t temporary[1] = {(uintptr_t)npc};
          uintptr_t listed, count;
          *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(actors + 1);
          *(uintptr_t *)(game + 0x124u) = (uintptr_t)temporary;
          *(uintptr_t *)(game + 0x128u) = (uintptr_t)(temporary + 1);
          Check(FindNearestPed((uintptr_t)mission, (uintptr_t)player, position,
                        (Vector3){0.0f, 0.0f, 1.0f}, &forward) == (uintptr_t)npc,
              "aim acquires a spawned temporary enemy");
          Check(TargetStillValid((uintptr_t)mission, (uintptr_t)player, (uintptr_t)npc, &forward),
              "aim retains lock on a temporary enemy");
          *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(actors + 2);
          Check(ListBounds((uintptr_t)mission, &listed, &count) && count == 2,
              "mission and temporary actor lists are deduplicated");
          *(uintptr_t *)(game + 0x124u) = 0;
          *(uintptr_t *)(game + 0x128u) = 0;
        }
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
        {
          void *sharedVtable[39] = {0};
          sharedVtable[13] = (void *)TestEntityTick;
          *(void ***)npc = sharedVtable;
          g_originalEntityTick = TestEntityTick;
          Check(BindEnemyInstanceAi((uintptr_t)npc) && *(void ***)npc == g_enemyVtable &&
              sharedVtable[13] == (void *)TestEntityTick,
              "enemy AI hook changes only its instance, not shared NPC vtable");
          g_originalEntityTick = NULL;
          g_pedestrianPanic = TestOrdinaryPanic;
          g_pedestrianMegaPanic = TestMegaPanic;
          g_tutorialFreeride = 1;
          TutorialPedestrianPanic(NULL, NULL, 3);
          Check(testOrdinaryPanic == 1 && testMegaPanic == 0,
              "tutorial gunfire panic uses fleeing instead of collapse");
          g_tutorialFreeride = 0;
          TutorialPedestrianPanic(NULL, NULL, 2);
          Check(testMegaPanic == 1 && testMegaSeverity == 2,
              "other modes retain native panic severity");
        }
        {
          HostVector3 playerPosition = {10.0f, 2.0f, 20.0f};
          HostVector3 playerDirection = {0.0f, 0.0f, 2.0f};
          HostVector3 spawnPosition, spawnDirection;
          Check(EnemySpawnPose(playerPosition, playerDirection, &spawnPosition, &spawnDirection) &&
              spawnPosition.x == 10.0f && spawnPosition.y == 2.0f && spawnPosition.z == 26.0f &&
              spawnDirection.z == 1.0f, "enemy spawns six metres ahead facing away from player");
          playerDirection.z = 0.0f;
          Check(!EnemySpawnPose(playerPosition, playerDirection, &spawnPosition, &spawnDirection),
              "enemy spawn rejects missing player direction");
          playerDirection.z = 1.0f;
          Check(!EnemyMayNotice(spawnPosition, playerDirection, playerPosition),
              "enemy cannot notice player standing behind him");
          playerPosition.z = 30.0f;
          Check(EnemyMayNotice(spawnPosition, playerDirection, playerPosition),
              "player in front may trigger native visual detection");
        }
    {
        static const BYTE menu[] = {0xB8, 0x11, 0, 0, 0, 0xC3};
        static const BYTE introMenu[] = {0xB8, 0x16, 0, 0, 0, 0xC3};
        static const BYTE store[] = {
            0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08,
            0x8B, 0x54, 0x24, 0x0C, 0xA3, 0xD0, 0x1E, 0x67, 0
        };
        static const BYTE policeCall[] = {0xE8, 0xE2, 0xC7, 0, 0};
        static const BYTE loopCall[] = {0xE8, 0x67, 0xF5, 0xFF, 0xFF};
        int (__cdecl *menuAction)(void) = (int (__cdecl *)(void))(testImage + 0x176AA7u);
        memcpy(testImage + 0x176AA7u, menu, sizeof(menu));
        memcpy(testImage + 0x176A85u, introMenu, sizeof(introMenu));
        memcpy(testImage + 0x251338u, "tutorial", 9);
        memcpy(testImage + 0x2512CCu, "freeitaly", 10);
        memcpy(testImage + 0x1608F0u, store, sizeof(store));
        memcpy(testImage + 0x1C01F9u, policeCall, sizeof(policeCall));
        memcpy(testImage + 0x1F9FD4u, loopCall, sizeof(loopCall));
        *(uintptr_t *)(testImage + 0x1FA260u) = (uintptr_t)testImage + 0x1F9C8Au;
        InstallTutorialHook();
        Check(testImage[0x176AA7u] == 0xB8, "tutorial redirect rejects wrong FreeItaly dispatch");
        *(uintptr_t *)(testImage + 0x1FA28Cu) = (uintptr_t)testImage + 0x1F9D7Au;
        InstallTutorialHook();
          Check(testImage[0x176AA7u] == 0xE8, "tutorial selector hook installed");
          Check(memcmp(testImage + 0x176A85u, introMenu, sizeof(introMenu)) == 0,
              "intro menu action remains untouched");
        g_freerideStore = TestFreerideStore;
        g_policeState = TestPoliceState;
        g_gameLoop = TestGameLoop;
        TutorialPoliceState(NULL, NULL, 1, 0, 0);
        Check(testPoliceEnabled == 1, "ordinary missions retain police activation");
        Check(menuAction() == 0x1C, "tutorial selects native Little Italy freeride");
        Check(testFreerideModel == 1 && testFreerideFlags == 0 && testFreerideCars == 1.0f &&
              testFreeridePedestrians == 1.0f && testFreeridePolice == 0.0f,
              "tutorial keeps traffic but disables police vehicles");
        TutorialPoliceState(NULL, NULL, 1, 0, 0);
        Check(testPoliceEnabled == 0, "tutorial freeride cannot activate pursuit manager");
        TutorialGameLoop();
        Check(testLoopSession == 1 && !g_tutorialFreeride, "session override clears after game loop");
        TutorialPoliceState(NULL, NULL, 1, 0, 0);
        Check(testPoliceEnabled == 1, "police behavior restored for subsequent sessions");
    }
    VirtualFree(testImage, 0, MEM_RELEASE);
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}