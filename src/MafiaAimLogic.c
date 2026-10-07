#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_M_IX86)
#error MafiaAimLogic must be built for Win32/x86.
#endif

/* Game.exe is loaded at its preferred base, so RVAs are used for its globals. */
#define WORLD_POINTER_RVA        0x25115Cu
#define MISSION_GAME_OFFSET      0x24u
#define WORLD_PLAYER_OFFSET      0xE4u
#define WORLD_LIST_BEGIN_OFFSET  0x38u
#define WORLD_LIST_END_OFFSET    0x3Cu
#define WORLD_CAMERA_OFFSET      0x50u
#define ENTITY_POSITION_OFFSET   0x24u
#define ENTITY_HEALTH_OFFSET     0x644u
#define EXPERIMENTAL_STANCE_BYTE_OFFSET 0x1E4u
#define SHOT_SKELETON_OFFSET     0x2DCu
#define SHOT_SKELETON_SIZE       0x130u
#define SHOT_SKELETON_JOINT_COUNT 16u
#define SHOT_SKELETON_BONES_OFFSET 0x40u
#define SHOT_SKELETON_BONE_COUNT 10u
#define SHOT_SKELETON_BONE_STRIDE 0x18u
#define SHOT_SKELETON_BODY_PART_OFFSET 0x14u
#define SHOT_SKELETON_HEAD_PART  6u
#define FRAME_WORLD_POSITION_OFFSET 0x40u
#define FRAME_WORLD_FORWARD_OFFSET 0x30u
#define FRAME_FLAGS_OFFSET       0xACu
#define FRAME_WORLD_MATRIX_VALID 0x20u
#define ENTITY_KIND_OFFSET       0x10u
#define ENTITY_TYPE_GROUP_OFFSET 0xF4Cu
#define SCRIPTABLE_NPC_KIND      0x1Bu
#define MISSION_ENEMY_GROUP      4u
#define CAMERA_FORWARD_OFFSET    0x30u
#define CAMERA_POSITION_OFFSET   0x40u
#define COLLISION_LINE_WRAPPER_RVA 0x190A30u
#define COLLISION_OBJECT_RVA      0x27A588u
#define GAMEPAD_B_BUTTON       0x2000u
#define CAR_KIND               4u
#define CAR_POLICE_OFFSET      0x2044u
#define CAR_WHEEL_COUNT_OFFSET 0x5B0u
#define CAR_WHEELS_OFFSET      0xD24u
#define WHEEL_HUB_OFFSET       0x1Cu
#define WHEEL_FLAGS_OFFSET     0x120u
#define WHEEL_UNUSABLE_FLAG    0x40000000u
#define POLICE_GROUP           2u
#define CROWD_LIST_RVA         0x2560C4u

#define MAX_ENTITIES        512u
#define MAX_TARGET_DISTANCE 80.0f
#define DRIVE_VIEW_HALF_WIDTH  0.85f
#define DRIVE_VIEW_HALF_HEIGHT 0.5f
#define MIN_FORWARD_DOT     0.985f /* cos(10 degrees) */
#define ENEMY_PRIORITY_FORWARD_DOT 0.9063f /* cos(25 degrees) */
#define SWITCH_MIN_FORWARD_DOT 0.5f /* switching may reach people up to 60 degrees off the crosshair */
#define SWITCH_MAX_ANGLE    1.2f    /* radians from the current target */
#define SWITCH_MIN_DELTA    0.03f
#define STICK_FLICK_DOMINANCE 1.3f
#define TARGET_SWITCH_COOLDOWN_MS 350
static float g_aimHeight = 0.95f; /* metres above the ped's origin; low enough to hit a crouching target */
static int   g_aimZone;
#define MIN_STEP_MS         5
#define DEADZONE_RAD        0.004f
#define MAX_STEP_COUNTS     40
#define K_INIT              0.18f
#define K_MIN               0.06f
#define K_MAX               0.24f
#define AIM_BRAKE_ANGLE     0.07f
#define AIM_BRAKE_FLOOR     0.55f
#define LOS_CHECK_INTERVAL_MS 100
#define LOS_BLOCKED_DROP_CHECKS 5
#define LOS_TARGET_MARGIN   0.6f
#define PI_F                3.14159265f

static float AimPointHeight(void)
{
    float height = g_aimHeight + (g_aimZone > 0 ? 0.75f : (g_aimZone < 0 ? -0.25f : 0.0f));
    if (height < 0.4f) height = 0.4f;
    if (height > 1.8f) height = 1.8f;
    return height;
}

/* Mouse sensitivity measured on this game: radians of camera turn per mouse count. */
#define SEED_GAIN_X   0.002039
#define SEED_GAIN_Y  -0.001105

typedef struct Vector3 { float x, y, z; } Vector3;
typedef int (__fastcall *CollisionLineTestFn)(void *collision, void *unused,
                                             const Vector3 *from, const Vector3 *delta,
                                             Vector3 *hitPosition, Vector3 *hitNormal,
                                             int ignoreObject, uint32_t mask);
typedef void (__fastcall *UpdateWorldMatrixFn)(void *frame, void *unused);

static int g_losEnabled = 1;
static int g_prioritizeEnemies = 1;
static float g_targetConeDot = MIN_FORWARD_DOT;
static float g_enemyTargetConeDot = ENEMY_PRIORITY_FORWARD_DOT;
static int g_crouchAwareHeadAim = 1;
static int g_animatedHeadAim = 1;
static float g_animatedHeadForward = 0.08f;
static int g_crouchToggleEnabled = 1;
static int g_vehicleAimEnabled = 1;
static int g_vehicleArcade = 1;
static int g_vehicleOneShot = 1;
static int g_vehicleFreeCamera = 1;
static int g_cheatHealth;
static float g_cheatAcceleration = 1.0f;
static int g_crouchDesired = -1;
static uintptr_t g_crouchPlayer;
static int g_lineTestState;
static int g_lineTestLogCount;
static CollisionLineTestFn g_lineTest;

static HANDLE g_log = INVALID_HANDLE_VALUE;
static LONG   g_logLines;

static void Log(const char *format, ...)
{
    char line[320];
    DWORD written;
    int length;
    va_list args;
    if (g_logLines > 4000)
        return;
    if (g_log == INVALID_HANDLE_VALUE)
    {
        char path[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, path);
        if (!n || n > MAX_PATH - 32)
            return;
        lstrcatA(path, "MafiaAimLogic.log");
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
    ++g_logLines;
}

static LONGLONG NowMs(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000 / frequency.QuadPart;
}

/* ---- safe memory access ------------------------------------------------ */

static int IsReadable(const void *address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start = (uintptr_t)address;
    uintptr_t end = start + size;
    DWORD protection;
    if (!address || size == 0 || end < start || start < 0x10000u || start > 0x7FFF0000u)
        return 0;
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT)
        return 0;
    protection = info.Protect & 0xFFu;
    if (protection == PAGE_NOACCESS || protection == PAGE_EXECUTE || (info.Protect & PAGE_GUARD))
        return 0;
    return end <= (uintptr_t)info.BaseAddress + info.RegionSize;
}

static uint32_t ReadU32(uintptr_t address)
{
    return IsReadable((const void *)address, 4) ? *(volatile uint32_t *)address : 0;
}

static int IsFinite(float value)
{
    return value == value && value < 1.0e20f && value > -1.0e20f;
}

static int ReadVector(uintptr_t address, Vector3 *out)
{
    if (!IsReadable((const void *)address, sizeof(Vector3)))
        return 0;
    out->x = *(volatile float *)(address);
    out->y = *(volatile float *)(address + 4u);
    out->z = *(volatile float *)(address + 8u);
    return IsFinite(out->x) && IsFinite(out->y) && IsFinite(out->z);
}

/* ---- vector helpers ---------------------------------------------------- */

static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vector3 Subtract(Vector3 a, Vector3 b)
{
    Vector3 r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}

static int Normalize(Vector3 *v)
{
    float lengthSquared = Dot(*v, *v);
    float inverse;
    if (!IsFinite(lengthSquared) || lengthSquared < 0.0001f)
        return 0;
    inverse = 1.0f / sqrtf(lengthSquared);
    v->x *= inverse; v->y *= inverse; v->z *= inverse;
    return 1;
}

static float WrapAngle(float angle)
{
    while (angle > PI_F) angle -= 2.0f * PI_F;
    while (angle < -PI_F) angle += 2.0f * PI_F;
    return angle;
}

static float ClampUnit(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }

static int BindLineTest(void)
{
    static const unsigned char signature[5] = {
        0xE9, 0x6B, 0x2E, 0xFF, 0xFF
    };
    uintptr_t base;
    const void *address;
    if (g_lineTestState != 0)
        return g_lineTestState > 0;
    base = (uintptr_t)GetModuleHandleA(NULL);
    address = (const void *)(base + COLLISION_LINE_WRAPPER_RVA);
    if (!base || !IsReadable(address, sizeof(signature)) ||
        memcmp(address, signature, sizeof(signature)) != 0)
    {
        g_lineTestState = -1;
        Log("LOS disabled: Game.exe line-test wrapper signature did not match");
        return 0;
    }
    g_lineTest = (CollisionLineTestFn)address;
    g_lineTestState = 1;
    Log("LOS bound to Game.exe wrapper RVA 0x%08lX", (unsigned long)COLLISION_LINE_WRAPPER_RVA);
    return 1;
}

static int HasLineOfSight(Vector3 from, Vector3 target)
{
    Vector3 delta = Subtract(target, from);
    Vector3 hitPosition = {0.0f, 0.0f, 0.0f};
    Vector3 hitNormal = {0.0f, 0.0f, 0.0f};
    float lineDistance, hitDistance;
    int hit, blocked;
    if (!g_losEnabled || !BindLineTest())
        return 1;
    lineDistance = sqrtf(Dot(delta, delta));
    if (!IsFinite(lineDistance) || lineDistance < 0.1f)
        return 1;
    __try
    {
        hit = g_lineTest((void *)((uintptr_t)GetModuleHandleA(NULL) + COLLISION_OBJECT_RVA),
                NULL, &from, &delta, &hitPosition, &hitNormal, -1, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_lineTest = NULL;
        g_lineTestState = -1;
        Log("LOS call raised an exception; line-of-sight filtering disabled");
        return 1;
    }
    if (!hit || !IsFinite(hitPosition.x) || !IsFinite(hitPosition.y) || !IsFinite(hitPosition.z))
        return 1;
    hitDistance = sqrtf(Dot(Subtract(hitPosition, from), Subtract(hitPosition, from)));
    blocked = IsFinite(hitDistance) && hitDistance + LOS_TARGET_MARGIN < lineDistance;
    if (g_lineTestLogCount < 40)
    {
        ++g_lineTestLogCount;
        Log("LOS hit=%d distance=%.2f hit_distance=%.2f result=%s", hit, lineDistance,
            hitDistance, blocked ? "blocked" : "clear");
    }
    return !blocked;
}

/* ---- game state -------------------------------------------------------- */

static int SupportedGame(void)
{
    static int state;
    static const BYTE playerAccessor[] = {0x8B, 0x81, 0xE4, 0x00, 0x00, 0x00, 0xC3};
    static const BYTE crouchEntry[] = {
        0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
        0xF8, 0x06, 0x00, 0x00, 0x85, 0xC0
    };
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!state)
    {
        state = -1;
        if (IsReadable((const void *)(base + 0x025530u), sizeof(playerAccessor)) &&
            IsReadable((const void *)(base + 0x09FDF0u), sizeof(crouchEntry)) &&
            memcmp((const void *)(base + 0x025530u), playerAccessor, sizeof(playerAccessor)) == 0 &&
            memcmp((const void *)(base + 0x09FDF0u), crouchEntry, sizeof(crouchEntry)) == 0)
            state = 1;
        Log("Mafia 1.0 memory profile %s", state > 0 ? "validated" : "rejected");
    }
    return state > 0;
}

static int GetActiveWorld(uintptr_t *world, uintptr_t *player)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t w;
    uintptr_t game, p;
    if (!SupportedGame())
        return 0;
    w = ReadU32(base + WORLD_POINTER_RVA);
    if (w < 0x10000u)
        return 0;
    game = ReadU32(w + MISSION_GAME_OFFSET);
    if (!game || !IsReadable((const void *)(game + 0x40u), 1) ||
        !*(volatile BYTE *)(game + 0x40u))
        return 0;
    p = ReadU32(game + WORLD_PLAYER_OFFSET);
    if (p < 0x10000u || ReadU32(p + ENTITY_KIND_OFFSET) != 2u)
        return 0;
    *world = w;
    *player = p;
    return 1;
}

static int GetWorld(uintptr_t *world, uintptr_t *player)
{
    return GetActiveWorld(world, player) &&
        ReadU32(*player + 0x98u) == 0 && ReadU32(*player + 0x9Cu) == 0;
}

static int DriveBySearchSector(Vector3 origin, Vector3 forward, Vector3 target)
{
    Vector3 direction = Subtract(target, origin);
    forward.y = direction.y = 0.0f;
    if (!Normalize(&forward) || !Normalize(&direction))
        return 0;
    return direction.z * forward.x - direction.x * forward.z >= 0.0f;
}

static int DriveByReachable(Vector3 origin, Vector3 forward, Vector3 target)
{
    Vector3 direction = Subtract(target, origin);
    float vertical, left, ahead;
    forward.y = 0.0f;
    if (!Normalize(&forward) || !Normalize(&direction))
        return 0;
    vertical = direction.y;
    direction.y = 0.0f;
    if (!Normalize(&direction) || vertical < -0.5f || vertical > 0.4226f)
        return 0;
    left = direction.z * forward.x - direction.x * forward.z;
    ahead = Dot(direction, forward);
    return left >= 0.6428f && fabsf(ahead) <= 0.7661f;
}

static int ReadCamera(Vector3 *position, Vector3 *forward)
{
    uintptr_t mission = ReadU32((uintptr_t)GetModuleHandleA(NULL) + WORLD_POINTER_RVA);
    uintptr_t game = ReadU32(mission + MISSION_GAME_OFFSET);
    uintptr_t camera = ReadU32(game + WORLD_CAMERA_OFFSET);
    if (camera < 0x10000u)
        return 0;
    if (!ReadVector(camera + CAMERA_FORWARD_OFFSET, forward) ||
        !ReadVector(camera + CAMERA_POSITION_OFFSET, position))
        return 0;
    return Normalize(forward);
}

static int IsLivePed(uintptr_t object, Vector3 *position)
{
    uint32_t kind;
    float health;
    if (object < 0x10000u || !IsReadable((const void *)object, ENTITY_HEALTH_OFFSET + 4u))
        return 0;
    kind = ReadU32(object + ENTITY_KIND_OFFSET);
    if (kind != SCRIPTABLE_NPC_KIND && kind != 3u && kind != 2u)
        return 0;
    health = *(volatile float *)(object + ENTITY_HEALTH_OFFSET);
    if (!IsFinite(health) || health < 0.1f || health > 1000.0f)
        return 0;
    return ReadVector(object + ENTITY_POSITION_OFFSET, position);
}

static int IsPriorityEnemyPed(uintptr_t object)
{
    if (!g_prioritizeEnemies || ReadU32(object + ENTITY_KIND_OFFSET) != SCRIPTABLE_NPC_KIND)
        return 0;
    return ReadU32(object + ENTITY_TYPE_GROUP_OFFSET) == MISSION_ENEMY_GROUP;
}

static uintptr_t g_stanceProbeTarget;
static int g_stanceProbeValue = -1;

static int ReadTargetStanceByte(uintptr_t target)
{
    if (!target || !IsReadable((const void *)(target + EXPERIMENTAL_STANCE_BYTE_OFFSET), 1))
        return -1;
    return *(volatile BYTE *)(target + EXPERIMENTAL_STANCE_BYTE_OFFSET);
}

static void ProbeStanceByte(uintptr_t target)
{
    int value = ReadTargetStanceByte(target);
    uintptr_t kind, group;
    if (!target)
        return;
    if (target == g_stanceProbeTarget && value == g_stanceProbeValue)
        return;
    kind = ReadU32(target + ENTITY_KIND_OFFSET);
    group = ReadU32(target + ENTITY_TYPE_GROUP_OFFSET);
    Log("stance_probe target=0x%08lX byte_1e4=%d kind=%lu group=%lu",
        (unsigned long)target, value, (unsigned long)kind, (unsigned long)group);
    g_stanceProbeTarget = target;
    g_stanceProbeValue = value;
}

static float AimPointHeightForTarget(uintptr_t target)
{
    float height = AimPointHeight();
    if (g_crouchAwareHeadAim && g_aimZone > 0 && ReadTargetStanceByte(target) > 0)
        height = g_aimHeight + 0.30f;
    if (height < 0.4f) height = 0.4f;
    if (height > 1.8f) height = 1.8f;
    return height;
}

static int ReadFrameWorldPosition(uintptr_t frame, Vector3 *position)
{
    static UpdateWorldMatrixFn updateWorldMatrix;
    static int updateWorldMatrixChecked;
    uintptr_t flagsAddress = frame + FRAME_FLAGS_OFFSET;
    if (!frame || !IsReadable((const void *)flagsAddress, 4))
        return 0;
    if ((ReadU32(flagsAddress) & FRAME_WORLD_MATRIX_VALID) == 0)
    {
        if (!updateWorldMatrixChecked)
        {
            HMODULE ls3df = GetModuleHandleA("LS3DF.dll");
            if (ls3df)
                updateWorldMatrix = (UpdateWorldMatrixFn)GetProcAddress(
                    ls3df, "?UpdateWMatrixProc@I3D_frame@@AAEXXZ");
            updateWorldMatrixChecked = 1;
        }
        if (!updateWorldMatrix)
            return 0;
        updateWorldMatrix((void *)frame, NULL);
        if ((ReadU32(flagsAddress) & FRAME_WORLD_MATRIX_VALID) == 0)
            return 0;
    }
    return ReadVector(frame + FRAME_WORLD_POSITION_OFFSET, position);
}

typedef struct DriveByView
{
    uintptr_t car;
    Vector3 origin, forward, aimDirection, cameraPosition;
} DriveByView;

typedef struct DriveByTarget
{
    uintptr_t owner;
    int kind;
    uint32_t wheel;
    Vector3 point;
} DriveByTarget;

static DriveByTarget g_driveTarget;

static int ReadByte(uintptr_t address)
{
    return IsReadable((const void *)address, 1) ? *(volatile BYTE *)address : -1;
}

static int ReadDriveByView(uintptr_t player, DriveByView *view)
{
    uintptr_t car = ReadU32(player + 0x98u);
    uintptr_t frame = ReadU32(player + 0x564u);
    uintptr_t carFrame;
    uintptr_t mission = ReadU32((uintptr_t)GetModuleHandleA(NULL) + WORLD_POINTER_RVA);
    uintptr_t scene = ReadU32(mission + 0x10u);
    uintptr_t camera = ReadU32(scene + 0x17Cu);
    uint32_t weapon = ReadU32(player + 0x1E8u);
    if (!g_vehicleAimEnabled || ReadU32(car + ENTITY_KIND_OFFSET) != CAR_KIND ||
        ReadU32(player + 0xACu) != 0 || ReadU32(player + 0x9Cu) != 0 ||
        ReadByte(player + 0xADAu) != 0 || weapon < 1 || weapon > 3 ||
        ReadByte(car + 0x5Eu) != 0 || !ReadFrameWorldPosition(frame, &view->origin))
        return 0;
    carFrame = ReadU32(car + 0x68u);
    if ((ReadU32(carFrame + FRAME_FLAGS_OFFSET) & FRAME_WORLD_MATRIX_VALID) == 0 ||
        !ReadVector(carFrame + FRAME_WORLD_FORWARD_OFFSET, &view->forward) ||
        !Normalize(&view->forward) ||
        (ReadU32(camera + FRAME_FLAGS_OFFSET) & FRAME_WORLD_MATRIX_VALID) == 0 ||
        !ReadVector(camera + FRAME_WORLD_POSITION_OFFSET, &view->cameraPosition) ||
        !ReadVector(camera + FRAME_WORLD_FORWARD_OFFSET, &view->aimDirection) ||
        !Normalize(&view->aimDirection))
        return 0;
    view->car = car;
    return 1;
}

static int DriveByLineClear(Vector3 origin, Vector3 point, float targetMargin)
{
    Vector3 delta = Subtract(point, origin);
    Vector3 hitPoint = {0}, normal = {0};
    float distance = sqrtf(Dot(delta, delta));
    int hit;
    if (!IsFinite(distance) || distance < 0.25f || !BindLineTest() || !g_lineTest)
        return 0;
    __try
    {
        hit = g_lineTest((void *)((uintptr_t)GetModuleHandleA(NULL) + COLLISION_OBJECT_RVA),
                        NULL, &origin, &delta, &hitPoint, &normal, -1, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_lineTestState = -1;
        g_lineTest = NULL;
        Log("drive-by LOS failed: vehicle lock disabled");
        return 0;
    }
    if (!hit)
        return 1;
    if (!IsFinite(hitPoint.x) || !IsFinite(hitPoint.y) || !IsFinite(hitPoint.z))
        return 0;
    delta = Subtract(hitPoint, origin);
    return sqrtf(Dot(delta, delta)) + targetMargin >= distance;
}

static int TyreAimPoint(Vector3 hub, Vector3 up, float radius, Vector3 *point)
{
    float offset;
    if (!IsFinite(radius) || radius < 0.1f || radius > 1.0f || !Normalize(&up))
        return 0;
    offset = radius * 0.85f;
    point->x = hub.x + up.x * offset;
    point->y = hub.y + up.y * offset;
    point->z = hub.z + up.z * offset;
    return IsFinite(point->x) && IsFinite(point->y) && IsFinite(point->z);
}

#define TRAFFIC_MANAGER_KIND     0xCu
#define TRAFFIC_ELEMENTS_OFFSET  0x74u
#define TRAFFIC_COUNT_OFFSET     0x220u
#define MAFIA_CREWS_BEGIN_RVA    0x256170u
#define MAFIA_CREWS_END_RVA      0x256174u
#define CAR_SEATS_BEGIN_OFFSET   0x210Cu
#define CAR_SEATS_END_OFFSET     0x2110u
#define MAX_CANDIDATE_CARS       192u

static int g_driveSwitch;

static int HostileGroup(uint32_t group)
{
    return group == POLICE_GROUP || group == MISSION_ENEMY_GROUP;
}

static int HasHostileOccupant(uintptr_t car)
{
    uintptr_t seats = ReadU32(car + CAR_SEATS_BEGIN_OFFSET);
    uintptr_t end = ReadU32(car + CAR_SEATS_END_OFFSET);
    uintptr_t count, seat;
    if (seats < 0x10000u || end < seats || (end - seats) % 0xA0u)
        return 0;
    count = (end - seats) / 0xA0u;
    if (!count || count > 8u || !IsReadable((const void *)seats, count * 0xA0u))
        return 0;
    for (seat = 0; seat < count; ++seat)
    {
        uintptr_t actor = ReadU32(seats + seat * 0xA0u + 0x10u);
        if (actor >= 0x10000u && ReadU32(actor + ENTITY_KIND_OFFSET) == SCRIPTABLE_NPC_KIND &&
            HostileGroup(ReadU32(actor + ENTITY_TYPE_GROUP_OFFSET)) &&
            ReadByte(actor + 0x5Du) == 1 && ReadByte(actor + 0x5Eu) == 0)
            return 1;
    }
    return 0;
}

static int InMafiaCrew(uintptr_t car)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t first = ReadU32(base + MAFIA_CREWS_BEGIN_RVA);
    uintptr_t end = ReadU32(base + MAFIA_CREWS_END_RVA);
    uintptr_t record;
    if (first < 0x10000u || end < first || (end - first) % 12u || end - first > 12u * 64u ||
        !IsReadable((const void *)first, end - first))
        return 0;
    for (record = first; record < end; record += 12u)
        if (ReadU32(record) == car)
            return 1;
    return 0;
}

/* Police cars, cars crewed by hostile actors and spawned mafia crews are valid wheel targets. */
static int IsHostileCar(uintptr_t car)
{
    return ReadU32(car + ENTITY_KIND_OFFSET) == CAR_KIND && ReadByte(car + 0x5Eu) == 0 &&
        (ReadByte(car + CAR_POLICE_OFFSET) == 1 || HasHostileOccupant(car) || InMafiaCrew(car));
}

static int IsMovingCar(uintptr_t car)
{
    float speed;
    if (!IsReadable((const void *)(car + 0x688u), sizeof(float)))
        return 0;
    speed = *(volatile float *)(car + 0x688u);
    return IsFinite(speed) && fabsf(speed) > 1.0f;
}

/* Wheels count only while the car is driving, or when it still carries someone hostile. */
static int IsWheelWorthTargeting(uintptr_t car)
{
    return IsMovingCar(car) || HasHostileOccupant(car);
}

static int ReadPoliceWheel(uintptr_t car, uint32_t index, Vector3 *point, float *radius)
{
    uint32_t count = ReadU32(car + CAR_WHEEL_COUNT_OFFSET);
    uintptr_t wheels = ReadU32(car + CAR_WHEELS_OFFSET);
    uintptr_t wheel;
    Vector3 up;
    if (!IsHostileCar(car) || !IsWheelWorthTargeting(car) || !count || count > 8 || index >= count ||
        !IsReadable((void *)wheels, count * sizeof(uint32_t)))
        return 0;
    wheel = ReadU32(wheels + index * 4u);
    if (!wheel || !IsReadable((void *)wheel, WHEEL_FLAGS_OFFSET + 4u) ||
        (ReadU32(wheel + WHEEL_FLAGS_OFFSET) & WHEEL_UNUSABLE_FLAG) != 0 ||
        !ReadVector(wheel + WHEEL_HUB_OFFSET, point))
        return 0;
    *radius = *(volatile float *)(wheel + 0x10Cu);
    return ReadVector(car + 0xD40u, &up) && TyreAimPoint(*point, up, *radius, point);
}

/* True when the point lies inside the camera's visible window, so only what the player can see is lockable. */
static int DriveByOnScreen(const DriveByView *view, Vector3 point)
{
    Vector3 d = Subtract(point, view->cameraPosition);
    Vector3 right = {view->aimDirection.z, 0.0f, -view->aimDirection.x};
    Vector3 up;
    float forward, horizontal, vertical;
    if (!Normalize(&d) || !Normalize(&right))
        return 0;
    up = (Vector3){view->aimDirection.y * right.z - view->aimDirection.z * right.y,
                   view->aimDirection.z * right.x - view->aimDirection.x * right.z,
                   view->aimDirection.x * right.y - view->aimDirection.y * right.x};
    forward = Dot(d, view->aimDirection);
    if (forward < 0.1f)
        return 0;
    horizontal = atan2f(Dot(d, right), forward);
    vertical = atan2f(Dot(d, up), forward);
    return fabsf(horizontal) <= DRIVE_VIEW_HALF_WIDTH && fabsf(vertical) <= DRIVE_VIEW_HALF_HEIGHT;
}

/* requireVisible: arcade mode locks people and wheels of driving hostile cars even when hidden or behind the camera. */
static int DriveByCandidate(DriveByView *view, Vector3 point, float margin,
                            int requireVisible, float *score)
{
    Vector3 direction = Subtract(point, view->cameraPosition);
    float distanceSquared = Dot(direction, direction);
    float alignment;
    int checkVisibility = requireVisible || !g_vehicleArcade;
    if (distanceSquared < 0.0625f || distanceSquared > MAX_TARGET_DISTANCE * MAX_TARGET_DISTANCE ||
        (!g_vehicleArcade && (!DriveBySearchSector(view->origin, view->forward, point) ||
                             !DriveByReachable(view->origin, view->forward, point))) ||
        !Normalize(&direction) || (checkVisibility && !DriveByOnScreen(view, point)))
        return 0;
    alignment = Dot(direction, view->aimDirection);
    if ((checkVisibility && !DriveByLineClear(view->cameraPosition, point, margin)) ||
        (!g_vehicleArcade && !DriveByLineClear(view->origin, point, margin)))
        return 0;
    *score = (1.0f - alignment) * 10000.0f + distanceSquared;
    return 1;
}

static int ReadAnimatedHeadPoint(uintptr_t target, Vector3 *headPoint)
{
    uintptr_t skeleton = target + SHOT_SKELETON_OFFSET;
    uintptr_t boneIndex;
    if (!target || !IsReadable((const void *)skeleton, SHOT_SKELETON_SIZE))
        return 0;
    for (boneIndex = 0; boneIndex < SHOT_SKELETON_BONE_COUNT; ++boneIndex)
    {
        uintptr_t bone = skeleton + SHOT_SKELETON_BONES_OFFSET + boneIndex * SHOT_SKELETON_BONE_STRIDE;
        uintptr_t jointAIndex, jointBIndex, jointAFrame, jointBFrame;
        float start, end, along;
        Vector3 jointA, jointB, axis;
        if (ReadU32(bone + SHOT_SKELETON_BODY_PART_OFFSET) != SHOT_SKELETON_HEAD_PART)
            continue;
        jointAIndex = ReadU32(bone);
        jointBIndex = ReadU32(bone + 4u);
        if (jointAIndex >= SHOT_SKELETON_JOINT_COUNT || jointBIndex >= SHOT_SKELETON_JOINT_COUNT)
            continue;
        start = *(volatile float *)(bone + 8u);
        end = *(volatile float *)(bone + 0xCu);
        if (!IsFinite(start) || !IsFinite(end) || end <= start || start < -2.0f || end > 4.0f)
            continue;
        jointAFrame = ReadU32(skeleton + jointAIndex * 4u);
        jointBFrame = ReadU32(skeleton + jointBIndex * 4u);
        if (!ReadFrameWorldPosition(jointAFrame, &jointA) ||
            !ReadFrameWorldPosition(jointBFrame, &jointB))
            continue;
        axis = Subtract(jointB, jointA);
        if (Dot(axis, axis) < 0.0001f || Dot(axis, axis) > 9.0f)
            continue;
        along = (start + end) * 0.5f;
        headPoint->x = jointA.x + axis.x * along;
        headPoint->y = jointA.y + axis.y * along;
        headPoint->z = jointA.z + axis.z * along;
        if (g_animatedHeadForward > 0.0f)
        {
            Vector3 faceDirection;
            if (ReadVector(jointBFrame + FRAME_WORLD_FORWARD_OFFSET, &faceDirection))
            {
                faceDirection.y = 0.0f;
                if (Normalize(&faceDirection))
                {
                    headPoint->x += faceDirection.x * g_animatedHeadForward;
                    headPoint->z += faceDirection.z * g_animatedHeadForward;
                }
            }
        }
        if (IsFinite(headPoint->x) && IsFinite(headPoint->y) && IsFinite(headPoint->z))
            return 1;
    }
    return 0;
}

static void GetAimPointPosition(uintptr_t target, Vector3 origin, Vector3 *aimPoint)
{
    *aimPoint = origin;
    if (g_aimZone > 0 && g_animatedHeadAim && ReadAnimatedHeadPoint(target, aimPoint))
        return;
    aimPoint->y += AimPointHeightForTarget(target);
}

static int ListBounds(uintptr_t world, uintptr_t *begin, uintptr_t *count)
{
    static uintptr_t actors[MAX_ENTITIES * 2u];
    uintptr_t game = ReadU32(world + MISSION_GAME_OFFSET);
    uintptr_t headers[2] = {world + WORLD_LIST_BEGIN_OFFSET, game ? game + 0x124u : 0};
    uintptr_t list, total = 0;
    for (list = 0; list < 2u; ++list)
    {
        uintptr_t first = ReadU32(headers[list]);
        uintptr_t end = ReadU32(headers[list] + 4u);
        uintptr_t length, index;
        if (first < 0x10000u || end < first || ((end - first) & 3u))
            continue;
        length = (end - first) / 4u;
        if (length > MAX_ENTITIES || !length || !IsReadable((const void *)first, length * 4u))
            continue;
        for (index = 0; index < length; ++index)
        {
            uintptr_t actor = ReadU32(first + index * 4u);
            uintptr_t existing;
            if (!actor)
                continue;
            for (existing = 0; existing < total && actors[existing] != actor; ++existing)
                ;
            if (existing == total)
                actors[total++] = actor;
        }
    }
    *begin = (uintptr_t)actors;
    *count = total;
    return total != 0;
}

/* Adds one car to a bounded unique list. */
static void AddCandidateCar(uintptr_t *cars, uintptr_t *count, uintptr_t car)
{
    uintptr_t index;
    if (car < 0x10000u || *count >= MAX_CANDIDATE_CARS || ReadU32(car + ENTITY_KIND_OFFSET) != CAR_KIND)
        return;
    for (index = 0; index < *count; ++index)
        if (cars[index] == car)
            return;
    cars[(*count)++] = car;
}

/* Cars from the actor lists, the ambient-traffic manager's elements and spawned mafia crews. */
static uintptr_t CollectCars(uintptr_t world, uintptr_t *cars)
{
    uintptr_t begin, count, index, total = 0;
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t crews = ReadU32(base + MAFIA_CREWS_BEGIN_RVA), crewsEnd = ReadU32(base + MAFIA_CREWS_END_RVA);
    if (ListBounds(world, &begin, &count))
    {
        for (index = 0; index < count; ++index)
        {
            uintptr_t actor = ReadU32(begin + index * 4u);
            uint32_t kind = ReadU32(actor + ENTITY_KIND_OFFSET);
            if (kind == CAR_KIND)
                AddCandidateCar(cars, &total, actor);
            else if (kind == TRAFFIC_MANAGER_KIND)
            {
                uintptr_t elements = ReadU32(actor + TRAFFIC_ELEMENTS_OFFSET);
                uintptr_t elementCount = ReadU32(actor + TRAFFIC_COUNT_OFFSET), element;
                if (elements < 0x10000u || !elementCount || elementCount > 256u ||
                    !IsReadable((const void *)elements, elementCount * 4u))
                    continue;
                for (element = 0; element < elementCount; ++element)
                {
                    uintptr_t record = ReadU32(elements + element * 4u);
                    if (record >= 0x10000u && ReadByte(record) != 0)
                        AddCandidateCar(cars, &total, ReadU32(record + 0xCu));
                }
            }
        }
    }
    if (crews >= 0x10000u && crewsEnd >= crews && (crewsEnd - crews) % 12u == 0 &&
        crewsEnd - crews <= 12u * 64u && IsReadable((const void *)crews, crewsEnd - crews))
        for (index = crews; index < crewsEnd; index += 12u)
            AddCandidateCar(cars, &total, ReadU32(index));
    return total;
}

typedef struct DriveByEntry
{
    DriveByTarget target;
    float score;
    int preferred;
} DriveByEntry;
#define MAX_DRIVE_ENTRIES 160

static void AddDriveByEntry(DriveByEntry *entries, int *count, DriveByTarget target, float score)
{
    if (*count < MAX_DRIVE_ENTRIES)
    {
        entries[*count].target = target;
        entries[*count].preferred = 1;
        entries[(*count)++].score = score;
    }
}

/* Wheels, living officers and ambient patrol officers share one pool with equal priority. */
static int CollectDriveByTargets(uintptr_t world, uintptr_t player, DriveByView *view,
                                 DriveByEntry *entries)
{
    uintptr_t cars[MAX_CANDIDATE_CARS], carCount = CollectCars(world, cars), index;
    uintptr_t begin, count;
    int total = 0;
    for (index = 0; index < carCount; ++index)
    {
        uintptr_t car = cars[index];
        uint32_t wheel, wheels = ReadU32(car + CAR_WHEEL_COUNT_OFFSET);
        if (car == view->car || wheels > 8)
            continue;
        for (wheel = 0; wheel < wheels; ++wheel)
        {
            Vector3 point;
            float score, radius;
            if (ReadPoliceWheel(car, wheel, &point, &radius) &&
                DriveByCandidate(view, point, 0.12f, !IsMovingCar(car), &score))
                AddDriveByEntry(entries, &total, (DriveByTarget){car, 1, wheel, point}, score);
        }
    }
    if (ListBounds(world, &begin, &count))
    {
        for (index = 0; index < count; ++index)
        {
            uintptr_t actor = ReadU32(begin + index * 4u);
            Vector3 point;
            float score;
            if (actor == player || ReadU32(actor + ENTITY_KIND_OFFSET) != SCRIPTABLE_NPC_KIND ||
                !HostileGroup(ReadU32(actor + ENTITY_TYPE_GROUP_OFFSET)) ||
                ReadU32(actor + 0x98u) || ReadU32(actor + 0x9Cu) ||
                ReadByte(actor + 0x5Du) != 1 || ReadByte(actor + 0x5Eu) != 0 || !IsLivePed(actor, &point))
                continue;
            point.y += 1.25f;
            if (DriveByCandidate(view, point, 0.35f, 0, &score))
                AddDriveByEntry(entries, &total, (DriveByTarget){actor, 2, 0, point}, score);
        }
    }
    {
        uintptr_t header = (uintptr_t)GetModuleHandleA(NULL) + CROWD_LIST_RVA;
        uintptr_t first = ReadU32(header), end = ReadU32(header + 4u);
        if (first && end >= first && (end - first) % 4u == 0 &&
            end - first <= MAX_ENTITIES * 4u && IsReadable((void *)first, end - first))
        {
            for (index = first; index < end; index += 4u)
            {
                uintptr_t pedestrian = ReadU32(index);
                int category = ReadByte(pedestrian + 0x15Au);
                Vector3 point;
                float score, life;
                if (ReadByte(pedestrian) != 0 || ReadByte(pedestrian + 0xCu) != 1 ||
                    (category != 1 && category != 3) || !ReadVector(pedestrian + 0x10u, &point) ||
                    !IsReadable((const void *)(pedestrian + 0x15Cu), sizeof(float)))
                    continue;
                life = *(volatile float *)(pedestrian + 0x15Cu);
                if (!IsFinite(life) || life <= 0.0f)
                    continue;
                point.y += 1.25f;
                if (DriveByCandidate(view, point, 0.35f, 0, &score))
                    AddDriveByEntry(entries, &total, (DriveByTarget){pedestrian, 3, 0, point}, score);
            }
        }
    }
    {
        /* In a chase the tyres of moving cars come first; otherwise people on foot do. */
        int chase = 0;
        for (index = 0; index < (uintptr_t)total; ++index)
            if (entries[index].target.kind == 1 && IsMovingCar(entries[index].target.owner))
                chase = 1;
        for (index = 0; index < (uintptr_t)total; ++index)
            entries[index].preferred = entries[index].target.kind == 1 ?
                IsMovingCar(entries[index].target.owner) : !chase;
    }
    return total;
}

/* The held lock stays until it becomes invalid; a new lock is the target nearest the crosshair. */
static int FindDriveByTarget(uintptr_t world, uintptr_t player, DriveByView *view,
                             DriveByTarget *result)
{
    DriveByEntry entries[MAX_DRIVE_ENTRIES];
    int count = CollectDriveByTargets(world, player, view, entries), index, best = -1;
    for (index = 0; index < count; ++index)
    {
        DriveByTarget *t = &entries[index].target;
        if (g_driveTarget.owner && t->owner == g_driveTarget.owner && t->kind == g_driveTarget.kind &&
            t->wheel == g_driveTarget.wheel)
        {
            *result = *t;
            return 1;
        }
        if (best < 0 || (entries[index].preferred && !entries[best].preferred) ||
            (entries[index].preferred == entries[best].preferred && entries[index].score < entries[best].score))
            best = index;
    }
    if (best < 0)
        return 0;
    *result = entries[best].target;
    return 1;
}

/* Screen position of a point as angles from the crosshair axis; false when behind the camera. */
static int DriveByScreenPosition(const DriveByView *view, Vector3 right, Vector3 up, Vector3 point,
                                 float *sx, float *sy)
{
    Vector3 d = Subtract(point, view->cameraPosition);
    float forward, side;
    if (!Normalize(&d))
        return 0;
    forward = Dot(d, view->aimDirection);
    side = Dot(d, right);
    *sx = atan2f(side, forward);
    *sy = atan2f(Dot(d, up), sqrtf(forward * forward + side * side));
    return 1;
}

/* Direction: 1 right, 2 left, 3 up, 4 down. Picks the next target in that direction on screen. */
static int SwitchDriveByTarget(uintptr_t world, uintptr_t player, DriveByView *view,
                               const DriveByTarget *current, int direction, DriveByTarget *result)
{
    DriveByEntry entries[MAX_DRIVE_ENTRIES];
    int count = CollectDriveByTargets(world, player, view, entries), index, best = -1;
    Vector3 right, up, worldUp = {0.0f, 1.0f, 0.0f};
    float baseX = 0.0f, baseY = 0.0f, bestCost = 1.0e20f;
    int haveBase = 0;
    right = (Vector3){worldUp.y * view->aimDirection.z - worldUp.z * view->aimDirection.y,
                      worldUp.z * view->aimDirection.x - worldUp.x * view->aimDirection.z,
                      worldUp.x * view->aimDirection.y - worldUp.y * view->aimDirection.x};
    if (!Normalize(&right))
        return 0;
    up = (Vector3){view->aimDirection.y * right.z - view->aimDirection.z * right.y,
                   view->aimDirection.z * right.x - view->aimDirection.x * right.z,
                   view->aimDirection.x * right.y - view->aimDirection.y * right.x};
    for (index = 0; index < count && !haveBase; ++index)
    {
        DriveByTarget *t = &entries[index].target;
        if (t->owner == current->owner && t->kind == current->kind && t->wheel == current->wheel)
            haveBase = DriveByScreenPosition(view, right, up, t->point, &baseX, &baseY);
    }
    if (!haveBase)
        return 0;
    for (index = 0; index < count; ++index)
    {
        DriveByTarget *t = &entries[index].target;
        float x, y, along, across, cost;
        if ((t->owner == current->owner && t->kind == current->kind && t->wheel == current->wheel) ||
            !DriveByScreenPosition(view, right, up, t->point, &x, &y))
            continue;
        along = direction == 1 ? x - baseX : direction == 2 ? baseX - x :
                direction == 3 ? y - baseY : baseY - y;
        across = fabsf(direction <= 2 ? y - baseY : x - baseX);
        if (along < 0.0087f)
            continue;
        cost = along + 0.5f * across;
        if (cost < bestCost)
        {
            bestCost = cost;
            best = index;
        }
    }
    if (best < 0)
        return 0;
    *result = entries[best].target;
    return 1;
}

static int TargetStillValid(uintptr_t world, uintptr_t player, uintptr_t target, Vector3 *position)
{
    uintptr_t begin, count, i;
    if (!ListBounds(world, &begin, &count))
        return 0;
    for (i = 0; i < count; ++i)
        if (ReadU32(begin + i * 4u) == target && target != player)
            return IsLivePed(target, position);
    return 0;
}

static uintptr_t FindNearestPed(uintptr_t world, uintptr_t player, Vector3 cameraPosition,
                                Vector3 cameraForward, Vector3 *targetPosition)
{
    uintptr_t begin, count, i, nearest = 0, nearestEnemy = 0;
    float best = MAX_TARGET_DISTANCE * MAX_TARGET_DISTANCE;
    float bestEnemy = MAX_TARGET_DISTANCE * MAX_TARGET_DISTANCE;
    Vector3 nearestPosition = {0.0f, 0.0f, 0.0f};
    Vector3 enemyPosition = {0.0f, 0.0f, 0.0f};
    if (!ListBounds(world, &begin, &count))
        return 0;
    for (i = 0; i < count; ++i)
    {
        uintptr_t object = ReadU32(begin + i * 4u);
        Vector3 position, aimPosition, direction;
        int priorityEnemy;
        float distanceSquared;
        if (object == player || !IsLivePed(object, &position))
            continue;
        priorityEnemy = IsPriorityEnemyPed(object);
        GetAimPointPosition(object, position, &aimPosition);
        direction = Subtract(aimPosition, cameraPosition);
        distanceSquared = Dot(direction, direction);
        if (distanceSquared < 0.25f || distanceSquared > MAX_TARGET_DISTANCE * MAX_TARGET_DISTANCE ||
            !Normalize(&direction))
            continue;
        if (Dot(direction, cameraForward) <
            (priorityEnemy ? g_enemyTargetConeDot : g_targetConeDot))
            continue;
        if (!HasLineOfSight(cameraPosition, aimPosition))
            continue;
        if (priorityEnemy)
        {
            if (distanceSquared < bestEnemy)
            {
                bestEnemy = distanceSquared;
                nearestEnemy = object;
                enemyPosition = position;
            }
        }
        else if (distanceSquared < best)
        {
            best = distanceSquared;
            nearest = object;
            nearestPosition = position;
        }
    }
    if (nearestEnemy)
    {
        *targetPosition = enemyPosition;
        return nearestEnemy;
    }
    if (nearest)
        *targetPosition = nearestPosition;
    return nearest;
}

/* The person closest in bearing to `current` on one side of it (side +1 = right, -1 = left). */
static uintptr_t FindAdjacentPed(uintptr_t world, uintptr_t player, Vector3 cameraPosition,
                                 Vector3 cameraForward, uintptr_t current, int side)
{
    uintptr_t begin, count, i, found = 0;
    Vector3 currentPosition, currentAimPosition, direction;
    float currentBearing, bestDelta = SWITCH_MAX_ANGLE, bestEnemyDelta = SWITCH_MAX_ANGLE;
    uintptr_t foundEnemy = 0;
    if (!ListBounds(world, &begin, &count) || !IsLivePed(current, &currentPosition))
        return 0;
    GetAimPointPosition(current, currentPosition, &currentAimPosition);
    direction = Subtract(currentAimPosition, cameraPosition);
    currentBearing = atan2f(direction.x, direction.z);
    for (i = 0; i < count; ++i)
    {
        uintptr_t object = ReadU32(begin + i * 4u);
        Vector3 position, aimPosition;
        float distanceSquared, delta;
        if (object == player || object == current || !IsLivePed(object, &position))
            continue;
        GetAimPointPosition(object, position, &aimPosition);
        direction = Subtract(aimPosition, cameraPosition);
        distanceSquared = Dot(direction, direction);
        if (distanceSquared < 0.25f || distanceSquared > MAX_TARGET_DISTANCE * MAX_TARGET_DISTANCE ||
            !Normalize(&direction) || Dot(direction, cameraForward) < SWITCH_MIN_FORWARD_DOT)
            continue;
        delta = WrapAngle(atan2f(direction.x, direction.z) - currentBearing) * (float)side;
        if (delta < SWITCH_MIN_DELTA)
            continue;
        if (!HasLineOfSight(cameraPosition, aimPosition))
            continue;
        if (IsPriorityEnemyPed(object))
        {
            if (delta < bestEnemyDelta)
            {
                bestEnemyDelta = delta;
                foundEnemy = object;
            }
        }
        else if (delta < bestDelta)
        {
            bestDelta = delta;
            found = object;
        }
    }
    return foundEnemy ? foundEnemy : found;
}

/* ---- settings (MafiaAimAssist.ini beside Game.exe, re-read every second) -- */

typedef struct Config
{
    int stickLook;   /* right stick moves the camera like a mouse */
    int xSpeed;      /* mouse counts per second at full deflection */
    int ySpeed;
    int invertY;
    int deadzone;    /* percent of stick travel */
    int aimResponse; /* percentage scaling the lock-on controller */
    int switchStick; /* stick whose flick changes the locked target: 0 off, 1 left, 2 right */
    int aimKey;      /* keyboard virtual key used for lock-on */
} Config;

static Config g_cfg = {1, 1100, 1000, 0, 15, 70, 2, 'O'};
static char g_iniPath[MAX_PATH];
static LONGLONG g_configNext;

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int ParseAimKey(const char *name)
{
    int i;
    if (!name || !name[0])
        return 0;
    if (name[1])
    {
        if (name && name[0] == 'F' && name[1] >= '1' && name[1] <= '9' && !name[2])
            return VK_F1 + (name[1] - '1');
        if (name && name[0] == 'F' && name[1] == '1' && name[2] >= '0' && name[2] <= '2' && !name[3])
            return VK_F10 + (name[2] - '0');
        if (name && name[0] == 'F' && name[1] == '1' && name[2] == '2' && !name[3])
            return VK_F12;
    }
    else if ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') ||
             (name[0] >= '0' && name[0] <= '9'))
        return (int)name[0] >= 'a' ? (int)name[0] - ('a' - 'A') : (int)name[0];
    for (i = 0; i < 12; ++i)
    {
        char functionKey[4] = {'F', (char)('1' + i), '\0', '\0'};
        if (i >= 9)
        {
            functionKey[1] = '1';
            functionKey[2] = (char)('0' + i - 9);
        }
        if (lstrcmpiA(name, functionKey) == 0)
            return VK_F1 + i;
    }
    if (lstrcmpiA(name, "SPACE") == 0) return VK_SPACE;
    if (lstrcmpiA(name, "ENTER") == 0) return VK_RETURN;
    if (lstrcmpiA(name, "TAB") == 0) return VK_TAB;
    if (lstrcmpiA(name, "ESC") == 0 || lstrcmpiA(name, "ESCAPE") == 0) return VK_ESCAPE;
    if (lstrcmpiA(name, "SHIFT") == 0) return VK_SHIFT;
    if (lstrcmpiA(name, "CTRL") == 0 || lstrcmpiA(name, "CONTROL") == 0) return VK_CONTROL;
    if (lstrcmpiA(name, "ALT") == 0) return VK_MENU;
    if (lstrcmpiA(name, "CAPSLOCK") == 0) return VK_CAPITAL;
    if (lstrcmpiA(name, "BACKSPACE") == 0) return VK_BACK;
    return 0;
}

static void ReloadConfig(LONGLONG now)
{
    if (now < g_configNext)
        return;
    g_configNext = now + 1000;
    if (!g_iniPath[0])
    {
        DWORD n = GetModuleFileNameA(NULL, g_iniPath, MAX_PATH);
        if (!n || n > MAX_PATH - 32)
            return;
        while (n && g_iniPath[n - 1] != '\\' && g_iniPath[n - 1] != '/')
            --n;
        g_iniPath[n] = '\0';
        lstrcatA(g_iniPath, "MafiaAimAssist.ini");
    }
    g_cfg.stickLook = GetPrivateProfileIntA("aim", "right_stick_look", 1, g_iniPath) != 0;
    g_cfg.xSpeed = ClampInt((int)GetPrivateProfileIntA("aim", "look_x_speed", 1100, g_iniPath), 100, 6000);
    g_cfg.ySpeed = ClampInt((int)GetPrivateProfileIntA("aim", "look_y_speed", 1000, g_iniPath), 100, 6000);
    g_cfg.invertY = GetPrivateProfileIntA("aim", "invert_y", 0, g_iniPath) != 0;
    g_cfg.deadzone = ClampInt((int)GetPrivateProfileIntA("aim", "stick_deadzone", 15, g_iniPath), 0, 60);
    g_cfg.aimResponse = ClampInt((int)GetPrivateProfileIntA("aim", "aim_response_percent", 70, g_iniPath), 25, 150);
    g_cfg.switchStick = ClampInt((int)GetPrivateProfileIntA("aim", "target_switch_stick", 2, g_iniPath), 0, 2);
    g_prioritizeEnemies = GetPrivateProfileIntA("aim", "prioritize_enemies", 1, g_iniPath) != 0;
    g_vehicleAimEnabled = GetPrivateProfileIntA("aim", "vehicle_aim", 1, g_iniPath) != 0;
    g_vehicleArcade = GetPrivateProfileIntA("aim", "vehicle_arcade", 1, g_iniPath) != 0;
    g_vehicleOneShot = GetPrivateProfileIntA("aim", "vehicle_one_shot", 1, g_iniPath) != 0;
    g_vehicleFreeCamera = GetPrivateProfileIntA("aim", "vehicle_free_camera", 1, g_iniPath) != 0;
    g_cheatHealth = ClampInt((int)GetPrivateProfileIntA("cheats", "player_health", 0, g_iniPath), 0, 100000);
    g_cheatAcceleration = (float)ClampInt((int)GetPrivateProfileIntA(
        "cheats", "car_acceleration_percent", 100, g_iniPath), 100, 1000) / 100.0f;
    g_crouchToggleEnabled = GetPrivateProfileIntA("aim", "crouch_toggle", 1, g_iniPath) != 0;
    g_targetConeDot = cosf((float)ClampInt((int)GetPrivateProfileIntA(
        "aim", "target_cone_degrees", 20, g_iniPath), 5, 45) * PI_F / 180.0f);
    g_enemyTargetConeDot = cosf((float)ClampInt((int)GetPrivateProfileIntA(
        "aim", "enemy_target_cone_degrees", 35, g_iniPath), 5, 60) * PI_F / 180.0f);
    g_crouchAwareHeadAim = GetPrivateProfileIntA("aim", "experimental_crouch_head_aim", 1, g_iniPath) != 0;
    g_animatedHeadAim = GetPrivateProfileIntA("aim", "animated_head_aim", 1, g_iniPath) != 0;
    g_animatedHeadForward = (float)ClampInt((int)GetPrivateProfileIntA(
        "aim", "animated_head_forward_cm", 8, g_iniPath), 0, 25) / 100.0f;
    g_losEnabled = GetPrivateProfileIntA("aim", "require_line_of_sight", 1, g_iniPath) != 0;
    {
        char keyName[32];
        GetPrivateProfileStringA("aim", "aim_key", "O", keyName, sizeof(keyName), g_iniPath);
        {
            int key = ParseAimKey(keyName);
            if (key)
                g_cfg.aimKey = key;
        }
    }
    g_aimHeight = (float)ClampInt((int)GetPrivateProfileIntA("aim", "aim_height_cm", 95, g_iniPath), 40, 180) / 100.0f;
}

/* ---- controller (XInput) ------------------------------------------------- */

typedef struct XiGamepad
{
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT thumbLX, thumbLY, thumbRX, thumbRY;
} XiGamepad;

typedef struct XiState
{
    DWORD packet;
    XiGamepad gamepad;
} XiState;

typedef DWORD (WINAPI *XInputGetStateFn)(DWORD index, XiState *state);

#define TRIGGER_PRESS   60
#define TRIGGER_RELEASE 20

static XiState g_pad;
static int     g_padOk;

static void PollPad(LONGLONG now)
{
    static XInputGetStateFn getState;
    static int tried, slot = -1;
    static LONGLONG nextPoll, nextScan;
    XiState state;
    int i;

    if (!tried)
    {
        static const char *libraries[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        tried = 1;
        for (i = 0; i < 3 && !getState; ++i)
        {
            HMODULE library = LoadLibraryA(libraries[i]);
            if (library)
            {
                getState = (XInputGetStateFn)GetProcAddress(library, "XInputGetState");
                if (getState)
                    Log("using %s", libraries[i]);
            }
        }
    }
    if (!getState || now < nextPoll)
        return;
    nextPoll = now + 3;

    if (slot < 0)
    {
        g_padOk = 0;
        if (now < nextScan)
            return;
        nextScan = now + 2000;
        for (i = 0; i < 4 && slot < 0; ++i)
            if (getState((DWORD)i, &state) == ERROR_SUCCESS)
                slot = i;
        if (slot < 0)
            return;
        Log("xinput controller found in slot %d", slot);
    }
    if (getState((DWORD)slot, &state) != ERROR_SUCCESS)
    {
        slot = -1;
        g_padOk = 0;
        return;
    }
    g_pad = state;
    g_padOk = 1;
}

__declspec(dllexport) int __cdecl AimCrouch(uintptr_t human, int requested)
{
    static int initialized;
    static int wasPressed;
    int pressed;
    uintptr_t world, player;
    LONGLONG now = NowMs();
    ReloadConfig(now);
    PollPad(now);
    if (!g_padOk || !g_crouchToggleEnabled ||
        !GetWorld(&world, &player) || player != human)
    {
        initialized = 0;
        wasPressed = 0;
        g_crouchPlayer = 0;
        g_crouchDesired = -1;
        return requested;
    }
    pressed = (g_pad.gamepad.buttons & GAMEPAD_B_BUTTON) != 0;
    if (!initialized || g_crouchPlayer != player)
    {
        initialized = 1;
        g_crouchPlayer = player;
        wasPressed = pressed;
        g_crouchDesired = -1;
        return requested;
    }
    if (pressed && !wasPressed)
    {
        int observed = ReadTargetStanceByte(player);
        if (observed >= 0)
            g_crouchDesired = !(g_crouchDesired < 0 ? observed : g_crouchDesired);
        Log("crouch toggle B: desired=%d observed=%d", g_crouchDesired, observed);
    }
    wasPressed = pressed;
    return g_crouchDesired < 0 ? requested : g_crouchDesired;
}

static int TriggerHeld(void)
{
    static int held;
    if (!g_padOk)
        return held = 0;
    held = held ? g_pad.gamepad.leftTrigger > TRIGGER_RELEASE : g_pad.gamepad.leftTrigger > TRIGGER_PRESS;
    return held;
}

static int AimButtonHeld(void)
{
    return TriggerHeld() || (GetAsyncKeyState(g_cfg.aimKey) & 0x8000) != 0;
}

/* One-shot flick of the chosen stick sideways: -1 left, +1 right, 0 none. Re-arms when the stick returns. */
static int StickFlick(void)
{
    static int armed = 1;
    float x, y;
    if (!g_padOk || g_cfg.switchStick == 0)
        return 0;
    x = (float)(g_cfg.switchStick == 1 ? g_pad.gamepad.thumbLX : g_pad.gamepad.thumbRX) / 32767.0f;
    y = (float)(g_cfg.switchStick == 1 ? g_pad.gamepad.thumbLY : g_pad.gamepad.thumbRY) / 32767.0f;
    if (fabsf(x) < 0.3f)
        armed = 1;
    else if (armed && fabsf(x) > 0.7f && fabsf(x) > fabsf(y) * STICK_FLICK_DOMINANCE)
    {
        armed = 0;
        return x > 0.0f ? 1 : -1;
    }
    return 0;
}

static int AimHeightFlick(void)
{
    static int armed = 1;
    float x, y;
    if (!g_padOk)
        return 0;
    x = (float)g_pad.gamepad.thumbRX / 32767.0f;
    y = (float)g_pad.gamepad.thumbRY / 32767.0f;
    if (fabsf(y) < 0.3f)
        armed = 1;
    else if (armed && fabsf(y) > 0.7f && fabsf(y) > fabsf(x) * STICK_FLICK_DOMINANCE)
    {
        armed = 0;
        return y > 0.0f ? 1 : -1;
    }
    return 0;
}

/* Right stick drives the mouse axes: radial deadzone, squared response, counts per second. */
static void AddStickLook(LONG *lx, LONG *ly, LONG dtMs)
{
    static float remX, remY;
    static int logged;
    float sx = (float)g_pad.gamepad.thumbRX / 32767.0f;
    float sy = (float)g_pad.gamepad.thumbRY / 32767.0f;
    float magnitude = sqrtf(sx * sx + sy * sy);
    float deadzone = (float)g_cfg.deadzone / 100.0f;
    float scaled, curve, dt, cx, cy;
    LONG ix, iy;

    if (magnitude <= deadzone)
    {
        remX = remY = 0.0f;
        return;
    }
    scaled = (magnitude - deadzone) / (1.0f - deadzone);
    if (scaled > 1.0f)
        scaled = 1.0f;
    curve = scaled * scaled;
    dt = (float)dtMs / 1000.0f;
    cx = (sx / magnitude) * curve * (float)g_cfg.xSpeed * dt + remX;
    cy = -(sy / magnitude) * curve * (float)g_cfg.ySpeed * dt * (g_cfg.invertY ? -1.0f : 1.0f) + remY;
    ix = (LONG)floorf(cx + 0.5f);
    iy = (LONG)floorf(cy + 0.5f);
    remX = cx - (float)ix;
    remY = cy - (float)iy;
    *lx += ix;
    *ly += iy;
    if (!logged)
    {
        logged = 1;
        Log("right stick look active");
    }
}

/* ---- sensitivity: learned online from how the camera really responds ------ */

typedef struct AxisModel
{
    double gain;      /* signed radians of camera turn per mouse count */
    double sxy, sxx;  /* decayed regression sums of turn against counts */
    int votes;        /* consecutive estimates that disagree in sign */
    int updates;
} AxisModel;

static AxisModel g_axis[2];
static AxisModel g_footAxis[2], g_carAxis[2];
static uintptr_t g_aimCar;

static void SeedGains(void)
{
    memset(g_axis, 0, sizeof(g_axis));
    g_axis[0].gain = SEED_GAIN_X;
    g_axis[1].gain = SEED_GAIN_Y;
}

static void GainPath(char *path)
{
    GetTempPathA(MAX_PATH - 32, path);
    lstrcatA(path, "MafiaAimGain.cal");
}

static void LoadGains(void)
{
    char path[MAX_PATH];
    FILE *file;
    long gx = 0, gy = 0;
    SeedGains();
    GainPath(path);
    file = fopen(path, "r");
    if (!file)
        return;
    if (fscanf(file, "%ld %ld", &gx, &gy) == 2)
    {
        double x = (double)gx * 1.0e-7, y = (double)gy * 1.0e-7;
        if (fabs(x) > 0.0007 && fabs(x) < 0.006 && fabs(y) > 0.0007 && fabs(y) < 0.006)
        {
            g_axis[0].gain = x;
            g_axis[1].gain = y;
        }
    }
    fclose(file);
}

static void SaveGains(void)
{
    char path[MAX_PATH];
    FILE *file;
    GainPath(path);
    file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file, "%ld %ld\n", (long)(g_axis[0].gain * 1.0e7), (long)(g_axis[1].gain * 1.0e7));
    fclose(file);
}

static void EstimateAxis(AxisModel *axis, const char *name, float turn, LONG counts)
{
    double estimate;
    LONG magnitude = counts < 0 ? -counts : counts;

    axis->sxy *= 0.97;
    axis->sxx *= 0.97;
    if (magnitude < 3)
        return;
    if (magnitude >= 15 && fabsf(turn) < 0.0002f)
        return; /* the camera did not move at all: clamped, not informative */
    axis->sxy += (double)turn * (double)counts;
    axis->sxx += (double)counts * (double)counts;
    if (axis->sxx < 6000.0)
        return;
    estimate = axis->sxy / axis->sxx;
    if (fabs(estimate) < 0.0004 || fabs(estimate) > 0.01)
        return;
    if ((estimate > 0.0) == (axis->gain > 0.0))
    {
        axis->votes = 0;
        axis->gain += 0.2 * (estimate - axis->gain);
        ++axis->updates;
    }
    else if (++axis->votes >= 12)
    {
        Log("%s gain sign corrected: %.6f -> %.6f", name, axis->gain, estimate);
        axis->gain = estimate;
        axis->sxy = axis->sxx = 0.0;
        axis->votes = 0;
    }
}

/* ---- aim controller ------------------------------------------------------ */

static uintptr_t g_target;
static int       g_held;
static float     g_K = K_INIT, g_prevError = -1.0f, g_remX, g_remY;
static int       g_growSteps, g_slowSteps, g_floorHits, g_logSteps, g_stallX, g_stallY, g_stepsSinceRetry;
static LONG      g_lastAssistX, g_lastAssistY;
static LONGLONG  g_noTargetLogAt;
static int       g_switchSide;
static LONGLONG  g_nextLosCheck;
static int       g_losBlockedChecks;
static LONGLONG  g_nextSwitchAt;
static int       g_acquirePending;

__declspec(dllexport) int __cdecl AimArcadeShot(uintptr_t game, uintptr_t shooter,
                                              Vector3 *origin, Vector3 *direction,
                                              float *power, int *count)
{
    uintptr_t world, player;
    uintptr_t car;
    DriveByView view;
    DriveByTarget target;
    Vector3 corrected;
    float standOff;
    if (!g_vehicleArcade || !g_vehicleAimEnabled || !origin || !direction ||
        !power || !count || !GetActiveWorld(&world, &player) || shooter != player ||
        game != ReadU32(world + MISSION_GAME_OFFSET))
        return 0;
    car = ReadU32(player + 0x98u);
    if (ReadU32(car + ENTITY_KIND_OFFSET) != CAR_KIND || ReadU32(player + 0xACu) != 0 ||
        ReadU32(player + 0x9Cu) != 0 || ReadByte(player + 0xADAu) != 0 ||
        ReadByte(car + 0x5Eu) != 0 || ReadU32(player + 0x1E8u) < 1 || ReadU32(player + 0x1E8u) > 3)
    {
        static int rejected;
        if (car && rejected < 20)
        {
            ++rejected;
            Log("player car shot ignored: kind=%lu seat=%lu link9C=%lu locked=%d car5E=%d weapon=%lu",
                (unsigned long)ReadU32(car + ENTITY_KIND_OFFSET), (unsigned long)ReadU32(player + 0xACu),
                (unsigned long)ReadU32(player + 0x9Cu), ReadByte(player + 0xADAu), ReadByte(car + 0x5Eu),
                (unsigned long)ReadU32(player + 0x1E8u));
        }
        return 0;
    }
    if (!g_held || !ReadDriveByView(player, &view) || !FindDriveByTarget(world, player, &view, &target))
    {
        if (!g_vehicleOneShot)
            return 0;
        Log("player car manual shot: damage=%.1f; direction and spread retained", *power);
        return 1;
    }
    corrected = Subtract(target.point, *origin);
    if (!Normalize(&corrected))
        return 0;
    standOff = target.kind == 1 ? 4.0f : 2.0f;
    origin->x = target.point.x - corrected.x * standOff;
    origin->y = target.point.y - corrected.y * standOff;
    origin->z = target.point.z - corrected.z * standOff;
    direction->x = corrected.x * (standOff + 2.0f);
    direction->y = corrected.y * (standOff + 2.0f);
    direction->z = corrected.z * (standOff + 2.0f);
    *count = 1;
    Log("arcade player car shot: target=0x%08lX kind=%d damage=%.1f stand_off=%.1f",
        (unsigned long)target.owner, target.kind, *power, standOff);
    return 1;
}

__declspec(dllexport) float __cdecl AimArcadeHitDamage(uintptr_t game, uintptr_t victim,
                                                      uintptr_t source, int type, float damage)
{
    uintptr_t world, player, car;
    float health;
    if (!g_vehicleArcade || !g_vehicleOneShot || !g_vehicleAimEnabled || type != 0 ||
        !GetActiveWorld(&world, &player) || source != player ||
        game != ReadU32(world + MISSION_GAME_OFFSET) ||
        ReadU32(victim + ENTITY_KIND_OFFSET) != SCRIPTABLE_NPC_KIND ||
        !HostileGroup(ReadU32(victim + ENTITY_TYPE_GROUP_OFFSET)) ||
        ReadByte(victim + 0x5Du) != 1 || ReadByte(victim + 0x5Eu) != 0 ||
        !IsReadable((const void *)(victim + ENTITY_HEALTH_OFFSET), 4))
        return damage;
    car = ReadU32(player + 0x98u);
    if (ReadU32(car + ENTITY_KIND_OFFSET) != CAR_KIND || ReadU32(player + 0xACu) != 0 ||
        ReadByte(player + 0xADAu) != 0 || ReadU32(player + 0x1E8u) < 1 || ReadU32(player + 0x1E8u) > 3)
        return damage;
    health = *(volatile float *)(victim + ENTITY_HEALTH_OFFSET);
    if (!IsFinite(health) || health <= 0.01f || health > 1000.0f)
        return damage;
    Log("confirmed player car police hit: damage %.4f -> %.1f hp=%.1f", damage, health * 10.0f, health);
    return health * 10.0f;
}

typedef struct WheelHitState
{
    uintptr_t world, car, wheel;
    uint32_t guid, shot;
    int hits;
} WheelHitState;
static WheelHitState g_wheelHits[64];
static uint32_t g_nextWheelHit;

__declspec(dllexport) int __cdecl AimArcadeWheelHit(uintptr_t game, uintptr_t car,
                                                  uintptr_t source, int type, uintptr_t frame, uint32_t shot,
                                                  uintptr_t *wheelOut, int *indexOut)
{
    uintptr_t world, player, wheels, wheel = 0;
    uint32_t count, index, slot, guid;
    WheelHitState *state = NULL;
    if (!g_vehicleArcade || !g_vehicleAimEnabled || type != 0 || !shot || !wheelOut || !indexOut ||
        !GetActiveWorld(&world, &player) || source != player ||
        game != ReadU32(world + MISSION_GAME_OFFSET) ||
        !IsHostileCar(car) ||
        car == ReadU32(player + 0x98u) || ReadU32(ReadU32(player + 0x98u) + ENTITY_KIND_OFFSET) != CAR_KIND ||
        ReadU32(player + 0xACu) != 0 || ReadByte(player + 0xADAu) != 0 ||
        ReadU32(player + 0x1E8u) < 1 || ReadU32(player + 0x1E8u) > 3)
        return 0;
    count = ReadU32(car + CAR_WHEEL_COUNT_OFFSET);
    wheels = ReadU32(car + CAR_WHEELS_OFFSET);
    if (!count || count > 8 || !IsReadable((void *)wheels, count * 4u))
        return 0;
    for (index = 0; index < count; ++index)
    {
        uintptr_t candidate = ReadU32(wheels + index * 4u);
        uintptr_t parent = frame ? ReadU32(frame + 0x120u) : 0;
        uintptr_t model = ReadU32(candidate + 4u);
        if (candidate && frame && (model == frame || (parent &&
            (model == parent || ReadU32(candidate + 8u) == parent))) &&
            IsReadable((void *)candidate, WHEEL_FLAGS_OFFSET + 4u) &&
            !(ReadU32(candidate + WHEEL_FLAGS_OFFSET) & WHEEL_UNUSABLE_FLAG))
        {
            wheel = candidate;
            break;
        }
    }
    if (!wheel)
    {
        static int missed;
        if (missed < 30)
        {
            ++missed;
            Log("player hit police car without a usable wheel: frame=0x%08lX parent=0x%08lX wheels=%lu",
                (unsigned long)frame, (unsigned long)(frame ? ReadU32(frame + 0x120u) : 0),
                (unsigned long)count);
        }
        return 0;
    }
    guid = ReadU32(car + 0x18u);
    for (slot = 0; slot < 64; ++slot)
        if (g_wheelHits[slot].world == world && g_wheelHits[slot].car == car &&
            g_wheelHits[slot].guid == guid && g_wheelHits[slot].wheel == wheel)
            state = &g_wheelHits[slot];
    if (!state)
    {
        state = &g_wheelHits[g_nextWheelHit++ % 64u];
        *state = (WheelHitState){world, car, wheel, guid, 0, 0};
    }
    else if (!(ReadU32(wheel + WHEEL_FLAGS_OFFSET) & 0x80000000u))
    {
        state->shot = 0;
        state->hits = 0;
    }
    *wheelOut = wheel;
    *indexOut = (int)index;
    if (state->hits && state->shot == shot)
        return -1;
    state->shot = shot;
    if (state->hits < 2)
        ++state->hits;
    Log("confirmed player car tyre hit: car=0x%08lX wheel=%lu stage=%d",
        (unsigned long)car, (unsigned long)index, state->hits);
    return state->hits;
}

static void ReleaseAim(void)
{
    if (g_held)
    {
        Log("release (gain %.6f,%.6f, updates %d,%d)", g_axis[0].gain, g_axis[1].gain,
            g_axis[0].updates, g_axis[1].updates);
        if (!g_aimCar && g_axis[0].updates >= 10 && g_axis[1].updates >= 10)
            SaveGains();
    }
    g_held = 0;
    g_target = 0;
    g_driveSwitch = 0;
    memset(&g_driveTarget, 0, sizeof(g_driveTarget));
    g_switchSide = 0;
    g_aimZone = 0;
    g_nextLosCheck = 0;
    g_losBlockedChecks = 0;
    g_nextSwitchAt = 0;
    g_acquirePending = 0;
    g_prevError = -1.0f;
    g_K = K_INIT;
    g_growSteps = g_slowSteps = g_floorHits = g_logSteps = 0;
    g_stallX = g_stallY = g_stepsSinceRetry = 0;
    g_lastAssistX = g_lastAssistY = 0;
    g_remX = g_remY = 0.0f;
}

static void RunAim(LONG *lx, LONG *ly, uintptr_t world, uintptr_t player, Vector3 cameraPosition,
                   Vector3 cameraForward, float yaw, float pitch, float turnYaw, float turnPitch,
                   LONGLONG now)
{
    Vector3 targetPosition, direction;
    float errYaw, errPitch, error, cx, cy, brakeX, brakeY;
    int blockX, blockY;
    LONG ix = 0, iy = 0;
    LONG prevAssistX = g_lastAssistX, prevAssistY = g_lastAssistY;
    int driving = ReadU32(player + 0x98u) != 0;

    g_lastAssistX = g_lastAssistY = 0;
    if (g_axis[0].gain == 0.0 || g_axis[1].gain == 0.0)
        return;

    if (driving)
    {
        DriveByView view;
        DriveByTarget next = {0};
        int found = ReadDriveByView(player, &view), switched = 0;
        if (found && g_driveSwitch && g_driveTarget.owner)
        {
            switched = SwitchDriveByTarget(world, player, &view, &g_driveTarget, g_driveSwitch, &next);
            Log("drive-by switch %s: %s", g_driveSwitch == 1 ? "right" : g_driveSwitch == 2 ? "left" :
                g_driveSwitch == 3 ? "up" : "down", switched ? "next target" : "no target that way");
        }
        g_driveSwitch = 0;
        if (found && !switched)
            found = FindDriveByTarget(world, player, &view, &next);
        if (!found)
        {
            g_target = 0;
            memset(&g_driveTarget, 0, sizeof(g_driveTarget));
            g_remX = g_remY = 0.0f;
            g_prevError = -1.0f;
            if (now >= g_noTargetLogAt)
            {
                uintptr_t cars[MAX_CANDIDATE_CARS], total = CollectCars(world, cars), index;
                unsigned hostile = 0;
                for (index = 0; index < total; ++index)
                    hostile += IsHostileCar(cars[index]) != 0;
                Log("drive-by: no target (cars %lu, hostile %u)", (unsigned long)total, hostile);
                g_noTargetLogAt = now + 1500;
            }
            return;
        }
        if (next.owner != g_driveTarget.owner || next.kind != g_driveTarget.kind ||
            next.wheel != g_driveTarget.wheel)
        {
            g_prevError = -1.0f;
            g_remX = g_remY = 0.0f;
            g_stallX = g_stallY = 0;
            Log("drive-by target 0x%08lX kind=%d wheel=%lu",
                (unsigned long)next.owner, next.kind, (unsigned long)next.wheel);
        }
        g_driveTarget = next;
        g_target = next.owner;
        targetPosition = next.point;
        g_switchSide = 0;
        g_aimZone = 0;
    }
    else
    {
    if (g_target && !TargetStillValid(world, player, g_target, &targetPosition))
    {
        Log("target lost; release and press aim to acquire again");
        g_target = 0;
        g_acquirePending = 0;
        g_switchSide = 0;
        g_prevError = -1.0f;
        g_stallX = g_stallY = 0;
        g_losBlockedChecks = 0;
        return;
    }

    if (!g_target)
    {
        if (!g_acquirePending)
            return;
        g_target = FindNearestPed(world, player, cameraPosition, cameraForward, &targetPosition);
        g_prevError = -1.0f;
        g_stallX = g_stallY = 0;
        g_losBlockedChecks = 0;
        g_nextLosCheck = now;
        if (!g_target)
        {
            g_switchSide = 0;
            if (now > g_noTargetLogAt)
            {
                Log("no target in view");
                g_noTargetLogAt = now + 1500;
            }
            return;
        }
        g_acquirePending = 0;
        Log("target 0x%08lX at %.1f %.1f %.1f enemy_priority=%d", (unsigned long)g_target,
            targetPosition.x, targetPosition.y, targetPosition.z, IsPriorityEnemyPed(g_target));
    }

    if (g_switchSide && now < g_nextSwitchAt)
        g_switchSide = 0;

    if (g_switchSide)
    {
        uintptr_t next = FindAdjacentPed(world, player, cameraPosition, cameraForward, g_target, g_switchSide);
        g_switchSide = 0;
        if (next && IsLivePed(next, &targetPosition))
        {
            g_target = next;
            g_prevError = -1.0f;
            g_stallX = g_stallY = 0;
            g_logSteps = 0;
            g_losBlockedChecks = 0;
            g_nextLosCheck = now;
            g_nextSwitchAt = now + TARGET_SWITCH_COOLDOWN_MS;
            Log("switched to target 0x%08lX enemy_priority=%d", (unsigned long)g_target,
                IsPriorityEnemyPed(g_target));
        }
        else
        {
            Log("no other person on that side");
        }
    }

    ProbeStanceByte(g_target);
    GetAimPointPosition(g_target, targetPosition, &targetPosition);
    if (g_losEnabled && now >= g_nextLosCheck)
    {
        g_nextLosCheck = now + LOS_CHECK_INTERVAL_MS;
        if (HasLineOfSight(cameraPosition, targetPosition))
            g_losBlockedChecks = 0;
        else if (++g_losBlockedChecks >= LOS_BLOCKED_DROP_CHECKS)
        {
            Log("target behind cover: dropped");
            g_target = 0;
            g_acquirePending = 0;
            g_switchSide = 0;
            g_prevError = -1.0f;
            g_stallX = g_stallY = 0;
            g_losBlockedChecks = 0;
            return;
        }
    }
    }
    direction = Subtract(targetPosition, cameraPosition);
    if (!Normalize(&direction))
        return;
    errYaw = WrapAngle(atan2f(direction.x, direction.z) - yaw);
    errPitch = asinf(ClampUnit(direction.y)) - pitch;
    brakeX = fabsf(errYaw) / AIM_BRAKE_ANGLE;
    brakeY = fabsf(errPitch) / AIM_BRAKE_ANGLE;
    if (brakeX < AIM_BRAKE_FLOOR) brakeX = AIM_BRAKE_FLOOR;
    if (brakeY < AIM_BRAKE_FLOOR) brakeY = AIM_BRAKE_FLOOR;
    if (brakeX > 1.0f) brakeX = 1.0f;
    if (brakeY > 1.0f) brakeY = 1.0f;

    /* An axis that does not move although we push it is at a hard limit: stop pushing it. */
    if (labs(prevAssistX) >= 40 && fabsf(turnYaw) < 0.0006f) ++g_stallX; else g_stallX = 0;
    if (labs(prevAssistY) >= 40 && fabsf(turnPitch) < 0.0006f) ++g_stallY; else g_stallY = 0;
    if (++g_stepsSinceRetry > 90)
    {
        g_stepsSinceRetry = 0;
        g_stallX = g_stallY = 0;
    }
    blockX = g_stallX >= 6;
    blockY = g_stallY >= 6;
    error = sqrtf((blockX ? 0.0f : errYaw * errYaw) + (blockY ? 0.0f : errPitch * errPitch));

    if (g_prevError >= 0.0f)
    {
        if (error > g_prevError * 1.08f + 0.002f)
        {
            if (++g_growSteps >= 3)
            {
                g_growSteps = 0;
                g_K *= 0.6f;
                if (g_K < K_MIN)
                {
                    g_K = K_MIN;
                    if (++g_floorHits >= 3)
                    {
                        Log("controller diverging at minimum gain: back to seed sensitivity");
                        SeedGains();
                        g_floorHits = 0;
                        return;
                    }
                }
            }
        }
        else
        {
            g_growSteps = 0;
        }
        if (error > g_prevError * 0.93f && error > 0.01f && g_growSteps == 0)
        {
            if (++g_slowSteps >= 6)
            {
                g_slowSteps = 0;
                g_K = g_K * 1.25f > K_MAX ? K_MAX : g_K * 1.25f;
            }
        }
        else
        {
            g_slowSteps = 0;
        }
    }
    g_prevError = error;

    if (error < DEADZONE_RAD)
    {
        g_remX = g_remY = 0.0f;
        return;
    }

    cx = blockX ? 0.0f : brakeX * g_K * (float)g_cfg.aimResponse * 0.01f *
        errYaw / (float)g_axis[0].gain + g_remX;
    cy = blockY ? 0.0f : brakeY * g_K * (float)g_cfg.aimResponse * 0.01f *
        errPitch / (float)g_axis[1].gain + g_remY;
    ix = (LONG)floorf(cx + 0.5f);
    iy = (LONG)floorf(cy + 0.5f);
    g_remX = cx - (float)ix;
    g_remY = cy - (float)iy;
    if (ix > MAX_STEP_COUNTS) { ix = MAX_STEP_COUNTS; g_remX = 0.0f; }
    if (ix < -MAX_STEP_COUNTS) { ix = -MAX_STEP_COUNTS; g_remX = 0.0f; }
    if (iy > MAX_STEP_COUNTS) { iy = MAX_STEP_COUNTS; g_remY = 0.0f; }
    if (iy < -MAX_STEP_COUNTS) { iy = -MAX_STEP_COUNTS; g_remY = 0.0f; }
    *lx += ix;
    *ly += iy;
    g_lastAssistX = ix;
    g_lastAssistY = iy;

    if (g_logSteps < 90)
    {
        ++g_logSteps;
        Log("step err=%.2f,%.2f deg  counts=%ld,%ld  K=%.2f%s%s", errYaw * 57.29578f,
            errPitch * 57.29578f, ix, iy, g_K, blockX ? " [x blocked]" : "", blockY ? " [y blocked]" : "");
    }
}

/* ---- entry point: called for every mouse state the game reads ---------------- */

static LONGLONG g_lastCall, g_lastStep;
static int      g_haveAngles;
static float    g_prevYaw, g_prevPitch;
static LONG     g_curX, g_curY; /* counts delivered since the previous measurement */

#define FREE_LOOK_RVA 0x1DAAD0u
typedef void (__fastcall *EnableFreeLookFn)(void *camera, void *unused, int enable, int forceChase);

/* Optional test cheats: raised health ceiling and a motor-force multiplier on the player's own car. */
static void ApplyPlayerCheats(uintptr_t player, uintptr_t car)
{
    static uintptr_t boostedCar;
    static float baseFactor, writtenFactor;
    int wantBoost = g_cheatAcceleration > 1.0f && car && ReadU32(car + ENTITY_KIND_OFFSET) == CAR_KIND &&
        ReadU32(player + 0xACu) == 0 && ReadU32(player + 0x9Cu) == 0;
    if (g_cheatHealth > 0 && ReadByte(player + 0x5Du) == 1 && IsReadable((const void *)(player + 0x604u), 0x44u))
    {
        volatile float *ceiling = (volatile float *)(player + 0x604u);
        volatile float *health = (volatile float *)(player + 0x644u);
        if (IsFinite(*health) && *health > 0.0f && *ceiling < (float)g_cheatHealth)
        {
            *ceiling = (float)g_cheatHealth;
            *health = (float)g_cheatHealth;
            Log("cheat: player health set to %d", g_cheatHealth);
        }
    }
    if (boostedCar && (boostedCar != car || !wantBoost) && IsReadable((const void *)(boostedCar + 0x2A0u), 4) &&
        ReadU32(boostedCar + ENTITY_KIND_OFFSET) == CAR_KIND &&
        *(volatile float *)(boostedCar + 0x2A0u) == writtenFactor)
    {
        *(volatile float *)(boostedCar + 0x2A0u) = baseFactor;
        boostedCar = 0;
    }
    if (wantBoost && IsReadable((const void *)(car + 0x2A0u), 4))
    {
        volatile float *factor = (volatile float *)(car + 0x2A0u);
        if (boostedCar != car || *factor != writtenFactor)
            baseFactor = *factor;
        if (IsFinite(baseFactor) && baseFactor > 0.0f && baseFactor <= 4.0f)
        {
            writtenFactor = baseFactor * g_cheatAcceleration;
            *factor = writtenFactor;
            if (boostedCar != car)
                Log("cheat: car motor force x%.2f", g_cheatAcceleration);
            boostedCar = car;
        }
    }
}

/* Records the player's weapon/aim bytes around a collision with the own car (what holsters the weapon). */
static void TraceCollisionAftermath(uintptr_t player, uintptr_t car)
{
    static uint32_t lastDamage;
    static uintptr_t lastCar;
    static DWORD until, nextLog;
    static int traced;
    uint32_t damage = ReadU32(car + 0x2154u);
    DWORD tick = GetTickCount();
    if (car != lastCar)
    {
        lastCar = car;
        lastDamage = damage;
        until = 0;
        return;
    }
    if (damage != lastDamage)
    {
        Log("own car collision damage bytes %08lX>%08lX", (unsigned long)lastDamage, (unsigned long)damage);
        lastDamage = damage;
        until = tick + 2000;
        nextLog = 0;
    }
    if (until && (int)(until - tick) > 0 && tick >= nextLog && traced < 40)
    {
        ++traced;
        nextLog = tick + 150;
        Log("collision aftermath: hp=%.1f state=%lu weapon=%lu f1E5=%d f1EC=%d f1FE=%d f21C=%d fAF5=%d f581=%d model=%d",
            ReadU32(player + 0x644u) ? *(volatile float *)(player + 0x644u) : 0.0f,
            (unsigned long)ReadU32(player + 0x40Cu), (unsigned long)ReadU32(player + 0x1E8u),
            ReadByte(player + 0x1E5u), ReadByte(player + 0x1ECu), ReadByte(player + 0x1FEu),
            ReadByte(player + 0x21Cu), ReadByte(player + 0xAF5u), ReadByte(player + 0x581u),
            ReadU32(player + 0x564u) != 0);
    }
}

static void TraceDriverState(uintptr_t player, uintptr_t car, uintptr_t camera)
{
    static uint32_t last[6];
    static uintptr_t lastCar;
    static int traced;
    uint32_t now[6];
    now[0] = ReadU32(player + 0x1E8u);
    now[1] = ReadU32(player + 0x40Cu);
    now[2] = (uint32_t)ReadByte(player + 0xADAu);
    now[3] = ReadU32(player + 0x564u) != 0;
    now[4] = ReadU32(camera + 0x10u);
    now[5] = (uint32_t)ReadByte(car + 0x5Eu);
    if (car != lastCar)
    {
        lastCar = car;
        memcpy(last, now, sizeof(last));
        return;
    }
    if (!memcmp(last, now, sizeof(last)) || traced >= 40)
        return;
    ++traced;
    Log("driver state change: weapon %lu>%lu state %lu>%lu locked %lu>%lu model %lu>%lu cam %lu>%lu car5E %lu>%lu",
        (unsigned long)last[0], (unsigned long)now[0], (unsigned long)last[1], (unsigned long)now[1],
        (unsigned long)last[2], (unsigned long)now[2], (unsigned long)last[3], (unsigned long)now[3],
        (unsigned long)last[4], (unsigned long)now[4], (unsigned long)last[5], (unsigned long)now[5]);
    memcpy(last, now, sizeof(last));
}

/* Keeps the native chase camera behind the car and rotatable through the aiming input. */
static void UpdateDriverCamera(uintptr_t world, uintptr_t player, uintptr_t car)
{
    static const BYTE entry[] = {0x8A, 0x81, 0x94, 0, 0, 0, 0x84, 0xC0, 0x75, 0x14};
    static uintptr_t activeCar;
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t camera = ReadU32(world + MISSION_GAME_OFFSET) + 0x4Cu;
    EnableFreeLookFn enable = (EnableFreeLookFn)(base + FREE_LOOK_RVA);
    uint32_t mode;
    int wanted;
    if (car)
    {
        TraceDriverState(player, car, camera);
        TraceCollisionAftermath(player, car);
    }
    if ((!g_vehicleFreeCamera && !activeCar) || !IsReadable((const void *)(base + FREE_LOOK_RVA), sizeof(entry)) ||
        memcmp((const void *)(base + FREE_LOOK_RVA), entry, sizeof(entry)) != 0 ||
        !IsReadable((const void *)camera, 0x98u))
        return;
    mode = ReadU32(camera + 0x10u);
    wanted = g_vehicleFreeCamera && car && ReadU32(car + ENTITY_KIND_OFFSET) == CAR_KIND &&
        ReadU32(player + 0xACu) == 0 && ReadU32(player + 0x9Cu) == 0 &&
        ReadByte(player + 0xADAu) == 0 && mode >= 7u && mode <= 0xFu;
    if (wanted && ReadByte(camera + 0x94u) == 0)
    {
        enable((void *)camera, NULL, 1, activeCar != car);
        Log("driver camera: free look enabled (mode %lu, %s)", (unsigned long)mode,
            activeCar != car ? "locked behind the car" : "kept");
        activeCar = car;
    }
    else if (activeCar && (!car || !g_vehicleFreeCamera))
    {
        if (ReadByte(camera + 0x94u) == 1)
            enable((void *)camera, NULL, 0, 0);
        Log("driver camera: native camera restored");
        activeCar = 0;
    }
}

static void HandleMouse(LONG *lx, LONG *ly)
{
    uintptr_t world, player;
    Vector3 cameraPosition, cameraForward;
    float yaw, pitch;
    LONGLONG now = NowMs();
    LONG dt = (LONG)(now - g_lastCall);
    int held;
    uintptr_t car = 0;
    int active;

    g_lastCall = now;
    if (dt < 1) dt = 1;
    if (dt > 50) dt = 50;

    ReloadConfig(now);
    PollPad(now);
    active = GetActiveWorld(&world, &player);
    if (active)
        car = ReadU32(player + 0x98u);
    if (car != g_aimCar)
    {
        ReleaseAim();
        memcpy(g_aimCar ? g_carAxis : g_footAxis, g_axis, sizeof(g_axis));
        g_aimCar = car;
        if (car && g_carAxis[0].gain == 0.0)
            SeedGains();
        else
            memcpy(g_axis, car ? g_carAxis : g_footAxis, sizeof(g_axis));
        g_haveAngles = 0;
        g_curX = g_curY = 0;
    }
    if (!GetWorld(&world, &player))
    {
        g_crouchPlayer = 0;
        g_crouchDesired = -1;
    }
    if (active)
    {
        UpdateDriverCamera(world, player, car);
        ApplyPlayerCheats(player, car);
    }
    if (g_cfg.stickLook && g_padOk && !g_target)
        AddStickLook(lx, ly, dt);

    held = AimButtonHeld();
    if (!held)
        ReleaseAim();
    else if (!g_held)
    {
        g_held = 1;
        g_acquirePending = 1;
        Log("press: gain=%.6f,%.6f", g_axis[0].gain, g_axis[1].gain);
    }
    if (held && car && g_target)
    {
        int flick = StickFlick();
        int heightFlick = AimHeightFlick();
        if (flick)
            g_driveSwitch = flick > 0 ? 1 : 2;
        else if (heightFlick)
            g_driveSwitch = heightFlick > 0 ? 3 : 4;
    }
    if (held && !car)
    {
        int flick = StickFlick();
        int heightFlick = AimHeightFlick();
        if (flick)
        {
            g_switchSide = flick;
            Log("stick flick %s", flick > 0 ? "right" : "left");
        }
        if (heightFlick)
        {
            int nextZone = heightFlick > 0 ? 1 : -1;
            g_aimZone = g_aimZone == nextZone ? 0 : nextZone;
            g_prevError = -1.0f;
            g_remX = g_remY = 0.0f;
            g_losBlockedChecks = 0;
            g_nextLosCheck = 0;
            if (g_aimZone > 0 && g_animatedHeadAim && g_target)
            {
                Vector3 headPoint;
                if (ReadAnimatedHeadPoint(g_target, &headPoint))
                    Log("aim point head (animated skeleton, forward offset %.0f cm)",
                        g_animatedHeadForward * 100.0f);
                else
                    Log("aim point head (fallback %.0f cm)", AimPointHeightForTarget(g_target) * 100.0f);
            }
            else
                Log("aim point %s (%.0f cm)", g_aimZone > 0 ? "head" :
                    (g_aimZone < 0 ? "lower torso" : "torso"),
                    AimPointHeightForTarget(g_target) * 100.0f);
        }
    }

    if (!active)
    {
        ReleaseAim();
        g_haveAngles = 0;
        g_curX = g_curY = 0;
        return;
    }
    if (car)
    {
        DriveByView view;
        if (!held || !ReadDriveByView(player, &view))
        {
            ReleaseAim();
            g_haveAngles = 0;
            g_curX = g_curY = 0;
            return;
        }
        cameraPosition = view.cameraPosition;
        cameraForward = view.aimDirection;
    }
    else if (!GetWorld(&world, &player) || !ReadCamera(&cameraPosition, &cameraForward))
    {
        ReleaseAim();
        g_haveAngles = 0;
        g_curX = g_curY = 0;
        return;
    }
    yaw = atan2f(cameraForward.x, cameraForward.z);
    pitch = asinf(ClampUnit(cameraForward.y));

    if (now - g_lastStep >= MIN_STEP_MS)
    {
        float turnYaw = 0.0f, turnPitch = 0.0f;
        if (g_haveAngles)
        {
            turnYaw = WrapAngle(yaw - g_prevYaw);
            turnPitch = pitch - g_prevPitch;
            if (!car)
            {
                EstimateAxis(&g_axis[0], "x", turnYaw, g_curX);
                EstimateAxis(&g_axis[1], "y", turnPitch, g_curY);
            }
        }
        g_prevYaw = yaw;
        g_prevPitch = pitch;
        g_haveAngles = 1;
        g_curX = g_curY = 0;
        g_lastStep = now;
        if (held)
            RunAim(lx, ly, world, player, cameraPosition, cameraForward, yaw, pitch, turnYaw,
                   turnPitch, now);
    }
    g_curX += *lx;
    g_curY += *ly;
}

__declspec(dllexport) void __cdecl AimInit(void)
{
    LoadGains();
    Log("Mafia 1.0 logic loaded: gain=%.6f,%.6f", g_axis[0].gain, g_axis[1].gain);
}

__declspec(dllexport) void __cdecl AimMouse(LONG *lx, LONG *ly)
{
    HandleMouse(lx, ly);
}
