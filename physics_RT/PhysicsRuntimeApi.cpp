#include "PhysicsRuntimeApi.h"

#include <cstring>

#include "CKIpionManager.h"
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
#include "VxLegacyMatrix.h"
#include "VxLegacyMatrixMultiply.h"
#endif
#ifdef IVP_EMULATE_X87
#include <ivu_x87_mode.hxx>
#endif

namespace {
CKIpionManager *AsPhysicsManager(CKBaseManager *manager)
{
    return static_cast<CKIpionManager *>(manager);
}
}

int TT_PhysicsResetForTas(CKBaseManager *manager, float deltaTimeMs)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->GetEnvironment())
        return 0;
    physics->ResetSimulationClock();
    physics->SetDeltaTimeImmediate(deltaTimeMs);
    return 1;
}

int TT_PhysicsArmClockResetOnNextObject(CKBaseManager *manager)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->GetEnvironment())
        return 0;
    physics->ArmClockResetOnNextPhysicsObject();
    return 1;
}

int TT_PhysicsCaptureLevelBaseline(
    CKBaseManager *manager, TT_PhysicsLevelBaseline *baseline)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->GetEnvironment() || !baseline)
        return 0;
    baseline->timeFactor = physics->GetPhysicsTimeFactor();
    baseline->nextMovementCheck = physics->GetNextMovementCheck();
    baseline->reserved = 0;
    baseline->randomSeed = ivp_srand_read();
    return 1;
}

int TT_PhysicsRestoreLevelBaseline(
    CKBaseManager *manager, const TT_PhysicsLevelBaseline *baseline)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->GetEnvironment() || !baseline)
        return 0;
    physics->SetPhysicsTimeFactor(baseline->timeFactor);
    physics->SetNextMovementCheck(baseline->nextMovementCheck);
    ivp_srand(baseline->randomSeed);
    return 1;
}

int TT_PhysicsSetTimeFactor(CKBaseManager *manager, float timeFactor)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics)
        return 0;
    physics->SetPhysicsTimeFactor(timeFactor);
    return 1;
}

int TT_PhysicsGetTiming(
    CKBaseManager *manager, float *timeFactor, float *deltaTimeMs,
    float *physicsDeltaSeconds, float *psiSeconds, double *simulationSeconds)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->GetEnvironment())
        return 0;
    if (timeFactor) *timeFactor = physics->GetPhysicsTimeFactor();
    if (deltaTimeMs) *deltaTimeMs = physics->GetDeltaTime();
    if (physicsDeltaSeconds) *physicsDeltaSeconds = physics->GetPhysicsDeltaTime();
    if (psiSeconds) *psiSeconds = physics->GetSimulationTimeStep();
    if (simulationSeconds) *simulationSeconds = physics->GetSimulationTime().get_time();
    return 1;
}

int TT_PhysicsSetExternalPostProcess(CKBaseManager *manager, int external)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics)
        return 0;
    physics->SetExternalPostProcess(external ? TRUE : FALSE);
    return 1;
}

int TT_PhysicsRunExternalPostProcess(CKBaseManager *manager)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    if (!physics || !physics->IsExternalPostProcess())
        return 0;
    return physics->RunExternalPostProcess() == CK_OK ? 1 : 0;
}

void *TT_PhysicsGetObject(CKBaseManager *manager, CK3dEntity *entity)
{
    CKIpionManager *physics = AsPhysicsManager(manager);
    return physics && entity ? physics->GetPhysicsObject(entity) : NULL;
}

int TT_PhysicsGetObjectState(void *object, TT_PhysicsObjectState *state)
{
    PhysicsObject *physics = static_cast<PhysicsObject *>(object);
    if (!physics || !state)
        return 0;
    std::memset(state, 0, sizeof(*state));
    VxVector position, angles, velocity, angularVelocity;
    physics->GetPosition(&position, &angles);
    physics->GetVelocity(&velocity, &angularVelocity);
    state->position[0] = position.x;
    state->position[1] = position.y;
    state->position[2] = position.z;
    state->angles[0] = angles.x;
    state->angles[1] = angles.y;
    state->angles[2] = angles.z;
    state->velocity[0] = velocity.x;
    state->velocity[1] = velocity.y;
    state->velocity[2] = velocity.z;
    state->angularVelocity[0] = angularVelocity.x;
    state->angularVelocity[1] = angularVelocity.y;
    state->angularVelocity[2] = angularVelocity.z;
    state->sleeping = physics->m_RealObject &&
        physics->m_RealObject->get_core()->movement_state >= IVP_MT_CALM;
    return 1;
}

int TT_PhysicsGetCoreState(void *object, TT_PhysicsCoreState *state)
{
    PhysicsObject *physics = static_cast<PhysicsObject *>(object);
    if (!physics || !physics->m_RealObject || !state)
        return 0;
    IVP_Core *core = physics->m_RealObject->get_core();
    if (!core)
        return 0;
    std::memset(state, 0, sizeof(*state));
    state->movementState = static_cast<int>(core->movement_state);
    state->timeOfLastPsi = core->time_of_last_psi.get_time();
    std::memcpy(state->position, core->pos_world_f_core_last_psi.k, sizeof(state->position));
    std::memcpy(state->speed, core->speed.k, sizeof(state->speed));
    std::memcpy(state->rotSpeed, core->rot_speed.k, sizeof(state->rotSpeed));
    std::memcpy(state->speedChange, core->speed_change.k, sizeof(state->speedChange));
    std::memcpy(state->rotSpeedChange, core->rot_speed_change.k, sizeof(state->rotSpeedChange));
    std::memcpy(state->deltaPosition, core->delta_world_f_core_psis.k, sizeof(state->deltaPosition));
    state->quaternionLast[0] = core->q_world_f_core_last_psi.x;
    state->quaternionLast[1] = core->q_world_f_core_last_psi.y;
    state->quaternionLast[2] = core->q_world_f_core_last_psi.z;
    state->quaternionLast[3] = core->q_world_f_core_last_psi.w;
    state->quaternionNext[0] = core->q_world_f_core_next_psi.x;
    state->quaternionNext[1] = core->q_world_f_core_next_psi.y;
    state->quaternionNext[2] = core->q_world_f_core_next_psi.z;
    state->quaternionNext[3] = core->q_world_f_core_next_psi.w;
    return 1;
}

int TT_PhysicsSetObjectVelocity(
    void *object, const float *velocity, const float *angularVelocity)
{
    PhysicsObject *physics = static_cast<PhysicsObject *>(object);
    if (!physics)
        return 0;
    VxVector linear;
    VxVector angular;
    const VxVector *linearPtr = NULL;
    const VxVector *angularPtr = NULL;
    if (velocity) {
        linear.Set(velocity[0], velocity[1], velocity[2]);
        linearPtr = &linear;
    }
    if (angularVelocity) {
        angular.Set(angularVelocity[0], angularVelocity[1], angularVelocity[2]);
        angularPtr = &angular;
    }
    physics->SetVelocity(linearPtr, angularPtr);
    return 1;
}

int TT_PhysicsGetRandomSeed()
{
    return ivp_srand_read();
}

void TT_PhysicsSetRandomSeed(int seed)
{
    ivp_srand(seed);
}

int TT_PhysicsSetSolverMode(int mode)
{
#ifdef IVP_EMULATE_X87
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
    static const VxLegacyMatrixBackend backend{ivp_x87_inverse_matrix,ivp_x87_inverse_translation};
    static const VxLegacyExtraBackend extra{ivp_x87_extra_normalize,ivp_x87_extra_rotation,ivp_x87_extra_rotate_vector,ivp_x87_extra_move,ivp_x87_extra_within_distance};
    if (!mode) {
        VxSetLegacyMatrixBackend(nullptr);
        VxSetLegacyOrientationNormalizer(nullptr);
        VxSetLegacyVectorMagnitude(nullptr);
        VxSetLegacyQuaternionFromMatrix(nullptr);
        VxSetLegacyDynamicPosition(nullptr);
        VxSetLegacyExtraBackend(nullptr);
        VxSetLegacyEntityMatrixMultiply(nullptr);
    }
#endif
    ivp_x87_set_enabled(mode != 0);
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
    if (ivp_x87_is_enabled()) {
        VxSetLegacyMatrixBackend(&backend);
        VxSetLegacyOrientationNormalizer(ivp_x87_normalize_orientation);
        VxSetLegacyVectorMagnitude(ivp_x87_vector_magnitude);
        VxSetLegacyQuaternionFromMatrix(ivp_x87_quaternion_from_matrix);
        VxSetLegacyDynamicPosition(ivp_x87_dynamic_position);
        VxSetLegacyExtraBackend(&extra);
        VxSetLegacyEntityMatrixMultiply(LegacyNonAliasedSSEMatrixMultiply);
    }
#endif
    return 1;
#else
    return mode == 0 ? 1 : 0;
#endif
}

int TT_PhysicsGetSolverMode()
{
#ifdef IVP_EMULATE_X87
    return ivp_x87_is_enabled() ? 1 : 0;
#else
    return 0;
#endif
}
