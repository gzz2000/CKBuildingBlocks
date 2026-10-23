#include "PhysicsSnapshot.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#include "CKContext.h"
#include "CK3dEntity.h"
#include "CKBehavior.h"
#include "CKIpionManager.h"
#include "PhysicsContact.h"

namespace {
struct PhysicsObjectState {
    struct ContactOutputState {
        CKBOOL active = FALSE;
        int number = 0;
    };
    struct ContactRecordState {
        int index = 0;
        double age = 0.0;
    };

    CK_ID entityId = 0;
    CK3dEntity *identity = NULL;
    IVP_U_Quat rotation;
    IVP_U_Point position;
    IVP_U_Float_Point speed;
    IVP_U_Float_Point rotationSpeed;
    IVP_U_Float_Point speedChange;
    IVP_U_Float_Point rotationSpeedChange;
    IVP_Movement_Type movement = IVP_MT_UNDEFINED;
    IVP_BOOL collisionEnabled = IVP_FALSE;
    CKBOOL hasContactData = FALSE;
    CK_ID contactBehaviorId = 0;
    float contactDelayStart = 0.0f;
    float contactDelayEnd = 0.0f;
    std::vector<ContactOutputState> contactOutputs;
    std::vector<ContactRecordState> contactRecords;
};

struct PhysicsSnapshot {
    std::vector<PhysicsObjectState> objects;
};

CKIpionManager *GetPhysicsManager(CKContext *context)
{
    return context ? CKIpionManager::GetManager(context) : NULL;
}

CKBehavior *FindTargetBehavior(CKContext *context, CK3dEntity *entity,
                               const CKGUID &guid)
{
    const XObjectPointerArray &behaviors =
        context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    for (XObjectPointerArray::ConstIterator it = behaviors.Begin();
         it != behaviors.End(); ++it)
    {
        CKBehavior *behavior = static_cast<CKBehavior *>(*it);
        if (behavior && behavior->GetPrototypeGuid() == guid &&
            behavior->GetTarget() == entity)
            return behavior;
    }
    return NULL;
}

int ExecuteBehaviorFunction(CKContext *context, CKBehavior *behavior,
                            int input)
{
    if (!context || !behavior || !behavior->GetFunction())
        return CKBR_GENERICERROR;

    behavior->ActivateInput(input, TRUE);
    CKBehaviorContext behaviorContext;
    behaviorContext.Behavior = behavior;
    behaviorContext.DeltaTime = 0.0f;
    behaviorContext.Context = context;
    behaviorContext.CurrentLevel = context->GetCurrentLevel();
    behaviorContext.CurrentScene = context->GetCurrentScene();
    behaviorContext.PreviousScene = NULL;
    behaviorContext.CurrentRenderContext = context->GetPlayerRenderContext();
    behaviorContext.ParameterManager = context->GetParameterManager();
    behaviorContext.MessageManager = context->GetMessageManager();
    behaviorContext.AttributeManager = context->GetAttributeManager();
    behaviorContext.TimeManager = context->GetTimeManager();
    behaviorContext.CallbackMessage = 0;
    behaviorContext.CallbackArg = NULL;
    return behavior->GetFunction()(behaviorContext);
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
            state.identity = entity;
            object->get_quat_world_f_object_AT(&state.rotation, &state.position);
            state.speed = core->speed;
            state.rotationSpeed = core->rot_speed;
            state.speedChange = core->speed_change;
            state.rotationSpeedChange = core->rot_speed_change;
            state.movement = object->get_movement_state();
            state.collisionEnabled = object->is_collision_detection_enabled();
            if (PhysicsContactData *data = physics->m_ContactData)
            {
                state.hasContactData = TRUE;
                state.contactBehaviorId = data->m_BehaviorID;
                state.contactDelayStart = data->m_TimeDelayStart;
                state.contactDelayEnd = data->m_TimeDelayEnd;
                state.contactOutputs.reserve(data->m_GroupOutputCount);
                for (int i = 0; i < data->m_GroupOutputCount; ++i)
                {
                    PhysicsObjectState::ContactOutputState output;
                    output.active = data->m_GroupOutputs[i].active;
                    output.number = data->m_GroupOutputs[i].number;
                    state.contactOutputs.push_back(output);
                }

                PhysicsContactManager *contacts = manager->GetContactManager();
                const IVP_Time now = manager->GetSimulationTime();
                for (int i = 0; contacts && i < contacts->GetRecordCount(); ++i)
                {
                    PhysicsContactRecord *record = contacts->GetRecord(i);
                    if (record && record->m_PhysicsObject == physics)
                    {
                        PhysicsObjectState::ContactRecordState savedRecord;
                        savedRecord.index = record->m_Index;
                        savedRecord.age = now - record->m_Time;
                        state.contactRecords.push_back(savedRecord);
                    }
                }
            }
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

    // Delayed continuous-contact records belong to the current timeline and
    // contain direct PhysicsObject pointers. They must not survive topology
    // reconciliation; otherwise a later Process() can dereference a removed
    // object even though every visible entity was restored correctly.
    if (manager->GetContactManager())
        manager->GetContactManager()->ClearRecords();

    std::unordered_map<CK_ID, CK3dEntity *> savedEntities;
    for (const PhysicsObjectState &state : saved->objects)
        savedEntities[state.entityId] = state.identity;

    // Physics presence is part of gameplay state. Ball transformations,
    // collectibles and checkpoint scripts physicalize/unphysicalize entities;
    // merely rewinding the objects that still happen to exist leaves both old
    // and new balls registered and eventually corrupts listener ownership.
    const XObjectPointerArray &entities =
        context->GetObjectListByType(CKCID_3DENTITY, TRUE);
    std::vector<CK3dEntity *> currentEntities;
    currentEntities.reserve(entities.Size());
    for (XObjectPointerArray::ConstIterator it = entities.Begin();
         it != entities.End(); ++it)
        currentEntities.push_back(static_cast<CK3dEntity *>(*it));
    for (CK3dEntity *entity : currentEntities)
    {
        PhysicsObject *physics = manager->GetPhysicsObject(entity, FALSE);
        const auto savedEntity = savedEntities.find(entity->GetID());
        if (physics && physics->m_RealObject &&
            (savedEntity == savedEntities.end() || savedEntity->second != entity))
            physics->m_RealObject->delete_silently();
    }

    for (const PhysicsObjectState &state : saved->objects)
    {
        CKObject *candidate = context->GetObject(state.entityId);
        if (!candidate || candidate != state.identity ||
            !CKIsChildClassOf(candidate, CKCID_3DENTITY))
            continue;

        CK3dEntity *entity = static_cast<CK3dEntity *>(candidate);
        PhysicsObject *physics = manager->GetPhysicsObject(entity, FALSE);
        if (!physics)
        {
            static const CKGUID physicalizeGuid(0x7522370e, 0x37ec15ec);
            CKBehavior *physicalize = FindTargetBehavior(
                context, entity, physicalizeGuid);
            if (physicalize)
                ExecuteBehaviorFunction(context, physicalize, 0);
            physics = manager->GetPhysicsObject(entity, FALSE);
        }
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

        // A recreated physics object no longer has the continuous-contact
        // listener that drives Ballance's collision scripts. Re-arm it from
        // the corresponding building block; the callback creates its owned
        // data at the next safe pre-simulation boundary.
        if (!physics->m_ContactData && state.hasContactData)
        {
            CKBehavior *contact = CKBehavior::Cast(
                context->GetObject(state.contactBehaviorId));
            if (!contact || contact->GetTarget() != entity)
            {
                static const CKGUID continuousContactGuid(0x199e4cf1, 0x545a78fe);
                contact = FindTargetBehavior(context, entity, continuousContactGuid);
            }
            if (contact)
                CreatePhysicsContinuousContactData(manager, physics, contact);
        }
    }

    // Physicalization may create/delete broad-phase objects and invalidate
    // collision caches belonging to objects restored earlier in the loop.
    // Once topology is final, commit every saved pose a second time so IVP's
    // collision world and the CK render entity receive one authoritative,
    // common transform.
    for (const PhysicsObjectState &state : saved->objects)
    {
        CKObject *candidate = context->GetObject(state.entityId);
        if (!candidate || candidate != state.identity ||
            !CKIsChildClassOf(candidate, CKCID_3DENTITY))
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

    // Restore continuous-contact state only after all PhysicsObject table
    // entries are stable. Records use relative age so frame-cap changes and
    // time spent in the restore operation cannot make them fire early.
    const IVP_Time contactNow = manager->GetSimulationTime();
    PhysicsContactManager *contacts = manager->GetContactManager();
    for (const PhysicsObjectState &state : saved->objects)
    {
        CKObject *candidate = context->GetObject(state.entityId);
        if (!candidate || candidate != state.identity ||
            !CKIsChildClassOf(candidate, CKCID_3DENTITY))
            continue;
        PhysicsObject *physics = manager->GetPhysicsObject(
            static_cast<CK3dEntity *>(candidate), FALSE);
        if (!physics)
            continue;

        if (!state.hasContactData)
        {
            if (physics->m_ContactData)
            {
                delete physics->m_ContactData;
                physics->m_ContactData = NULL;
            }
            continue;
        }

        PhysicsContactData *data = physics->m_ContactData;
        if (!data || data->m_BehaviorID != state.contactBehaviorId)
            continue;
        data->m_TimeDelayStart = state.contactDelayStart;
        data->m_TimeDelayEnd = state.contactDelayEnd;
        const int outputCount = std::min(
            data->m_GroupOutputCount,
            static_cast<int>(state.contactOutputs.size()));
        for (int i = 0; i < outputCount; ++i)
        {
            data->m_GroupOutputs[i].active = state.contactOutputs[i].active;
            data->m_GroupOutputs[i].number = state.contactOutputs[i].number;
        }
        for (int i = outputCount; i < data->m_GroupOutputCount; ++i)
        {
            data->m_GroupOutputs[i].active = FALSE;
            data->m_GroupOutputs[i].number = 0;
        }
        for (const PhysicsObjectState::ContactRecordState &record : state.contactRecords)
        {
            if (contacts && record.index >= 0 &&
                record.index < data->m_GroupOutputCount)
                contacts->AddRecord(physics, record.index,
                                    contactNow + (-record.age));
        }
    }

    return CK_OK;
}

void TT_PhysicsDestroySnapshot(TT_PhysicsSnapshotHandle snapshot)
{
    delete static_cast<PhysicsSnapshot *>(snapshot);
}
