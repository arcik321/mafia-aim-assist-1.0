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
static void Check(int condition, const char *name);
static void *testShotGame, *testShotOwner, *testShotFrame;
static HostVector3 testShotOrigin, testShotDirection;
static float testShotPower;
static int testShotEffect, testShotCount;

static void __fastcall TestNativeShoot(void *game, void *unused, void *shooter,
                                     HostVector3 origin, HostVector3 direction,
                                     float power, int effect, void *frame, int count)
{
    (void)unused;
    testShotGame = game;
    testShotOwner = shooter;
    testShotOrigin = origin;
    testShotDirection = direction;
    testShotPower = power;
    testShotEffect = effect;
    testShotFrame = frame;
    testShotCount = count;
}
static int testDriveByBlocked;
static void *testHitSource;
static int testHitType;
static float testHitDamage;
static int testDetachCalls, testDetachIndex;
static void *testDetachCar, *testCarHitFrame;

static BYTE __fastcall TestCarHit(void *car, void *unused, int type,
                                const HostVector3 *position, const HostVector3 *normal,
                                const HostVector3 *direction, float damage, void *source,
                                DWORD bodyPart, void *frame)
{
    (void)car; (void)unused; (void)position; (void)normal; (void)direction; (void)bodyPart;
    testHitSource = source;
    testHitType = type;
    testHitDamage = damage;
    testCarHitFrame = frame;
    return 1;
}

static void __fastcall TestDetachWheel(void *car, int index,
                                     const HostVector3 *motion, const HostVector3 *impulse)
{
    uintptr_t slots = *(uintptr_t *)((BYTE *)car + CAR_WHEELS_OFFSET);
    uintptr_t wheelAddress = *(uintptr_t *)(slots + index * 4u);
    ++testDetachCalls;
    testDetachCar = car;
    testDetachIndex = index;
    *(uint32_t *)(wheelAddress + WHEEL_FLAGS_OFFSET) |= WHEEL_UNUSABLE_FLAG;
    Check(motion && motion->x == 0 && motion->y == 0 && motion->z == 0 && impulse == NULL,
          "native wheel-dropout fastcall receives two valid stack vector arguments");
}

static BYTE __fastcall TestHumanHit(void *human, void *unused, int type,
                                  const HostVector3 *position, const HostVector3 *normal,
                                  const HostVector3 *direction, float damage, void *source,
                                  DWORD bodyPart, void *frame)
{
    (void)unused; (void)position; (void)normal; (void)direction; (void)bodyPart; (void)frame;
    testHitSource = source;
    testHitType = type;
    testHitDamage = damage;
    *(float *)((BYTE *)human + 0x644u) -= damage;
    return 1;
}

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
static int __fastcall TestDriveByLine(void *collision, void *unused, const Vector3 *from,
                                    const Vector3 *delta, Vector3 *hit, Vector3 *normal,
                                    int ignore, uint32_t mask)
{
    (void)collision;
    (void)unused;
    (void)normal;
    (void)ignore;
    (void)mask;
    *hit = *from;
    if (testDriveByBlocked == 4)
        return from->z < -2.0f;
    if (testDriveByBlocked == 2 || testDriveByBlocked == 3 || testDriveByBlocked == 5)
    {
        float distance = sqrtf(Dot(*delta, *delta));
        float margin = testDriveByBlocked == 3 ? 0.3f : (testDriveByBlocked == 5 ? 0.08f : 0.0f);
        float scale = (distance - margin) / distance;
        hit->x += delta->x * scale;
        hit->y += delta->y * scale;
        hit->z += delta->z * scale;
    }
    return testDriveByBlocked != 0;
}
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

static int testMegaPanic, testMegaSeverity;

static void __fastcall TestMegaPanic(void *pedestrian, void *unused, int severity)
{
    (void)pedestrian;
    (void)unused;
    ++testMegaPanic;
    testMegaSeverity = severity;
}

static int testEntityTicks, testStopAnimations, testSuspendFlag;
static void *testTickActor, *testStopActor, *testSuspendActor;

static void __fastcall TestEntityTick(void *entity, void *unused, DWORD delta)
{
    (void)unused;
    (void)delta;
    testTickActor = entity;
    ++testEntityTicks;
}

static void __fastcall TestStopAnimation(void *entity, void *unused)
{
    (void)unused;
    testStopActor = entity;
    ++testStopAnimations;
}

static void __fastcall TestSuspendSwitcher(void *entity, void *unused, int flag)
{
    (void)unused;
    testSuspendActor = entity;
    testSuspendFlag = flag;
}

static int testFreeLookCalls, testFreeLookEnable, testFreeLookForce;

static void __fastcall TestFreeLook(void *camera, void *unused, int enable, int force)
{
    (void)unused;
    ++testFreeLookCalls;
    testFreeLookEnable = enable;
    testFreeLookForce = force;
    *((BYTE *)camera + 0x94u) = (BYTE)enable;
}

static void TestNativeJump(uintptr_t rva, void *callback)
{
    int32_t displacement = (int32_t)((uintptr_t)callback - (uintptr_t)(testImage + rva + 5));
    testImage[rva] = 0xE9;
    memcpy(testImage + rva + 1, &displacement, sizeof(displacement));
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
    BYTE npc[0x1300] = {0}, camera[0x100] = {0};
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
        g_originalShoot = TestNativeShoot;
        ArcadeShootHook(game, NULL, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6},
                  7.0f, 8, camera, 9);
        Check(testShotGame == game && testShotOwner == player && testShotFrame == camera &&
            testShotOrigin.x == 1 && testShotOrigin.y == 2 && testShotOrigin.z == 3 &&
            testShotDirection.x == 4 && testShotDirection.y == 5 && testShotDirection.z == 6 &&
            testShotPower == 7 && testShotEffect == 8 && testShotCount == 9,
            "shot wrapper preserves thiscall receiver and both by-value vectors");
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
    g_vehicleArcade = 0;
    Check(GetWorld(&worldAddress, &playerAddress) && worldAddress == (uintptr_t)mission &&
          playerAddress == (uintptr_t)player, "1.0 mission and player pointer chain");
    Check(ReadCamera(&position, &forward) && forward.z == 1.0f, "1.0 camera read");
        Check(DriveBySearchSector((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){-1, 0.5f, 10}) &&
            DriveBySearchSector((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){-1, 0.5f, -10}),
            "180-degree search includes targets at both edges of the left half-plane");
        Check(!DriveBySearchSector((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){1, 0.5f, 10}) &&
            !DriveBySearchSector((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){1, 0.5f, -10}),
            "180-degree search never includes the right half-plane");
        Check(DriveBySearchSector((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){-1, 0.5f, 10}) &&
            !DriveByReachable((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){-1, 0.5f, 10}),
            "finding a target inside the search sector does not imply a reachable lock");
        Check(DriveByReachable((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){-10, 0.5f, 0}),
            "driver can acquire a wheel on the left");
        Check(!DriveByReachable((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){10, 0.5f, 0}),
            "driver cannot acquire a wheel on the right");
        Check(!DriveByReachable((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){0, 0.5f, 10}) &&
            !DriveByReachable((Vector3){0, 1, 0}, (Vector3){0, 0, 1}, (Vector3){0, 0.5f, -10}),
            "driver does not snap across the bonnet or through the rear of the car");
        Check(DriveByReachable((Vector3){0, 1, 0}, (Vector3){1, 0, 0}, (Vector3){0, 0.5f, 10}) &&
            !DriveByReachable((Vector3){0, 1, 0}, (Vector3){1, 0, 0}, (Vector3){0, 0.5f, -10}),
            "drive-by side is relative to the moving car rather than world axes");
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
        {
          BYTE ownCar[0x2200] = {0}, policeCar[0x2200] = {0}, wheel[0x130] = {0};
        BYTE playerFrame[0xB0] = {0}, carFrame[0xB0] = {0}, ambientCop[0x200] = {0};
        BYTE scene[0x200] = {0}, aimCamera[0xB0] = {0};
          uintptr_t targets[3] = {(uintptr_t)ownCar, (uintptr_t)policeCar, (uintptr_t)npc};
          uintptr_t wheels[1] = {(uintptr_t)wheel}, crowd[1] = {(uintptr_t)ambientCop};
          DriveByView view;
          DriveByTarget selected;
          g_vehicleArcade = 0;
          *(uint32_t *)(ownCar + ENTITY_KIND_OFFSET) = CAR_KIND;
          *(float *)(ownCar + 0x688u) = 5.0f;
          *(float *)(policeCar + 0x688u) = 5.0f;
          *(uint32_t *)(policeCar + ENTITY_KIND_OFFSET) = CAR_KIND;
          policeCar[CAR_POLICE_OFFSET] = 1;
          *(Vector3 *)(policeCar + 0xD40u) = (Vector3){0, 1, 0};
          *(uint32_t *)(policeCar + CAR_WHEEL_COUNT_OFFSET) = 1;
          *(uintptr_t *)(policeCar + CAR_WHEELS_OFFSET) = (uintptr_t)wheels;
          *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0};
        *(float *)(wheel + 0x10Cu) = 0.35f;
          *(uintptr_t *)(player + 0x98u) = (uintptr_t)ownCar;
        *(uintptr_t *)(player + 0x564u) = (uintptr_t)playerFrame;
        *(uintptr_t *)(mission + 0x10u) = (uintptr_t)scene;
        *(uintptr_t *)(scene + 0x17Cu) = (uintptr_t)aimCamera;
        *(uint32_t *)(aimCamera + FRAME_FLAGS_OFFSET) = FRAME_WORLD_MATRIX_VALID;
        *(Vector3 *)(aimCamera + FRAME_WORLD_FORWARD_OFFSET) = (Vector3){-1, 0, 0};
        *(Vector3 *)(aimCamera + FRAME_WORLD_POSITION_OFFSET) = (Vector3){0, 1.5f, 0};
          *(uintptr_t *)(ownCar + 0x68u) = (uintptr_t)carFrame;
          *(uint32_t *)(player + 0x1E8u) = 1;
          *(Vector3 *)(player + 0x200u) = (Vector3){-20, 0.5f, 0};
          *(uint32_t *)(playerFrame + FRAME_FLAGS_OFFSET) = FRAME_WORLD_MATRIX_VALID;
        *(Vector3 *)(playerFrame + FRAME_WORLD_POSITION_OFFSET) = (Vector3){0, 1.5f, 0};
          *(uint32_t *)(carFrame + FRAME_FLAGS_OFFSET) = FRAME_WORLD_MATRIX_VALID;
          *(Vector3 *)(carFrame + FRAME_WORLD_FORWARD_OFFSET) = (Vector3){0, 0, 1};
          *(uintptr_t *)(mission + WORLD_LIST_BEGIN_OFFSET) = (uintptr_t)targets;
          *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(targets + 3);
          *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
          npc[0x5Du] = 1;
          {
              HostVector3 hitPosition = {1, 2, 3};
              static const BYTE nativeHitEntry[] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x34};
              static const BYTE nativeHitCall[] = {0xE8, 0xD0, 0x44, 0x08, 0};
              int32_t patchedTarget;
              memcpy(testImage + 0x096710u, nativeHitEntry, sizeof(nativeHitEntry));
              memcpy(testImage + 0x01223Bu, nativeHitCall, sizeof(nativeHitCall));
              InstallPoliceHitTrace();
              memcpy(&patchedTarget, testImage + 0x01223Cu, 4);
              Check((uintptr_t)(testImage + 0x012240u) + patchedTarget == (uintptr_t)TracePoliceHit,
                  "hit trace patches the actual entity hit call RVA1223B");
              TestNativeJump(0x096710u, (void *)TestHumanHit);
              TracePoliceHit(npc, NULL, 0, &hitPosition, &hitPosition, &hitPosition, 10,
                             player, 5, NULL);
              Check(testHitType == 0 && testHitSource == player && testHitDamage == 10 &&
                    *(float *)(npc + 0x644u) == 90,
                    "actual police hit trace forwards native damage rather than manufacturing kills");
              *(float *)(npc + 0x644u) = 100.0f;
          }
          g_lineTest = TestDriveByLine;
          Check(GetActiveWorld(&worldAddress, &playerAddress) && !GetWorld(&worldAddress, &playerAddress),
              "driver world remains accessible while crouch stays disabled");
          Check(ReadDriveByView((uintptr_t)player, &view) && view.aimDirection.x == -1.0f,
              "driver view follows native active camera rather than stale shot target");
          *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 3};
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) &&
              selected.owner == (uintptr_t)policeCar && selected.kind == 1,
              "wheel nearest the crosshair is chosen over an off-axis foot officer");
            Check(fabsf(selected.point.y - (0.5f + 0.35f * 0.85f)) < 0.0001f,
                "wheel target lies on tyre band rather than hub centre");
            {
              Vector3 rubber;
              Check(TyreAimPoint((Vector3){1, 2, 3}, (Vector3){1, 0, 0}, 0.4f, &rubber) &&
                  fabsf(rubber.x - 1.34f) < 0.0001f && rubber.y == 2 && rubber.z == 3,
                  "tyre radial aim follows vehicle tilt instead of world vertical");
              Check(!TyreAimPoint((Vector3){1, 2, 3}, (Vector3){0, 0, 0}, 0.4f, &rubber),
                  "unknown tyre orientation is rejected rather than falling back to rim");
            }
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
            *(float *)(ownCar + 0x688u) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "during a chase the tyres of a moving police car are preferred over a foot officer");
            *(float *)(policeCar + 0x688u) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "tyres of an empty stopped police car are ignored and the foot officer is locked");
            *(float *)(policeCar + 0x688u) = 5.0f;
            *(float *)(npc + ENTITY_HEALTH_OFFSET) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "dead foot officer is ignored and stopped car falls back to wheels");
            *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
            npc[0x5Eu] = 1;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "death-processed officer cannot regain priority from stale positive health");
                npc[0x5Eu] = 0;
                npc[0x5Du] = 0;
                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                    "native not-alive actor cannot be selected from stale positive health");
                npc[0x5Du] = 1;
            npc[0x5Eu] = 0;
            *(float *)(ownCar + 0x688u) = -5.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "the player's own speed does not change target priority");
            *(float *)(ownCar + 0x688u) = 5.0f;
            view.aimDirection = (Vector3){0, 0, 1};
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-30, 0, 0};
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "police wheel outside the visible camera window is never locked");
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
            view.aimDirection = (Vector3){-1, 0, 0};
          *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){10, 0.5f, 0};
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
              "right-hand police wheel is rejected and eligible left officer is selected");
          *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0};
          *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = WHEEL_UNUSABLE_FLAG;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
              "unusable wheel does not retain priority");
          *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = 0;
          testDriveByBlocked = 1;
          Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
              "obstacle in firing line prevents any vehicle lock");
          g_lineTestState = -1;
          testDriveByBlocked = 0;
          Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
              "missing collision binding fails closed for drive-by");
          g_lineTestState = 1;
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
          testDriveByBlocked = 2;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
              "collision at wheel endpoint is accepted rather than treated as cover");
          testDriveByBlocked = 5;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
              "visible rubber surface just before the tyre aim point is accepted");
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
          testDriveByBlocked = 3;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "cover before the tyre band no longer passes a full-radius tolerance");
          testDriveByBlocked = 0;
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
          {
            LONG mouseX = 0, mouseY = 0;
            float yaw = atan2f(view.aimDirection.x, view.aimDirection.z);
            SeedGains();
            RunAim(&mouseX, &mouseY, (uintptr_t)mission, (uintptr_t)player, view.cameraPosition,
                 view.aimDirection, yaw, 0, 0, 0, 10000);
            Check(g_target == (uintptr_t)policeCar && (mouseX != 0 || mouseY != 0),
                "drive-by controller locks a wheel and generates ordinary mouse correction");
            testDriveByBlocked = 1;
            mouseX = mouseY = 0;
            RunAim(&mouseX, &mouseY, (uintptr_t)mission, (uintptr_t)player, view.cameraPosition,
                 view.aimDirection, yaw, 0, 0, 0, 10010);
            Check(g_target == 0 && mouseX == 0 && mouseY == 0,
                "drive-by lock and correction stop immediately when shot is obstructed");
            testDriveByBlocked = 0;
            *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){10, 0.5f, 0};
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){5, 0, 0};
            RunAim(&mouseX, &mouseY, (uintptr_t)mission, (uintptr_t)player, view.cameraPosition,
                 view.aimDirection, yaw, 0, 0, 0, 10020);
            Check(g_target == 0 && mouseX == 0 && mouseY == 0,
                "controller cannot retain a target after it crosses to driver's right");
            *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0};
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
            ReleaseAim();
          }
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
          policeCar[CAR_POLICE_OFFSET] = 0;
            testDriveByBlocked = 3;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "foot officer body surface is not mistaken for cover before the chest aim point");
            testDriveByBlocked = 0;
            view.aimDirection = (Vector3){0, 0, 1};
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "nearby officer outside the visible camera window is never locked");
                view.cameraPosition.z = -5.0f;
                testDriveByBlocked = 4;
                Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                    "officer hidden from the chase camera is not locked even when the gun line is clear");
                testDriveByBlocked = 1;
                Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                    "nearby officer still cannot be acquired through cover in front of the gun");
                testDriveByBlocked = 0;
                    *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-0.8f, 0, 0};
                    Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                        "officer at driver's door remains aimable at less than one metre from the gun");
                    *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
                view.cameraPosition.z = 0.0f;
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){5, 0, 0};
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "near officer acquisition never bypasses driver's right-side exclusion");
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-12, 0, 0};
                Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                    "distant officer outside the visible camera window is never locked");
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
            view.aimDirection = (Vector3){-1, 0, 0};
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
          Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
              "civilian cars and non-hostile actors are excluded from drive-by");
          ambientCop[0xCu] = 1;
          ambientCop[0x15Au] = 1;
          *(float *)(ambientCop + 0x15Cu) = 1.0f;
          *(Vector3 *)(ambientCop + 0x10u) = (Vector3){-7.5f, 0, 0};
          *(uintptr_t *)(testImage + CROWD_LIST_RVA) = (uintptr_t)crowd;
          *(uintptr_t *)(testImage + CROWD_LIST_RVA + 4u) = (uintptr_t)(crowd + 1);
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 3,
              "ambient patrol officer is supported without casting crowd records as actors");
            *(float *)(ownCar + 0x688u) = 0.0f;
            policeCar[CAR_POLICE_OFFSET] = 1;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "pursuit prefers the tyres of a moving police car over an ambient patrol officer");
            *(float *)(policeCar + 0x688u) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 3,
                "ambient patrol officer is locked when the police car is empty and stopped");
            *(float *)(ambientCop + 0x15Cu) = 0.0f;
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "dead ambient officer and an empty stopped car leave nothing to lock");
            *(float *)(ambientCop + 0x15Cu) = -1.0f;
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "terminal negative ambient damage state cannot be targeted");
            *(float *)(policeCar + 0x688u) = 5.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "tyres remain the fallback when the ambient officer is dead");
            *(float *)(ambientCop + 0x15Cu) = 1.0f;
            policeCar[CAR_POLICE_OFFSET] = 0;
            *(float *)(ownCar + 0x688u) = 5.0f;
            view.aimDirection = (Vector3){0, 0, 1};
            Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                "ambient patrol officer outside the visible camera window is never locked");
                {
                  static const BYTE entry[] = {0x81, 0xEC, 0xD4, 0, 0, 0};
                  static const BYTE callA[] = {0xE8, 0xA6, 0xEC, 0x13, 0};
                  static const BYTE callB[] = {0xE8, 0x01, 0xE1, 0x13, 0};
                  uintptr_t sites[2] = {0x0A5145u, 0x0A5CEAu};
                  int siteIndex, argument;
                  void (__stdcall *fire)(void *, void *, HostVector3, HostVector3, float, int, void *, int);
                  for (siteIndex = 0; siteIndex < 2; ++siteIndex)
                  {
                    BYTE *caller = testImage + sites[siteIndex] - 48u;
                    memcpy(caller, "\x8B\x4C\x24\x04", 4);
                    for (argument = 0; argument < 11; ++argument)
                        memcpy(caller + 4 + argument * 4, "\xFF\x74\x24\x30", 4);
                    memcpy(testImage + sites[siteIndex] + 5, "\xC2\x30\x00", 3);
                  }
                  memcpy(testImage + 0x1E3DF0u, entry, sizeof(entry));
                  memcpy(testImage + sites[0], callA, sizeof(callA));
                  memcpy(testImage + sites[1], callB, sizeof(callB));
                  InstallArcadeShotHook();
                  TestNativeJump(0x1E3DF0u, (void *)TestNativeShoot);
                  g_shot = (AimShotFn)AimArcadeShot;
                  g_vehicleArcade = 1;
                  g_vehicleOneShot = 1;
                  g_held = 1;
                  ambientCop[0xCu] = 0;
                  policeCar[CAR_POLICE_OFFSET] = 1;
                  *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){10, 0.5f, 0};
                  view.aimDirection = (Vector3){-1, 0, 0};
                  view.cameraPosition = (Vector3){0, 1.5f, 0};
                  *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0};
                  testDriveByBlocked = 1;
                  Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                        "arcade mode locks a wheel of a driving hostile car even when hidden from the camera");
                  testDriveByBlocked = 0;
                  view.aimDirection = (Vector3){0, 0, 1};
                  Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                        "arcade mode locks a wheel of a driving hostile car outside the visible window");
                  {
                      BYTE hiddenSeats[0xA0] = {0};
                      BYTE hiddenRider[0x1300] = {0};
                      *(uint32_t *)(hiddenRider + ENTITY_KIND_OFFSET) = SCRIPTABLE_NPC_KIND;
                      *(uint32_t *)(hiddenRider + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
                      hiddenRider[0x5Du] = 1;
                      *(uintptr_t *)(hiddenSeats + 0x10u) = (uintptr_t)hiddenRider;
                      *(uintptr_t *)(policeCar + CAR_SEATS_BEGIN_OFFSET) = (uintptr_t)hiddenSeats;
                      *(uintptr_t *)(policeCar + CAR_SEATS_END_OFFSET) = (uintptr_t)(hiddenSeats + sizeof(hiddenSeats));
                      *(float *)(policeCar + 0x688u) = 0.0f;
                      Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
                            "wheels of a stopped car with police inside are not locked while out of view");
                      view.aimDirection = (Vector3){-1, 0, 0};
                      Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                            "wheels of a stopped car with police inside are lockable when visible");
                      *(uintptr_t *)(policeCar + CAR_SEATS_BEGIN_OFFSET) = 0;
                      *(uintptr_t *)(policeCar + CAR_SEATS_END_OFFSET) = 0;
                      ambientCop[0xCu] = 1;
                      *(Vector3 *)(ambientCop + 0x10u) = (Vector3){6, 0, 0};
                      Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 3,
                            "an officer standing behind the player's car is lockable");
                      ambientCop[0xCu] = 0;
                      *(Vector3 *)(ambientCop + 0x10u) = (Vector3){-7.5f, 0, 0};
                      *(float *)(policeCar + 0x688u) = 5.0f;
                  }
                  view.aimDirection = (Vector3){-1, 0, 0};
                  *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){10, 0.5f, 0};
                  *(Vector3 *)(aimCamera + FRAME_WORLD_FORWARD_OFFSET) = (Vector3){1, 0, 0};
                  testDriveByBlocked = 0;
                {
                    LONG mouseX = 0, mouseY = 0;
                    SeedGains();
                    RunAim(&mouseX, &mouseY, (uintptr_t)mission, (uintptr_t)player, view.cameraPosition,
                        view.aimDirection, atan2f(view.aimDirection.x, view.aimDirection.z),
                        0, 0, 0, 20000);
                    Check(g_target == (uintptr_t)policeCar && (mouseX != 0 || mouseY != 0),
                       "arcade mode retains visible mouse aim correction instead of only selecting target");
                }
                  for (siteIndex = 0; siteIndex < 2; ++siteIndex)
                  {
                    fire = (void (__stdcall *)(void *, void *, HostVector3, HostVector3, float, int, void *, int))
                        (testImage + sites[siteIndex] - 48u);
                    fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                    Check(testShotOwner == player && testShotGame == game && testShotFrame == aimCamera &&
                        testShotPower == 7 && testShotCount == 1 && testShotDirection.x > 0,
                        "native shot call redirects player car shot to a right-side tyre with no spread");
                    Check(fabsf(testShotOrigin.x + testShotDirection.x * (4.0f / 6.0f) - 10.0f) < 0.0001f &&
                        fabsf(testShotOrigin.y + testShotDirection.y * (4.0f / 6.0f) - (0.5f + 0.35f * 0.85f)) < 0.0001f,
                        "tyre projectile starts four metres outside target instead of inside car collider");
                    Check(fabsf(sqrtf(testShotDirection.x * testShotDirection.x +
                                testShotDirection.y * testShotDirection.y +
                                testShotDirection.z * testShotDirection.z) - 6.0f) < 0.0001f,
                        "native tyre shot range exceeds the four-metre target distance");
                    fire(game, npc, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                    Check(testShotOwner == npc && testShotPower == 7 && testShotCount == 9 &&
                        testShotOrigin.x == 1 && testShotDirection.x == 4,
                        "NPC shot preserves original damage, origin, direction and pellet count");
                  }
                  *(uintptr_t *)(player + 0x98u) = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotCount == 9 && testShotOrigin.x == 1,
                      "player on-foot shot receives no arcade accuracy or damage change");
                  *(uintptr_t *)(player + 0x98u) = (uintptr_t)ownCar;
                  g_held = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotCount == 9 &&
                      testShotOrigin.x == 1 && testShotDirection.x == 4,
                      "manual player car shot preserves projectile damage; lethal policy is applied at police Hit");
                  *(uint32_t *)(aimCamera + FRAME_FLAGS_OFFSET) = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotDirection.x == 4,
                      "manual one-shot damage does not depend on camera matrix freshness");
                  *(uint32_t *)(aimCamera + FRAME_FLAGS_OFFSET) = FRAME_WORLD_MATRIX_VALID;
                  g_vehicleOneShot = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotCount == 9 && testShotOrigin.x == 1,
                      "manual player car shot is fully native when one-shot option is disabled");
                  g_held = 1;
                  g_vehicleOneShot = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotCount == 1,
                      "one-shot damage can be disabled independently of arcade accuracy");
                  g_vehicleOneShot = 1;
                  *(float *)(ownCar + 0x688u) = 0.0f;
                  ambientCop[0xCu] = 1;
                  *(float *)(policeCar + 0x688u) = 0.0f;
                  *(Vector3 *)(aimCamera + FRAME_WORLD_FORWARD_OFFSET) = (Vector3){-1, 0, 0};
                  memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotDirection.x < 0,
                      "arcade shot targets the officer nearest the crosshair instead of a right-side tyre");
                  Check(fabsf(testShotOrigin.x + testShotDirection.x * 0.5f + 7.5f) < 0.0001f &&
                      fabsf(testShotOrigin.y + testShotDirection.y * 0.5f - 1.25f) < 0.0001f,
                      "officer projectile starts two metres outside target and passes through upper torso");
                  Check(fabsf(sqrtf(testShotDirection.x * testShotDirection.x +
                              testShotDirection.y * testShotDirection.y +
                              testShotDirection.z * testShotDirection.z) - 4.0f) < 0.0001f,
                      "native officer shot range reaches beyond the two-metre target distance");
                  ambientCop[0xCu] = 0;
                  policeCar[CAR_POLICE_OFFSET] = 0;
                  fire(game, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotPower == 7 && testShotCount == 9 && testShotDirection.x == 4,
                      "missing target retains native projectile damage, direction and spread");
                  fire(camera, player, (HostVector3){1, 2, 3}, (HostVector3){4, 5, 6}, 7, 8, aimCamera, 9);
                  Check(testShotGame == camera && testShotPower == 7 && testShotOrigin.x == 1,
                      "arcade callback rejects a different game-world receiver");
                  ambientCop[0xCu] = 1;
                  *(float *)(policeCar + 0x688u) = 5.0f;
                  *(float *)(ownCar + 0x688u) = 5.0f;
                  g_vehicleOneShot = 1;
                  g_vehicleArcade = 0;
                  {
                    HostVector3 actualHit = {1, 2, 3};
                    *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
                    *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
                    g_vehicleArcade = 1;
                    g_hitDamage = AimArcadeHitDamage;
                      Check(TracePoliceHit(npc, NULL, 0, &actualHit, &actualHit, &actualHit, 0,
                          player, 5, NULL) == 1, "police Hit preserves the native boolean in AL after logging");
                    Check(testHitDamage == 1000 && *(float *)(npc + ENTITY_HEALTH_OFFSET) <= 0,
                        "confirmed player car hit receives lethal damage even if projectile conversion yielded zero");
                    *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
                    g_vehicleOneShot = 0;
                    TracePoliceHit(npc, NULL, 0, &actualHit, &actualHit, &actualHit, 10,
                               player, 5, NULL);
                    Check(testHitDamage == 10 && *(float *)(npc + ENTITY_HEALTH_OFFSET) == 90,
                        "one-shot disabled preserves actual native hit damage");
                    g_vehicleOneShot = 1;
                    Check(AimArcadeHitDamage((uintptr_t)game, (uintptr_t)npc, (uintptr_t)npc, 0, 10) == 10,
                        "NPC hit never receives player-only lethal damage");
                    *(uintptr_t *)(player + 0x98u) = 0;
                    Check(AimArcadeHitDamage((uintptr_t)game, (uintptr_t)npc, (uintptr_t)player, 0, 10) == 10,
                        "on-foot hit never receives driver-only lethal damage");
                    *(uintptr_t *)(player + 0x98u) = (uintptr_t)ownCar;
                      {
                        BYTE otherWheel[0x130] = {0}, wheelModel[0x130] = {0}, child[0x130] = {0};
                        BYTE otherModel[0x130] = {0};
                        uintptr_t wheelSlots[2] = {(uintptr_t)wheel, (uintptr_t)otherWheel};
                        void **carTable = (void **)(testImage + 0x23BC08u);
                        void **extendedTable = (void **)(testImage + 0x23BD68u);
                        HostHumanHitFn wheelHit;
                        uintptr_t hitWheel = 0;
                        int hitIndex = -1;
                        memcpy(testImage + 0x06A670u, "\x81\xEC\xF0\x00\x00\x00", 6);
                        memcpy(testImage + 0x06E0C0u, "\x83\xEC\x2C\x8B\x81\x24\x0D\x00\x00", 9);
                        carTable[31] = extendedTable[31] = testImage + 0x06A670u;
                        InstallArcadeWheelHitHook();
                        Check(carTable[31] == (void *)ArcadeCarHit && extendedTable[31] == carTable[31],
                            "both verified car Hit vtables install the confirmed wheel hook");
                        TestNativeJump(0x06A670u, (void *)TestCarHit);
                        TestNativeJump(0x06E0C0u, (void *)TestDetachWheel);
                        wheelHit = (HostHumanHitFn)carTable[31];
                        g_wheelHit = AimArcadeWheelHit;
                        memset(g_wheelHits, 0, sizeof(g_wheelHits));
                        policeCar[CAR_POLICE_OFFSET] = 1;
                        *(uintptr_t *)(policeCar + CAR_WHEELS_OFFSET) = (uintptr_t)wheelSlots;
                        *(uint32_t *)(policeCar + CAR_WHEEL_COUNT_OFFSET) = 2;
                        *(uintptr_t *)(wheel + 4u) = (uintptr_t)wheelModel;
                        *(uintptr_t *)(otherWheel + 4u) = (uintptr_t)otherModel;
                        *(uintptr_t *)(child + 0x120u) = (uintptr_t)wheelModel;
                        *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = 0;
                        g_playerShotSequence = 1;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, NULL);
                        Check(testHitDamage == 7 && g_wheelHits[0].hits == 0,
                            "non-wheel collision preserves damage and does not count as a tyre hit");
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, npc, 0, wheelModel);
                        Check(testHitDamage == 7 && *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) == 0,
                            "NPC wheel shot keeps native damage and wheel state");
                        *(uintptr_t *)(player + 0x98u) = 0;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, wheelModel);
                        Check(testHitDamage == 7 && *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) == 0,
                            "on-foot wheel shot keeps native damage and wheel state");
                        *(uintptr_t *)(player + 0x98u) = (uintptr_t)ownCar;
                        g_vehicleArcade = 0;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, wheelModel);
                        Check(testHitDamage == 7 && *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) == 0,
                            "disabled arcade preserves native wheel shots");
                        g_vehicleArcade = 1;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, child);
                        Check(*(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) == 0x80000000u &&
                            testHitDamage == 0 && testDetachCalls == 0 && testCarHitFrame == child,
                            "first confirmed child-frame hit punctures only the matching tyre without lethal car damage");
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, wheelModel);
                        Check(testDetachCalls == 0 && testHitDamage == 0,
                            "duplicate notification from the same player shot cannot detach a tyre");
                        g_playerShotSequence = 2;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, otherModel);
                        Check(*(uint32_t *)(otherWheel + WHEEL_FLAGS_OFFSET) == 0x80000000u && testDetachCalls == 0,
                            "different wheel has its own first-hit state");
                        g_playerShotSequence = 3;
                        Check(wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7,
                            player, 0, wheelModel) == 1, "car Hit preserves the native boolean in AL after detachment");
                        Check(testDetachCalls == 1 && testDetachCar == policeCar && testDetachIndex == 0 &&
                            (*(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) & WHEEL_UNUSABLE_FLAG),
                            "second confirmed hit invokes native dropout with the exact car and wheel index");
                        Check(AimArcadeWheelHit((uintptr_t)game, (uintptr_t)policeCar, (uintptr_t)player,
                            0, (uintptr_t)wheelModel, 4, &hitWheel, &hitIndex) == 0,
                            "detached wheel is no longer eligible for a hit-stage change");
                        *(uint32_t *)(policeCar + 0x18u) = 123;
                        g_playerShotSequence = 4;
                        wheelHit(policeCar, NULL, 0, &actualHit, &actualHit, &actualHit, 7, player, 0, otherModel);
                        Check(testDetachCalls == 1,
                            "reused car pointer with a different identity resets its wheel-hit count");
                        g_wheelHit = NULL;
                        {
                            static const BYTE freeLookEntry[] = {0x8A, 0x81, 0x94, 0, 0, 0, 0x84, 0xC0, 0x75, 0x14};
                            BYTE twin[0x130] = {0};
                            uintptr_t pair[2] = {(uintptr_t)wheel, (uintptr_t)twin};
                            DriveByTarget chosen = {0};
                            BYTE *cameraBlock = game + 0x4Cu;
                            int32_t jump;
                            ambientCop[0xCu] = 0;
                            *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
                            policeCar[CAR_POLICE_OFFSET] = 1;
                            *(uintptr_t *)(policeCar + CAR_WHEELS_OFFSET) = (uintptr_t)pair;
                            *(uint32_t *)(policeCar + CAR_WHEEL_COUNT_OFFSET) = 2;
                            *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = 0;
                            *(Vector3 *)(wheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 1.0f};
                            *(Vector3 *)(twin + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0.5f};
                            *(float *)(twin + 0x10Cu) = 0.35f;
                            *(float *)(ownCar + 0x688u) = 5.0f;
                            memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                            Check(ReadDriveByView((uintptr_t)player, &view) &&
                                  FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.kind == 1 && chosen.wheel == 1,
                                  "without a held lock the best aligned wheel is chosen");
                            g_driveTarget = (DriveByTarget){(uintptr_t)policeCar, 1, 0, {0, 0, 0}};
                            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.kind == 1 && chosen.wheel == 0,
                                  "a held wheel lock does not hop to a marginally better wheel");
                            *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = WHEEL_UNUSABLE_FLAG;
                            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.wheel == 1,
                                  "a held lock is dropped once its wheel is detached");
                            memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                            *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = 0;

                            memcpy(testImage + 0x1DAAD0u, freeLookEntry, sizeof(freeLookEntry));
                            jump = (int32_t)((uintptr_t)TestFreeLook - (uintptr_t)(testImage + 0x1DAAD0u + 0x0Au + 5u));
                            testImage[0x1DAAD0u + 0x0Au] = 0xE9;
                            memcpy(testImage + 0x1DAAD0u + 0x0Bu, &jump, 4);
                            jump = (int32_t)((uintptr_t)TestFreeLook - (uintptr_t)(testImage + 0x1DAAD0u + 0x1Eu + 5u));
                            testImage[0x1DAAD0u + 0x1Eu] = 0xE9;
                            memcpy(testImage + 0x1DAAD0u + 0x1Fu, &jump, 4);
                            g_vehicleFreeCamera = 1;
                            *(uint32_t *)(cameraBlock + 0x10u) = 7;
                            cameraBlock[0x94u] = 0;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 1 && testFreeLookEnable == 1 && testFreeLookForce == 1 &&
                                  cameraBlock[0x94u] == 1,
                                  "driver free look is enabled and locks the chase camera on car entry");
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 1, "free look already on is not re-applied every frame");
                            cameraBlock[0x94u] = 0;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 2 && testFreeLookForce == 0,
                                  "free look re-enabled after the game clears it keeps the chosen camera mode");
                            cameraBlock[0x94u] = 0;
                            *(uint32_t *)(cameraBlock + 0x10u) = 0x14;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 2, "race and cutscene cameras are never taken over");
                            *(uint32_t *)(cameraBlock + 0x10u) = 7;
                            *(uint32_t *)(player + 0xACu) = 1;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 2, "passenger seat camera is left to the game");
                            *(uint32_t *)(player + 0xACu) = 0;
                            cameraBlock[0x94u] = 1;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, 0);
                            Check(testFreeLookCalls == 3 && testFreeLookEnable == 0 && cameraBlock[0x94u] == 0,
                                  "leaving the car restores the native camera");
                            g_vehicleFreeCamera = 0;
                            cameraBlock[0x94u] = 0;
                            UpdateDriverCamera((uintptr_t)mission, (uintptr_t)player, (uintptr_t)ownCar);
                            Check(testFreeLookCalls == 3, "disabled option never touches the camera");
                            g_vehicleFreeCamera = 1;
                            memset(testImage + 0x1DAAD0u, 0, 0x30);
                        }
                        {
                            *(float *)(player + 0x604u) = 100.0f;
                            *(float *)(player + 0x644u) = 100.0f;
                            player[0x5Du] = 1;
                            g_cheatHealth = 1000;
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(*(float *)(player + 0x604u) == 1000.0f && *(float *)(player + 0x644u) == 1000.0f,
                                  "cheat health raises the ceiling and fills the player");
                            *(float *)(player + 0x644u) = 640.0f;
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(*(float *)(player + 0x644u) == 640.0f, "cheat health does not refill damage");
                            g_cheatHealth = 0;
                            *(float *)(ownCar + 0x2A0u) = 0.8f;
                            g_cheatAcceleration = 2.0f;
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(fabsf(*(float *)(ownCar + 0x2A0u) - 1.6f) < 0.0001f,
                                  "cheat acceleration doubles the motor-force factor");
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(fabsf(*(float *)(ownCar + 0x2A0u) - 1.6f) < 0.0001f,
                                  "cheat acceleration is not compounded every frame");
                            *(float *)(ownCar + 0x2A0u) = 0.5f;
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(fabsf(*(float *)(ownCar + 0x2A0u) - 1.0f) < 0.0001f,
                                  "engine damage recalculated by the game becomes the new base");
                            g_cheatAcceleration = 1.0f;
                            ApplyPlayerCheats((uintptr_t)player, (uintptr_t)ownCar);
                            Check(fabsf(*(float *)(ownCar + 0x2A0u) - 0.5f) < 0.0001f,
                                  "disabling the cheat restores the original motor-force factor");
                        }
                        {
                            BYTE seatRecords[0xA0 * 2] = {0}, trafficCar[0x2200] = {0};
                            BYTE trafficWheel[0x130] = {0}, trafficModel[0x40] = {0};
                            BYTE manager[0x300] = {0}, element[0x20] = {0};
                            uintptr_t crewRecord[3] = {(uintptr_t)policeCar, 0, 0};
                            uintptr_t trafficWheels[1] = {(uintptr_t)trafficWheel};
                            uintptr_t elementList[1] = {(uintptr_t)element};
                            uintptr_t withManager[4] = {(uintptr_t)ownCar, (uintptr_t)policeCar,
                                                        (uintptr_t)npc, (uintptr_t)manager};
                            uintptr_t carsFound[MAX_CANDIDATE_CARS], carCount, hitWheel = 0;
                            int hitIndex = -1;
                            DriveByTarget chosen = {0};
                            policeCar[CAR_POLICE_OFFSET] = 0;
                            *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
                            npc[0x5Du] = 1;
                            npc[0x5Eu] = 0;
                            *(uintptr_t *)(seatRecords + 0x10u) = (uintptr_t)npc;
                            *(uintptr_t *)(policeCar + CAR_SEATS_BEGIN_OFFSET) = (uintptr_t)seatRecords;
                            *(uintptr_t *)(policeCar + CAR_SEATS_END_OFFSET) = (uintptr_t)(seatRecords + sizeof(seatRecords));
                            Check(IsHostileCar((uintptr_t)policeCar), "car crewed by a mafioso is a valid wheel target");
                            *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
                            Check(!IsHostileCar((uintptr_t)policeCar), "car with a civilian occupant is not targeted");
                            *(uintptr_t *)(testImage + MAFIA_CREWS_BEGIN_RVA) = (uintptr_t)crewRecord;
                            *(uintptr_t *)(testImage + MAFIA_CREWS_END_RVA) = (uintptr_t)(crewRecord + 3);
                            Check(IsHostileCar((uintptr_t)policeCar), "spawned mafia crew car is a valid wheel target");
                            *(uintptr_t *)(testImage + MAFIA_CREWS_BEGIN_RVA) = 0;
                            *(uintptr_t *)(testImage + MAFIA_CREWS_END_RVA) = 0;
                            *(uintptr_t *)(policeCar + CAR_SEATS_BEGIN_OFFSET) = 0;
                            *(uintptr_t *)(policeCar + CAR_SEATS_END_OFFSET) = 0;

                            *(uint32_t *)(trafficCar + ENTITY_KIND_OFFSET) = CAR_KIND;
                            trafficCar[CAR_POLICE_OFFSET] = 1;
                            *(Vector3 *)(trafficCar + 0xD40u) = (Vector3){0, 1, 0};
                            *(uint32_t *)(trafficCar + CAR_WHEEL_COUNT_OFFSET) = 1;
                            *(uintptr_t *)(trafficCar + CAR_WHEELS_OFFSET) = (uintptr_t)trafficWheels;
                            *(float *)(trafficCar + 0x688u) = 5.0f;
                            *(Vector3 *)(trafficWheel + WHEEL_HUB_OFFSET) = (Vector3){-10, 0.5f, 0};
                            *(float *)(trafficWheel + 0x10Cu) = 0.35f;
                            *(uintptr_t *)(trafficWheel + 4u) = (uintptr_t)trafficModel;
                            element[0] = 1;
                            *(uintptr_t *)(element + 0xCu) = (uintptr_t)trafficCar;
                            *(uint32_t *)(manager + ENTITY_KIND_OFFSET) = TRAFFIC_MANAGER_KIND;
                            *(uintptr_t *)(manager + TRAFFIC_ELEMENTS_OFFSET) = (uintptr_t)elementList;
                            *(uint32_t *)(manager + TRAFFIC_COUNT_OFFSET) = 1;
                            *(uintptr_t *)(mission + WORLD_LIST_BEGIN_OFFSET) = (uintptr_t)withManager;
                            *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(withManager + 4);
                            carCount = CollectCars((uintptr_t)mission, carsFound);
                            Check(carCount == 3 && carsFound[2] == (uintptr_t)trafficCar,
                                  "patrol cars owned only by the ambient-traffic manager are discovered");
                            *(float *)(ownCar + 0x688u) = 0.0f;
                            memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                            Check(ReadDriveByView((uintptr_t)player, &view) &&
                                  FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.kind == 1 && chosen.owner == (uintptr_t)trafficCar,
                                  "wheel of a moving traffic-manager police car is locked");
                            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
                            *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
                            *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
                            memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.kind == 1,
                                  "chase preference keeps tyres ahead of a mafioso on foot");
                            *(float *)(trafficCar + 0x688u) = 0.0f;
                            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                  chosen.kind == 2 && chosen.owner == (uintptr_t)npc,
                                  "mafioso on foot is locked when the nearby car is empty and stopped");
                            {
                                BYTE trafficSeats[0xA0] = {0};
                                BYTE rider[0x1300] = {0};
                                *(uint32_t *)(rider + ENTITY_KIND_OFFSET) = SCRIPTABLE_NPC_KIND;
                                *(uint32_t *)(rider + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
                                rider[0x5Du] = 1;
                                *(uintptr_t *)(trafficSeats + 0x10u) = (uintptr_t)rider;
                                *(uintptr_t *)(trafficCar + CAR_SEATS_BEGIN_OFFSET) = (uintptr_t)trafficSeats;
                                *(uintptr_t *)(trafficCar + CAR_SEATS_END_OFFSET) = (uintptr_t)(trafficSeats + sizeof(trafficSeats));
                                *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
                                memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                      chosen.kind == 1 && chosen.owner == (uintptr_t)trafficCar,
                                      "tyres of a stopped car with police inside can be locked");
                                ambientCop[0xCu] = 1;
                                *(Vector3 *)(ambientCop + 0x10u) = (Vector3){-7.5f, 0, 3};
                                memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &chosen) &&
                                      chosen.kind == 3,
                                      "people on foot are preferred over the tyres of a stopped occupied car");
                                *(uintptr_t *)(trafficCar + CAR_SEATS_BEGIN_OFFSET) = 0;
                                *(uintptr_t *)(trafficCar + CAR_SEATS_END_OFFSET) = 0;
                                ambientCop[0xCu] = 0;
                                *(Vector3 *)(ambientCop + 0x10u) = (Vector3){-7.5f, 0, 0};
                                *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
                                *(float *)(trafficCar + 0x688u) = 5.0f;
                            }
                            Check(AimArcadeHitDamage((uintptr_t)game, (uintptr_t)npc, (uintptr_t)player, 0, 10) == 1000.0f,
                                  "confirmed hit on a mafioso receives lethal damage");
                            Check(AimArcadeWheelHit((uintptr_t)game, (uintptr_t)trafficCar, (uintptr_t)player, 0,
                                  (uintptr_t)trafficModel, 9, &hitWheel, &hitIndex) == 1 &&
                                  hitWheel == (uintptr_t)trafficWheel,
                                  "first hit on a traffic-manager car wheel punctures it");
                            {
                                BYTE copA[0x200] = {0}, copB[0x200] = {0}, copC[0x200] = {0};
                                BYTE *records[3] = {copA, copB, copC};
                                uintptr_t switchCrowd[3] = {(uintptr_t)copA, (uintptr_t)copB, (uintptr_t)copC};
                                uintptr_t savedBegin = *(uintptr_t *)(testImage + CROWD_LIST_RVA);
                                uintptr_t savedEnd = *(uintptr_t *)(testImage + CROWD_LIST_RVA + 4u);
                                DriveByTarget held = {0}, next = {0};
                                Vector3 toLeft;
                                LONG mouseX = 0, mouseY = 0;
                                int record;
                                trafficCar[CAR_POLICE_OFFSET] = 0;
                                *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = 1u;
                                for (record = 0; record < 3; ++record)
                                {
                                    records[record][0xCu] = 1;
                                    records[record][0x15Au] = 1;
                                    *(float *)(records[record] + 0x15Cu) = 1.0f;
                                }
                                *(Vector3 *)(copA + 0x10u) = (Vector3){-8, 0, -3};
                                *(Vector3 *)(copB + 0x10u) = (Vector3){-14, 0, 0.3f};
                                *(Vector3 *)(copC + 0x10u) = (Vector3){-6, 0, 2.5f};
                                *(uintptr_t *)(testImage + CROWD_LIST_RVA) = (uintptr_t)switchCrowd;
                                *(uintptr_t *)(testImage + CROWD_LIST_RVA + 4u) = (uintptr_t)(switchCrowd + 3);
                                ReadDriveByView((uintptr_t)player, &view);
                                toLeft = Subtract((Vector3){-8, 1.25f, -3}, view.cameraPosition);
                                Normalize(&toLeft);
                                view.aimDirection = toLeft;
                                memset(&g_driveTarget, 0, sizeof(g_driveTarget));
                                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &next) &&
                                      next.owner == (uintptr_t)copA,
                                      "crosshair near the left target selects it");
                                held = next;
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 1, &next) &&
                                      next.owner == (uintptr_t)copB,
                                      "first right flick selects the far centre target");
                                held = next;
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 1, &next) &&
                                      next.owner == (uintptr_t)copC,
                                      "second right flick selects the near right target");
                                held = next;
                                Check(!SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 1, &next),
                                      "right flick with nothing further right keeps the current target");
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 2, &next) &&
                                      next.owner == (uintptr_t)copB,
                                      "left flick walks back through the same order");
                                held = next;
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 2, &next) &&
                                      next.owner == (uintptr_t)copA, "left flick reaches the leftmost target");
                                *(Vector3 *)(copA + 0x10u) = (Vector3){-8, -2, 0};
                                *(Vector3 *)(copB + 0x10u) = (Vector3){-8, 0, 0};
                                *(Vector3 *)(copC + 0x10u) = (Vector3){-8, 2, 0};
                                view.aimDirection = (Vector3){-1, 0, 0};
                                held = (DriveByTarget){(uintptr_t)copA, 3, 0, {0, 0, 0}};
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 3, &next) &&
                                      next.owner == (uintptr_t)copB, "up flick selects the next target above");
                                held = next;
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 3, &next) &&
                                      next.owner == (uintptr_t)copC, "second up flick selects the highest target");
                                held = next;
                                Check(SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 4, &next) &&
                                      next.owner == (uintptr_t)copB, "down flick selects the next target below");
                                held = (DriveByTarget){(uintptr_t)copA, 3, 0, {0, 0, 0}};
                                Check(!SwitchDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &held, 4, &next),
                                      "down flick with nothing lower keeps the current target");
                                g_driveTarget = held;
                                g_driveSwitch = 3;
                                SeedGains();
                                RunAim(&mouseX, &mouseY, (uintptr_t)mission, (uintptr_t)player, view.cameraPosition,
                                       view.aimDirection, atan2f(view.aimDirection.x, view.aimDirection.z),
                                       0, 0, 0, 40000);
                                Check(g_driveTarget.owner == (uintptr_t)copB && g_target == (uintptr_t)copB &&
                                      g_driveSwitch == 0, "aim controller applies a requested target switch once");
                                ReleaseAim();
                                *(uintptr_t *)(testImage + CROWD_LIST_RVA) = savedBegin;
                                *(uintptr_t *)(testImage + CROWD_LIST_RVA + 4u) = savedEnd;
                            }
                            *(uintptr_t *)(mission + WORLD_LIST_BEGIN_OFFSET) = (uintptr_t)targets;
                            *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(targets + 3);
                            *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = POLICE_GROUP;
                            *(float *)(ownCar + 0x688u) = 5.0f;
                            memset(g_wheelHits, 0, sizeof(g_wheelHits));
                        }
                        *(uintptr_t *)(policeCar + CAR_WHEELS_OFFSET) = (uintptr_t)wheels;
                        *(uint32_t *)(policeCar + CAR_WHEEL_COUNT_OFFSET) = 1;
                        *(uint32_t *)(wheel + WHEEL_FLAGS_OFFSET) = 0;
                        policeCar[CAR_POLICE_OFFSET] = 0;
                      }
                    *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
                    *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
                    g_hitDamage = NULL;
                    g_vehicleArcade = 0;
                  }
                  g_held = 0;
                  g_shot = NULL;
                  testDriveByBlocked = 0;
                }
          *(uint32_t *)(player + 0xACu) = 1;
          Check(!ReadDriveByView((uintptr_t)player, &view), "unsupported passenger seat does not use driver-left aiming");
          *(uint32_t *)(player + 0xACu) = 0;
          *(uintptr_t *)(player + 0x98u) = 0;
        *(uintptr_t *)(player + 0x564u) = 0;
        *(uintptr_t *)(mission + 0x10u) = 0;
          *(uintptr_t *)(mission + WORLD_LIST_BEGIN_OFFSET) = (uintptr_t)actors;
          *(uintptr_t *)(mission + WORLD_LIST_END_OFFSET) = (uintptr_t)(actors + 2);
          *(uintptr_t *)(testImage + CROWD_LIST_RVA) = 0;
          *(uintptr_t *)(testImage + CROWD_LIST_RVA + 4u) = 0;
          g_lineTest = TestLine;
          *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){0, 0, 10};
        }
    game[0x40] = 0;
    Check(!GetWorld(&worldAddress, &playerAddress), "unloaded world rejected");
        {
          float healthBefore = *(float *)(npc + 0x644u);
          *(float *)(npc + 0x660u) = 0.2f;
          *(float *)(npc + 0x67Cu) = 0.5f;
          *(float *)(npc + 0xECCu) = -0.1f;
          *(float *)(npc + 0xEC4u) = -0.2f;
          ConfigureEnemyPersonality((uintptr_t)npc);
          Check(*(float *)(npc + 0x660u) + *(float *)(npc + 0xECCu) == 1.0f &&
              *(float *)(npc + 0x67Cu) + *(float *)(npc + 0xEC4u) == 1.0f,
              "custom enemy has high effective aggression and morale");
          Check(*(float *)(npc + 0x644u) == healthBefore && *(float *)(player + 0x660u) == 0.0f,
              "enemy personality does not heal or change another actor");
          Check(EnemyWasHurt(75.0f, 100.0f), "nonfatal damage triggers retaliation without visual contact");
          Check(!EnemyWasHurt(0.0f, 75.0f), "fatal damage cannot restart combat or revive enemy");
          Check(!EnemyWasHurt(75.0f, 75.0f) && !EnemyWasHurt(100.0f, 75.0f),
              "unchanged or recovered health does not repeatedly force combat");
        }
        {
          void *sharedStorage[40] = {0};
          void **sharedVtable = sharedStorage + 1;
          sharedStorage[0] = (void *)0x12345678u;
          sharedVtable[13] = (void *)TestEntityTick;
          *(void ***)npc = sharedVtable;
          g_originalEntityTick = TestEntityTick;
            Check(BindEnemyInstanceAi((uintptr_t)npc) && *(void ***)npc == g_enemyVtable.methods &&
              sharedVtable[13] == (void *)TestEntityTick,
              "enemy AI hook changes only its instance, not shared NPC vtable");
            Check((*(void ***)npc)[-1] == sharedStorage[0] &&
                memcmp(g_enemyVtable.methods, sharedVtable, 13 * sizeof(void *)) == 0 &&
                memcmp(g_enemyVtable.methods + 14, sharedVtable + 14, 25 * sizeof(void *)) == 0,
                "instance table preserves prefix and every non-AI native method");
            TestNativeJump(0x09D840u, (void *)TestStopAnimation);
            TestNativeJump(0x1C9010u, (void *)TestSuspendSwitcher);
            testImage[0x0107D0u] = 0xC3;
            g_tutorialFreeride = 1;
            g_tutorialEnemy = (uintptr_t)npc;
            g_enemyAlerted = 0;
            g_enemyHealth = 100.0f;
            *(float *)(npc + 0x644u) = 75.0f;
              ((HostEntityTickFn)(*(void ***)npc)[13])(npc, NULL, 16);
            Check(testStopAnimations == 1 && testStopActor == npc && testSuspendActor == npc &&
                testSuspendFlag == 0 && g_enemyAlerted,
                "actual AI callback wakes a hurt smoking enemy without seeing the player");
            Check(*(uintptr_t *)(npc + 0x1280u) == 0 && *(float *)(npc + 0x644u) == 75.0f,
                "damage alert leaves target selection and health to the engine");
            TutorialEnemyTick(player, NULL, 16);
            Check(testTickActor == player && testStopAnimations == 1,
                "unrelated actor receives original AI and no custom state changes");
            *(float *)(npc + 0x644u) = 0.0f;
            TutorialEnemyTick(npc, NULL, 16);
            Check(testEntityTicks == 3 && testStopAnimations == 1 && *(float *)(npc + 0x644u) == 0.0f,
                "dead enemy keeps native update without restarting its state machine");
            g_tutorialEnemy = 0;
            g_enemyHealth = 0.0f;
          g_originalEntityTick = NULL;
          g_pedestrianMegaPanic = TestMegaPanic;
          g_tutorialFreeride = 1;
          TutorialPedestrianPanic(NULL, NULL, 3);
          Check(testMegaPanic == 0,
              "tutorial skips collapse without forcing a different crowd state");
          g_tutorialFreeride = 0;
          TutorialPedestrianPanic(NULL, NULL, 2);
          Check(testMegaPanic == 1 && testMegaSeverity == 2,
              "other modes retain native panic severity");
        }
    {
        static const BYTE panicCaller[] = {
            0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x44, 0x24, 0x08, 0x50,
            0xE8, 0x2F, 0x5C, 0, 0, 0xC2, 0x08, 0
        };
        static const BYTE secondCall[] = {0xE8, 0xBA, 0x3A, 0, 0};
        BYTE pedestrian[0x200] = {0};
        void (__stdcall *panicA)(void *, int) = (void (__stdcall *)(void *, int))(testImage + 0x0B8373u);
        void (__stdcall *panicB)(void *, int) = (void (__stdcall *)(void *, int))(testImage + 0x0BA4E8u);
        memcpy(testImage + 0x0B8373u, panicCaller, sizeof(panicCaller));
        memcpy(testImage + 0x0BA4E8u, panicCaller, sizeof(panicCaller));
        memcpy(testImage + 0x0BA4F1u, secondCall, sizeof(secondCall));
        TestNativeJump(0x0BDFB0u, (void *)TestMegaPanic);
        InstallPedestrianPanicHook();
        testMegaPanic = 0;
        pedestrian[0x19C] = 1;
        g_tutorialFreeride = 1;
        panicA(pedestrian, 1);
        panicB(pedestrian, 3);
        Check(testMegaPanic == 0 && pedestrian[0x19C] == 1 && pedestrian[0x1A4] == 0,
              "both patched native panic callers retain stack balance and crowd movement state");
        g_tutorialFreeride = 0;
        panicA(pedestrian, 2);
        Check(testMegaPanic == 1 && testMegaSeverity == 2,
              "patched native panic caller forwards severity outside sandbox");
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
          InstallSandboxHooks(0);
          Check(memcmp(testImage + 0x176AA7u, menu, sizeof(menu)) == 0,
              "disabled sandbox leaves the native Tutorial menu untouched");
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