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
static int testDriveByBlocked;

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
          *(uint32_t *)(ownCar + ENTITY_KIND_OFFSET) = CAR_KIND;
          *(float *)(ownCar + 0x688u) = 5.0f;
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
          g_lineTest = TestDriveByLine;
          Check(GetActiveWorld(&worldAddress, &playerAddress) && !GetWorld(&worldAddress, &playerAddress),
              "driver world remains accessible while crouch stays disabled");
          Check(ReadDriveByView((uintptr_t)player, &view) && view.aimDirection.x == -1.0f,
              "driver view follows native active camera rather than stale shot target");
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) &&
              selected.owner == (uintptr_t)policeCar && selected.kind == 1,
              "reachable police wheel takes priority over nearer foot officer");
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
            *(float *)(ownCar + 0x688u) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "stopped car prioritizes a living foot officer over police wheels");
            *(float *)(npc + ENTITY_HEALTH_OFFSET) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "dead foot officer is ignored and stopped car falls back to wheels");
            *(float *)(npc + ENTITY_HEALTH_OFFSET) = 100.0f;
            npc[0x5Eu] = 1;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "death-processed officer cannot regain priority from stale positive health");
            npc[0x5Eu] = 0;
            *(float *)(ownCar + 0x688u) = -5.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "reversing car retains moving wheel priority");
            *(float *)(ownCar + 0x688u) = 5.0f;
            view.aimDirection = (Vector3){0, 0, 1};
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "reachable police wheel is acquired outside the old 20-degree camera cone");
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
          testDriveByBlocked = 2;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
              "collision at wheel endpoint is accepted rather than treated as cover");
          testDriveByBlocked = 5;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
              "visible rubber surface just before the tyre aim point is accepted");
          testDriveByBlocked = 3;
          Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "cover before the tyre band no longer passes a full-radius tolerance");
          testDriveByBlocked = 0;
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
          policeCar[CAR_POLICE_OFFSET] = 0;
            testDriveByBlocked = 3;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "foot officer body surface is not mistaken for cover before the chest aim point");
            testDriveByBlocked = 0;
            view.aimDirection = (Vector3){0, 0, 1};
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                "nearby left officer can be acquired outside the wheel camera cone");
                view.cameraPosition.z = -5.0f;
                testDriveByBlocked = 4;
                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                    "own car obscuring chase camera does not block a clear gun-to-officer shot");
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
                Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 2,
                    "reachable distant foot officer is acquired outside the old camera cone");
            *(Vector3 *)(npc + ENTITY_POSITION_OFFSET) = (Vector3){-5, 0, 0};
            view.aimDirection = (Vector3){-1, 0, 0};
          *(uint32_t *)(npc + ENTITY_TYPE_GROUP_OFFSET) = MISSION_ENEMY_GROUP;
          Check(!FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected),
              "civilian cars and non-police actors are excluded from drive-by");
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
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 3,
                "stopped car prefers living ambient patrol officer over tyres");
            *(float *)(ambientCop + 0x15Cu) = 0.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "dead active ambient officer does not block tyre fallback");
            *(float *)(ambientCop + 0x15Cu) = -1.0f;
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 1,
                "terminal negative ambient damage state cannot be targeted");
            *(float *)(ambientCop + 0x15Cu) = 1.0f;
            policeCar[CAR_POLICE_OFFSET] = 0;
            *(float *)(ownCar + 0x688u) = 5.0f;
            view.aimDirection = (Vector3){0, 0, 1};
            Check(FindDriveByTarget((uintptr_t)mission, (uintptr_t)player, &view, &selected) && selected.kind == 3,
                "nearby ambient patrol officer also uses the full reachable left sector");
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