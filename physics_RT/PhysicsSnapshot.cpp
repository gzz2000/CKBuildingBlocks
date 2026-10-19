#include "PhysicsSnapshot.h"

#include <memory>
#include <vector>

#include "CKContext.h"
#include "CK3dEntity.h"
#include "CKIpionManager.h"

namespace {
struct PhysicsObjectState {
    CK_ID entityId = 0;
    IVP_U_Quat rotation;
    IVP_U_Point position;
    IVP_U_Float_Point speed;
    IVP_U_Float_Point rotationSpeed;
    IVP_U_Float_Point speedChange;
    IVP_U_Float_Point rotationSpeedChange;
    IVP_Movement_Type movement = IVP_MT_UNDEFINED;
    IVP_BOOL collisionEnabled = IVP_FALSE;
};

struct PhysicsSnapshot {
    std::vector<PhysicsObjectState> objects;
};

CKIpionManager *GetPhysicsManager(CKContext *context)
{
    return context ? CKIpionManager::GetManager(context) : NULL;
}
}

int TT_PhysicsCaptureSnapshot(CKContext *context, TT_PhysicsSnapshotHandle *snapshot)
{
    if (!snapshot)
        return CKERR_INVALIDPARAMETER;
    *snapshot = NULL;

    CKIpionManager *manager = GetPhysicsManager(context);
    if (!manager || !manager->GetEnvironment())
        return CKERR_NOTINITIALIZED;

    try {
        std::unique_ptr<PhysicsSnapshot> result(new PhysicsSnapshot());
        const XObjectPointerArray &entities =
            context->GetObjectListByType(CKCID_3DENTITY, TRUE);
        result->objects.reserve(entities.Size());

        for (XObjectPointerArray::ConstIterator it = entities.Begin();
             it != entities.End(); ++it)
        {
            CK3dEntity *entity = static_cast<CK3dEntity *>(*it);
            PhysicsObject *physics = manager->GetPhysicsObject(entity, FALSE);
            IVP_Real_Object *object = physics ? physics->m_RealObject : NULL;
            IVP_Core *core = object ? object->get_core() : NULL;
            if (!object || !core)
                continue;

            PhysicsObjectState state;
            state.entityId = entity->GetID();
            object->get_quat_world_f_object_AT(&state.rotation, &state.position);
            state.speed = core->speed;
            state.rotationSpeed = core->rot_speed;
            state.speedChange = core->speed_change;
            state.rotationSpeedChange = core->rot_speed_change;
            state.movement = object->get_movement_state();
            state.collisionEnabled = object->is_collision_detection_enabled();
            result->objects.push_back(state);
        }

        *snapshot = result.release();
        return CK_OK;
    }
    catch (...)
    {
        return CKERR_OUTOFMEMORY;
    }
}

int TT_PhysicsRestoreSnapshot(CKContext *context, TT_PhysicsSnapshotHandle snapshot)
{
    CKIpionManager *manager = GetPhysicsManager(context);
    PhysicsSnapshot *saved = static_cast<PhysicsSnapshot *>(snapshot);
    if (!manager || !saved || !manager->GetEnvironment())
        return CKERR_INVALIDPARAMETER;

    for (const PhysicsObjectState &state : saved->objects)
    {
        CKObject *candidate = context->GetObject(state.entityId);
        if (!candidate || !CKIsChildClassOf(candidate, CKCID_3DENTITY))
            continue;

        CK3dEntity *entity = static_cast<CK3dEntity *>(candidate);
        PhysicsObject *physics = manager->GetPhysicsObject(entity, FALSE);
        IVP_Real_Object *object = physics ? physics->m_RealObject : NULL;
        IVP_Core *core = object ? object->get_core() : NULL;
        if (!object || !core)
            continue;

        object->enable_collision_detection(state.collisionEnabled);
        object->beam_object_to_new_position(&state.rotation, &state.position);

        const bool shouldSimulate = state.movement < IVP_MT_NOT_SIM;
        const bool isSimulating = object->get_movement_state() < IVP_MT_NOT_SIM;
        if (shouldSimulate && !isSimulating && !core->physical_unmoveable)
            object->ensure_in_simulation_now();
        else if (!shouldSimulate && isSimulating && !core->physical_unmoveable)
            object->disable_simulation();

        core = object->get_core();
        core->speed = state.speed;
        core->rot_speed = state.rotationSpeed;
        core->speed_change = state.speedChange;
        core->rot_speed_change = state.rotationSpeedChange;
        CKIpionManager::UpdateObjectWorldMatrix(object);
    }

    return CK_OK;
}

void TT_PhysicsDestroySnapshot(TT_PhysicsSnapshotHandle snapshot)
{
    delete static_cast<PhysicsSnapshot *>(snapshot);
}
