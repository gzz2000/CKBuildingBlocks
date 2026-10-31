#ifndef PHYSICS_RT_PHYSICSRUNTIMEAPI_H
#define PHYSICS_RT_PHYSICSRUNTIMEAPI_H

class CKBaseManager;
class CK3dEntity;

#if defined(_WIN32)
#define TT_PHYSICS_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define TT_PHYSICS_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Stable semantic ABI for tooling.  Unlike CKIpionManager and PhysicsObject,
// these records deliberately contain no C++ containers or IVP pointers.
struct TT_PhysicsLevelBaseline
{
    float timeFactor;
    short nextMovementCheck;
    short reserved;
    int randomSeed;
};

struct TT_PhysicsObjectState
{
    float position[3];
    float angles[3];
    float velocity[3];
    float angularVelocity[3];
    int sleeping;
};

struct TT_PhysicsCoreState
{
    int movementState;
    int reserved;
    double timeOfLastPsi;
    double position[4];
    float speed[4];
    float rotSpeed[4];
    float speedChange[4];
    float rotSpeedChange[4];
    float deltaPosition[4];
    double quaternionLast[4];
    double quaternionNext[4];
};

// Explicit legacy asset-preload operation, never run by ordinary Physicalize.
// Call before retiring the supplied live owners. Status 0 means success;
// 1 invalid request, 2 owner/topology mismatch, 3 unsupported dynamics,
// 4 topology changed while stepping, 5 completion limit reached.
// Rejection during preflight has no simulation side effects. Status 4/5 may
// follow completed steps, which are reported; this is not a rollback API.
struct TT_PhysicsPreloadResultV1
{
    unsigned int size;
    int status;
    unsigned int steps;
    unsigned int objects;
    double timeBefore;
    double timeAfter;
    int seedBefore;
    int seedAfter;
    int countdownBefore;
    int countdownAfter;
};

TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsCompleteFreefallPreloadV1(
    CKBaseManager *manager, CK3dEntity *const *owners, unsigned int count,
    TT_PhysicsPreloadResultV1 *result);

TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsResetForTas(
    CKBaseManager *manager, float deltaTimeMs);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsArmClockResetOnNextObject(
    CKBaseManager *manager);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsCaptureLevelBaseline(
    CKBaseManager *manager, TT_PhysicsLevelBaseline *baseline);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsRestoreLevelBaseline(
    CKBaseManager *manager, const TT_PhysicsLevelBaseline *baseline);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsSetTimeFactor(
    CKBaseManager *manager, float timeFactor);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsGetTiming(
    CKBaseManager *manager, float *timeFactor, float *deltaTimeMs,
    float *physicsDeltaSeconds, float *psiSeconds, double *simulationSeconds);
// Redirect the manager's ordinary CK PostProcess callback to an external
// scheduler (BML+).  This preserves the retail order without patching a C++
// vtable whose private layout differs between the recovered runtimes.
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsSetExternalPostProcess(
    CKBaseManager *manager, int external);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsRunExternalPostProcess(
    CKBaseManager *manager);
TT_PHYSICS_RUNTIME_EXPORT void *TT_PhysicsGetObject(
    CKBaseManager *manager, CK3dEntity *entity);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsGetObjectState(
    void *object, TT_PhysicsObjectState *state);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsGetCoreState(
    void *object, TT_PhysicsCoreState *state);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsSetObjectVelocity(
    void *object, const float *velocity, const float *angularVelocity);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsGetRandomSeed();
TT_PHYSICS_RUNTIME_EXPORT void TT_PhysicsSetRandomSeed(int seed);
// 0 = host-native fast arithmetic, 1 = x87-compatible extended arithmetic.
// This is an execution policy only; no IVP object or snapshot layout changes.
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsSetSolverMode(int mode);
TT_PHYSICS_RUNTIME_EXPORT int TT_PhysicsGetSolverMode();

#endif // PHYSICS_RT_PHYSICSRUNTIMEAPI_H
