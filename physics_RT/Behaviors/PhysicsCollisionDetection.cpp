/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		     Physics Collision Detection
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"
#include "PhysicsCollisionDetectionCheckpoint.h"

CKObjectDeclaration *FillBehaviorPhysicsCollDetectionDecl();

CKERROR CreatePhysicsCollDetectionProto(CKBehaviorPrototype **pproto);

int PhysicsCollDetection(const CKBehaviorContext &behcontext);

CKERROR PhysicsCollDetectionCallBack(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorPhysicsCollDetectionDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("PhysicsCollDetection");
    od->SetDescription("PhysicsCollDetection");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x7435488d, 0x201d1188));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010001);
    od->SetCreationFunction(CreatePhysicsCollDetectionProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}

CKERROR CreatePhysicsCollDetectionProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("PhysicsCollDetection");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("Create");
    proto->DeclareInput("Stop");

    proto->DeclareOutput("Collision");

    proto->DeclareInParameter("Min Speed m/s", CKPGUID_FLOAT, "0.3");
    proto->DeclareInParameter("Max Speed m/s", CKPGUID_FLOAT, "10.0");
    proto->DeclareInParameter("Sleep afterwards", CKPGUID_FLOAT, "0.5");
    proto->DeclareInParameter("Collision ID", CKPGUID_INT, "1");

    proto->DeclareOutParameter("Entity", CKPGUID_3DENTITY);
    proto->DeclareOutParameter("Speed (0-1)", CKPGUID_FLOAT);
    proto->DeclareOutParameter("Collision Normal World", CKPGUID_VECTOR);
    proto->DeclareOutParameter("Position World", CKPGUID_VECTOR);

    proto->DeclareLocalParameter("IVP_Handle", CKPGUID_POINTER);

    proto->DeclareSetting("Use Collision ID", CKPGUID_BOOL, "FALSE");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(PhysicsCollDetection);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);
    proto->SetBehaviorCallbackFct(PhysicsCollDetectionCallBack);

    *pproto = proto;
    return CK_OK;
}

#define MIN_SPEED 0
#define MAX_SPEED 1
#define SLEEP_AFTERWARDS 2
#define COLLISION_ID 3

#define ENTITY 0
#define SPEED 1
#define COLLISION_NORMAL_WORLD 2
#define POSITION_WORLD 3

class PhysicsCollDetectionListener : public IVP_Listener_Collision
{
public:
    PhysicsCollDetectionListener(float sleepAfterwards, float minSpeed, float maxSpeed,
                                 IVP_Real_Object *obj, CKIpionManager *manager, CKBehavior *beh, int collisionID)
        : IVP_Listener_Collision(IVP_LISTENER_COLLISION_CALLBACK_POST_COLLISION |
                                 IVP_LISTENER_COLLISION_CALLBACK_OBJECT_DELETED),
          m_Time(0),
          m_SleepAfterwards(sleepAfterwards),
          m_MinSpeed(minSpeed),
          m_MaxSpeed(maxSpeed),
          m_RealObject(obj),
          m_IpionManager(manager),
          m_BehaviorID(beh ? beh->GetID() : 0),
          m_CollisionID(collisionID)
    {
        if (obj)
            obj->add_listener_collision(this);
    }

    ~PhysicsCollDetectionListener()
    {
        if (m_RealObject)
            m_RealObject->remove_listener_collision(this);

        CKBehavior *beh = GetBehavior();
        if (beh)
        {
            PhysicsCollDetectionListener *listener = NULL;
            beh->SetLocalParameterValue(0, &listener);
        }
    }

    CKBehavior *GetBehavior() const
    {
        if (!m_IpionManager || !m_IpionManager->m_Context || m_BehaviorID == 0)
            return NULL;

        CKObject *obj = m_IpionManager->m_Context->GetObject(m_BehaviorID);
        if (!obj || !CKIsChildClassOf(obj, CKCID_BEHAVIOR))
            return NULL;

        return (CKBehavior *)obj;
    }

    void event_post_collision(IVP_Event_Collision *collision)
    {
        CKBehavior *beh = GetBehavior();
        if (!beh)
            return;

        IVP_Contact_Situation *situation = collision->contact_situation;
        IVP_Real_Object *obj = situation->objects[0];
        if (obj == m_RealObject)
            obj = situation->objects[1];

        CK3dEntity *ent = (CK3dEntity *)obj->client_data;

        int collisionID = m_IpionManager->GetCollisionDetectID(ent);

        CKBOOL useCollisionID = FALSE;
        beh->GetLocalParameterValue(1, &useCollisionID);
        if (useCollisionID && m_CollisionID != collisionID)
            return;

        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent);
        if (!po)
            return;

        if (beh->IsOutputActive(0) ||
            m_IpionManager->GetSimulationTime() - m_Time < m_SleepAfterwards)
            return;

        double sl = situation->speed.real_length();
        if (sl <= m_MinSpeed)
            return;

        float speed;
        if (sl < 0.0001f)
        {
            speed = 0.0001f;
        }
        else if (m_MaxSpeed < 0.0001f)
        {
            speed = 1.0f;
        }
        else
        {
            speed = (float)sl / m_MaxSpeed;
            if (speed > 1.0f)
                speed = 1.0f;
        }

        beh->SetOutputParameterObject(ENTITY, ent);
        beh->SetOutputParameterValue(SPEED, &speed);

        const float normalSign = (situation->objects[0] != m_RealObject) ? -1.0f : 1.0f;
        VxVector collisionNormalWorld((float)situation->surf_normal.k[0] * normalSign,
                                      (float)situation->surf_normal.k[1] * normalSign,
                                      (float)situation->surf_normal.k[2] * normalSign);
        beh->SetOutputParameterValue(COLLISION_NORMAL_WORLD, &collisionNormalWorld);

        VxVector positionWorld((float)situation->contact_point_ws.k[0],
                               (float)situation->contact_point_ws.k[1],
                               (float)situation->contact_point_ws.k[2]);
        beh->SetOutputParameterValue(POSITION_WORLD, &positionWorld);

        m_Time = m_IpionManager->GetSimulationTime();

        beh->ActivateOutput(0, TRUE);
    }

    void event_collision_object_deleted(IVP_Real_Object *obj)
    {
        if (obj == m_RealObject)
        {
            m_RealObject->remove_listener_collision(this);
            m_RealObject = NULL;
        }
    }

    IVP_Time m_Time;
    float m_SleepAfterwards;
    float m_MinSpeed;
    float m_MaxSpeed;
    IVP_Real_Object *m_RealObject;
    CKIpionManager *m_IpionManager;
    CK_ID m_BehaviorID;
    int m_CollisionID;
    int field_2C;
};

IVP_Listener_Collision *GetPhysicsCollisionDetectionListener(CKBehavior *behavior)
{
    PhysicsCollDetectionListener *listener = nullptr;
    if (behavior) behavior->GetLocalParameterValue(0, &listener);
    return listener;
}

bool CapturePhysicsCollisionDetection(CKIpionManager *manager, CKBehavior *behavior,
                                     PhysicsCollisionDetectionState &state)
{
    if (!manager || !behavior)
        return false;
    PhysicsCollDetectionListener *listener = nullptr;
    behavior->GetLocalParameterValue(0, &listener);
    if (!listener || listener->m_IpionManager != manager ||
        listener->m_BehaviorID != behavior->GetID())
        return false;
    CK3dEntity *body = listener->m_RealObject
        ? static_cast<CK3dEntity *>(listener->m_RealObject->client_data) : nullptr;
    if (listener->m_RealObject) {
        auto *host = body ? manager->GetPhysicsObject(body, FALSE) : nullptr;
        if (!host || host->m_RealObject != listener->m_RealObject)
            return false;
    }
    state.body = body;
    state.bodyId = body ? body->GetID() : 0;
    state.lastCollisionTime = listener->m_Time.get_time();
    state.sleepAfterwards = listener->m_SleepAfterwards;
    state.minSpeed = listener->m_MinSpeed;
    state.maxSpeed = listener->m_MaxSpeed;
    state.collisionId = listener->m_CollisionID;
    return true;
}

bool RestorePhysicsCollisionDetection(CKIpionManager *manager, CKBehavior *behavior,
                                     const PhysicsCollisionDetectionState &state, bool create)
{
    if (!manager || !behavior || !manager->m_Context ||
        manager->m_Context->GetObject(behavior->GetID()) != behavior ||
        (state.bodyId == 0) != (state.body == nullptr))
        return false;
    IVP_Real_Object *object = nullptr;
    if (state.body) {
        if (manager->m_Context->GetObject(state.bodyId) != state.body)
            return false;
        auto *host = manager->GetPhysicsObject(state.body, FALSE);
        if (!host || !host->m_RealObject)
            return false;
        object = host->m_RealObject;
    }
    PhysicsCollDetectionListener *listener = nullptr;
    behavior->GetLocalParameterValue(0, &listener);
    if (create) {
        if (listener)
            return false;
        listener = new PhysicsCollDetectionListener(state.sleepAfterwards, state.minSpeed,
            state.maxSpeed, object, manager, behavior, state.collisionId);
        behavior->SetLocalParameterValue(0, &listener);
    } else if (!listener || listener->m_RealObject != object ||
               listener->m_IpionManager != manager || listener->m_BehaviorID != behavior->GetID()) {
        return false;
    }
    listener->m_Time = IVP_Time(state.lastCollisionTime);
    listener->m_SleepAfterwards = state.sleepAfterwards;
    listener->m_MinSpeed = state.minSpeed;
    listener->m_MaxSpeed = state.maxSpeed;
    listener->m_CollisionID = state.collisionId;
    return true;
}

class PhysicsCollDetectionCallback : public PhysicsCallback
{
public:
    PhysicsCollDetectionCallback(CKIpionManager *pm, CKBehavior *beh) : PhysicsCallback(pm, beh, 2) {}

    PhysicsCallback *CloneForSnapshot() const override
    {
        // This callback has no payload beyond manager/type/behavior identity.
        return new PhysicsCollDetectionCallback(*this);
    }

    virtual int Execute()
    {
        CKBehavior *beh = GetBehavior();
        if (!beh)
            return 1;

        CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
        if (!ent)
            return 1;

        float minSpeed = 0.3f;
        beh->GetInputParameterValue(MIN_SPEED, &minSpeed);

        float maxSpeed = 0.3f;
        beh->GetInputParameterValue(MAX_SPEED, &maxSpeed);

        float sleepAfterwards = 0.5f;
        beh->GetInputParameterValue(SLEEP_AFTERWARDS, &sleepAfterwards);

        int collisionID = 1;
        beh->GetInputParameterValue(COLLISION_ID, &collisionID);

        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent, TRUE);
        if (!po)
            return 0;

        IVP_Real_Object *obj = po->m_RealObject;

        PhysicsCollDetectionListener *listener = new PhysicsCollDetectionListener(sleepAfterwards, minSpeed, maxSpeed,
                                                                                  obj, m_IpionManager, beh,
                                                                                  collisionID);

        beh->SetLocalParameterValue(0, &listener);

        return 1;
    }
};

int PhysicsCollDetection(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    if (beh->IsInputActive(0))
    {
        PhysicsCollDetectionListener *listener = NULL;
        beh->GetLocalParameterValue(0, &listener);
        if (!listener)
        {
            CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
            if (!ent)
                return CKBR_OWNERERROR;

            CKIpionManager *man = CKIpionManager::GetManager(context);
            if (!man || !man->GetEnvironment() || !man->m_PreSimulateCallbacks)
                return CKBR_GENERICERROR;
            man->GetPhysicsObject(ent, TRUE);

            PhysicsCollDetectionCallback *physicsCall = new PhysicsCollDetectionCallback(man, beh);
            man->m_PreSimulateCallbacks->Process(physicsCall);
        }

        beh->ActivateInput(0, FALSE);
        return CKBR_ACTIVATENEXTFRAME;
    }
    else if (beh->IsInputActive(1))
    {
        PhysicsCollDetectionListener *listener = NULL;
        beh->GetLocalParameterValue(0, &listener);
        if (listener)
        {
            delete listener;
        }

        beh->ActivateInput(1, FALSE);
        return CKBR_OK;
    }

    return CKBR_ACTIVATENEXTFRAME;
}

CKERROR PhysicsCollDetectionCallBack(const CKBehaviorContext &behcontext)
{
    if (behcontext.CallbackMessage == CKM_BEHAVIORRESET ||
        behcontext.CallbackMessage == CKM_BEHAVIORDELETE ||
        behcontext.CallbackMessage == CKM_BEHAVIORDETACH)
    {
        CKBehavior *beh = behcontext.Behavior;
        PhysicsCollDetectionListener *listener = NULL;
        beh->GetLocalParameterValue(0, &listener);
        CKIpionManager *man = CKIpionManager::GetManager(behcontext.Context);
        if (listener && man && man->GetEnvironment())
            delete listener;
        else
        {
            void *handle = NULL;
            beh->SetLocalParameterValue(0, &handle);
        }

        listener = NULL;
        beh->SetLocalParameterValue(0, &listener);
    }

    return CKBR_OK;
}
