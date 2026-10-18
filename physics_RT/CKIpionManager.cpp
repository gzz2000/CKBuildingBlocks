#include "CKIpionManager.h"

#include "CKTimeManager.h"
#include "CKMesh.h"
#include "CK3dEntity.h"
#include "CK3dObject.h"
#include "CKParameterOut.h"
#include "VxMatrix.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "ivp_performancecounter.hxx"
#include "ivp_surbuild_pointsoup.hxx"
#include "ivp_surman_polygon.hxx"

static bool IsPhysicsHandleBehavior(CKGUID guid)
{
    return guid == CKGUID(0x5e624f0a, 0x35160450) || // Set Physics Ball Joint
           guid == CKGUID(0x7435488d, 0x201d1188) || // PhysicsCollDetection
           guid == CKGUID(0x56e20c57, 0xb926068) ||  // SetPhysicsForce
           guid == CKGUID(0x41cd3653, 0x0de60c1d) || // Set Physics Hinge
           guid == CKGUID(0x2973360e, 0x23d31aa7) || // Set Physics Slider
           guid == CKGUID(0x24a06a3a, 0x07100fce) || // Set Physics Spring
           guid == CKGUID(0x199e4cf1, 0x545a78fe);   // PhysicsContinuousContact
}

static bool IsZeroVector(const VxVector &v)
{
    return fabsf(v.x) <= 0.0001f && fabsf(v.y) <= 0.0001f && fabsf(v.z) <= 0.0001f;
}

static void DeleteCollisionSurfaceOwner(PhysicsCollisionSurface *surface)
{
    if (!surface)
        return;

    delete surface->m_SurfaceManager;
    if (surface->m_CompactSurface)
        ivp_free_aligned(surface->m_CompactSurface);
    delete surface;
}

class PhysicsObjectListener : public IVP_Listener_Object
{
public:
    explicit PhysicsObjectListener(CKIpionManager *man) : m_IpionManager(man) {}

    virtual void event_object_deleted(IVP_Event_Object *object)
    {
        IVP_Real_Object *obj = object->real_object;

        if (obj->get_movement_state() != IVP_MT_STATIC)
        {
            int i = m_IpionManager->m_MovableObjects.index_of(obj);
            if (i != -1)
                m_IpionManager->m_MovableObjects.remove_at(i);
        }

        CK3dEntity *entity = (CK3dEntity *)obj->client_data;
        if (!entity)
            return;

        PhysicsObject *po = m_IpionManager->GetPhysicsObject(entity);
        if (po && po->m_ContactData)
        {
            delete po->m_ContactData;
            po->m_ContactData = NULL;
        }

        m_IpionManager->RemovePhysicsObject(entity);
    }

    virtual void event_object_created(IVP_Event_Object *object) {}

    virtual void event_object_revived(IVP_Event_Object *object)
    {
        IVP_Real_Object *obj = object->real_object;
        if (obj->get_movement_state() != IVP_MT_STATIC)
        {
            m_IpionManager->m_MovableObjects.add(obj);
        }
    }

    virtual void event_object_frozen(IVP_Event_Object *object)
    {
        IVP_Real_Object *obj = object->real_object;
        if (obj->get_movement_state() != IVP_MT_STATIC)
        {
            int i = m_IpionManager->m_MovableObjects.index_of(obj);
            if (i != -1)
                m_IpionManager->m_MovableObjects.remove_at(i);
        }
    }

private:
    CKIpionManager *m_IpionManager;
};

class PhysicsCollisionListener : public IVP_Listener_Collision
{
public:
    explicit PhysicsCollisionListener(CKIpionManager *man)
        : IVP_Listener_Collision(IVP_LISTENER_COLLISION_CALLBACK_POST_COLLISION |
                                 IVP_LISTENER_COLLISION_CALLBACK_FRICTION),
          m_IpionManager(man) {}

    virtual void event_friction_created(IVP_Event_Friction *friction)
    {
        if (!friction)
            return;

        IVP_Contact_Situation *situation = friction->contact_situation;

        CK3dEntity *entity1 = (CK3dEntity *)situation->objects[0]->client_data;
        if (!entity1)
            return;

        PhysicsObject *po1 = m_IpionManager->GetPhysicsObject(entity1);
        if (!po1)
            return;

        if (po1->m_FrictionCount == 0)
            po1->m_FrictionTime = friction->environment->get_current_time();
        ++po1->m_FrictionCount;

        CK3dEntity *entity2 = (CK3dEntity *)situation->objects[1]->client_data;
        if (!entity2)
            return;

        PhysicsObject *po2 = m_IpionManager->GetPhysicsObject(entity2);
        if (!po2)
            return;

        if (po2->m_FrictionCount == 0)
            po2->m_FrictionTime = friction->environment->get_current_time();
        ++po2->m_FrictionCount;
    }

    virtual void event_friction_deleted(IVP_Event_Friction *friction)
    {
        if (!friction)
            return;

        IVP_Contact_Situation *situation = friction->contact_situation;

        CK3dEntity *entity1 = (CK3dEntity *)situation->objects[0]->client_data;
        if (!entity1)
            return;

        PhysicsObject *po1 = m_IpionManager->GetPhysicsObject(entity1);
        if (!po1)
            return;

        --po1->m_FrictionCount;

        CK3dEntity *entity2 = (CK3dEntity *)situation->objects[1]->client_data;
        if (!entity2)
            return;

        PhysicsObject *po2 = m_IpionManager->GetPhysicsObject(entity2);
        if (!po2)
            return;

        --po2->m_FrictionCount;
    }

private:
    CKIpionManager *m_IpionManager;
};

CKIpionManager::CKIpionManager(CKContext *context)
    : CKBaseManager(context, TT_PHYSICS_MANAGER_GUID, "TT Physics Manager")
{
    m_HasPhysicsCalls = 0;
    m_PhysicalizeCalls = 0;
    m_DePhysicalizeCalls = 0;
    m_HasPhysicsTime = 0.0f;
    m_DePhysicalizeTime = 0.0f;
    field_FC = 0.0f;
    field_104 = 0.0f;

    m_CollisionFilterExclusivePair = NULL;
    m_PreSimulateCallbacks = NULL;
    m_PostSimulateCallbacks = NULL;
    m_ContactManager = NULL;
    m_CollisionListener = NULL;
    m_ObjectListener = NULL;
    m_Environment = NULL;
    m_TimeManager = NULL;
    m_DeltaTime = 0.0f;
    m_PhysicsDeltaTime = 0.0f;
    m_CallbackProcessingDepth = 0;
    m_ResetRequested = FALSE;

    if (context->RegisterNewManager(this) == CKERR_MANAGERALREADYEXISTS)
        context->OutputToConsole("Manager already exists", TRUE);

    m_CollisionSurfaces = NULL;
    m_CollDetectionIDAttribType = -1;
}

CKIpionManager::~CKIpionManager()
{
    DestroyEnvironment();
    DeleteCollisionSurfaces();
    ClearLiquidSurfaces();
}

CKERROR CKIpionManager::OnCKInit()
{
    m_CollisionSurfaces = new IVP_U_String_Hash(64);

    m_TimeManager = m_Context->GetTimeManager();
    m_PhysicsTimeFactor = 0.001f;
    ResetProfiler();

    return CK_OK;
}

CKERROR CKIpionManager::OnCKEnd()
{
    DestroyEnvironment();
    DeleteCollisionSurfaces();
    ClearLiquidSurfaces();

    m_TimeManager = NULL;

    return CK_OK;
}

CKERROR CKIpionManager::OnCKPlay()
{
    if (m_Context->IsReseted())
        CreateEnvironment();

    return CK_OK;
}

CKERROR CKIpionManager::OnCKReset()
{
    return CK_OK;
}

CKERROR CKIpionManager::OnCKPostReset()
{
    DestroyEnvironment();

    m_PhysicsTimeFactor = 0.001f;
    m_PhysicsObjects.Clear();

    ClearCollisionSurfaces();
    ClearLiquidSurfaces();

    return CK_OK;
}

CKERROR CKIpionManager::PostClearAll()
{
    DestroyEnvironment();

    m_PhysicsTimeFactor = 0.001f;
    m_PhysicsObjects.Clear();

    ClearCollisionSurfaces();
    ClearLiquidSurfaces();

    return CK_OK;
}

CKERROR CKIpionManager::PostProcess()
{
    Simulate(m_TimeManager->GetLastDeltaTime());

    return CK_OK;
}

CKERROR CKIpionManager::SequenceToBeDeleted(CK_ID *objids, int count)
{
    for (int i = 0; i < count; ++i)
    {
        CKObject *obj = m_Context->GetObject(objids[i]);
        if (CKIsChildClassOf(obj, CKCID_3DENTITY))
        {
            CK3dEntity *ent = (CK3dEntity *)obj;
            PhysicsObject *po = GetPhysicsObject(ent);
            if (po)
            {
                IVP_Real_Object *realObject = po->m_RealObject;
                if (realObject)
                    realObject->delete_silently();
                RemovePhysicsObject(ent);

                if (realObject && m_MovableObjects.index_of(realObject) != -1)
                    m_MovableObjects.remove(realObject);
            }
        }
        else if (CKIsChildClassOf(obj, CKCID_BEHAVIOR))
        {
            CKBehavior *beh = (CKBehavior *)obj;
            ClearBehaviorCallbacks(beh->GetID());
            if (IsPhysicsHandleBehavior(beh->GetPrototypeGuid()))
                beh->CallCallbackFunction(CKM_BEHAVIORRESET);
        }
    }

    return CK_OK;
}

void CKIpionManager::Reset()
{
    if (m_CallbackProcessingDepth > 0)
    {
        m_ResetRequested = TRUE;
        return;
    }

    DestroyEnvironment();

    m_PhysicsTimeFactor = 0.001f;
    m_DeltaTime = 0.0f;
    m_PhysicsDeltaTime = 0.0f;
    m_ResetRequested = FALSE;
    m_PhysicsObjects.Clear();
    ClearCollisionSurfaces();
    ClearLiquidSurfaces();

    CreateEnvironment();
}

CKBOOL CKIpionManager::ProcessPendingReset()
{
    if (!m_ResetRequested || m_CallbackProcessingDepth > 0)
        return FALSE;

    Reset();
    return TRUE;
}

void CKIpionManager::ResetPhysicsBehaviorHandles()
{
    if (!m_Context)
        return;

    CK_ID *ids = m_Context->GetObjectsListByClassID(CKCID_BEHAVIOR);
    const int count = m_Context->GetObjectsCountByClassID(CKCID_BEHAVIOR);

    for (int i = 0; i < count; ++i)
    {
        CKBehavior *beh = (CKBehavior *)m_Context->GetObject(ids[i]);
        if (!beh)
            continue;

        if (IsPhysicsHandleBehavior(beh->GetPrototypeGuid()))
            beh->CallCallbackFunction(CKM_BEHAVIORRESET);
    }
}

void CKIpionManager::ClearBehaviorCallbacks(CK_ID behaviorID)
{
    if (m_PreSimulateCallbacks)
        m_PreSimulateCallbacks->ClearBehavior(behaviorID);
    if (m_PostSimulateCallbacks)
        m_PostSimulateCallbacks->ClearBehavior(behaviorID);
}

int CKIpionManager::GetPhysicsObjectCount() const
{
    return m_PhysicsObjects.Size();
}

PhysicsObject *CKIpionManager::GetPhysicsObject(CK3dEntity *entity, CKBOOL logging)
{
    if (!entity)
        return NULL;

    VxTimeProfiler profiler;

    ++m_HasPhysicsCalls;

    PhysicsObject *obj = NULL;
    PhysicsObjectTable::Iterator it = m_PhysicsObjects.Find(entity->GetID());
    if (it == m_PhysicsObjects.End())
    {
        if (logging)
            m_Context->OutputToConsoleEx("You must Physicalize %s ...", entity->GetName());
    }
    else
    {
        obj = &*it;
    }

    m_DePhysicalizeTime = profiler.Current();
    if (m_DePhysicalizeTime > m_HasPhysicsTime)
        m_HasPhysicsTime = m_DePhysicalizeTime;

    return obj;
}

void CKIpionManager::RemovePhysicsObject(CK3dEntity *entity)
{
    if (!entity)
        return;

    const CK_ID owner = entity->GetID();
    m_PhysicsObjects.Remove(owner);
    DeletePrivateCollisionSurface(owner);
    DeleteMaterial(owner);
}

int CKIpionManager::CreatePhysicsObjectOnParameters(CK3dEntity *target, int convexCount, CKMesh **convexes,
                                                    int ballCount, VxVector *ballPositions, float *ballRadii,
                                                    int concaveCount, CKMesh **concaves,
                                                    float ballRadius, CKSTRING collisionSurface,
                                                    VxVector *shiftMassCenter, CKBOOL fixed, IVP_Material *material,
                                                    float mass, CKSTRING collisionGroup, CKBOOL startFrozen,
                                                    CKBOOL enableCollision, CKBOOL autoCalcMassCenter,
                                                    float linearSpeedDampening, float rotSpeedDampening)
{
    if (!target || !m_Environment)
        return CKERR_INVALIDPARAMETER;

    VxVector scale;
    target->GetScale(&scale);

    IVP_Real_Object *obj = NULL;

    if (ballCount > 0 || !collisionSurface || collisionSurface[0] == '\0')
    {
        if (!collisionSurface || collisionSurface[0] == '\0')
            enableCollision = FALSE;

        if (ballCount > 0)
        {
            const float firstRadius = (ballRadii && ballRadii[0] > 0.0f) ? ballRadii[0] : ballRadius;
            const VxVector firstPosition = ballPositions ? ballPositions[0] : VxVector(0.0f, 0.0f, 0.0f);
            if (ballCount == 1 && IsZeroVector(firstPosition))
            {
                obj = CreatePhysicsBall(target->GetName(), mass, firstRadius, material, linearSpeedDampening,
                                        rotSpeedDampening, target, startFrozen, fixed, collisionGroup,
                                        enableCollision, shiftMassCenter);
            }
            else
            {
                obj = CreatePhysicsMultiBall(target->GetName(), mass, ballCount, ballPositions, ballRadii, material,
                                             linearSpeedDampening, rotSpeedDampening, target, startFrozen, fixed,
                                             collisionGroup, enableCollision, shiftMassCenter, &scale);
            }
        }
        else
        {
            obj = CreatePhysicsBall(target->GetName(), mass, ballRadius, material, linearSpeedDampening,
                                    rotSpeedDampening, target, startFrozen, fixed, collisionGroup,
                                    enableCollision, shiftMassCenter);
        }
    }
    else
    {
        // Collision Surface is the caller-provided stable identity. Geometry is
        // only consumed to populate that named cache entry on the first miss.
        IVP_SurfaceManager *surman = GetCollisionSurface(collisionSurface);
        if (!surman)
        {
            IVP_SurfaceBuilder_Ledge_Soup builder;
            int ledgeCount = 0;

            if (convexCount > 0)
            {
                for (int i = 0; i < convexCount; i++)
                {
                    if (convexes[i])
                    {
                        ledgeCount += AddConvexSurface(&builder, convexes[i], &scale);
                    }
                }
            }

            if (concaveCount > 0)
            {
                for (int i = 0; i < concaveCount; i++)
                {
                    if (concaves[i])
                    {
                        AddConcaveSurface(&builder, concaves[i], &scale);
                        ++ledgeCount;
                    }
                }
            }

            if (ledgeCount != 0)
            {
                IVP_Compact_Surface *compactSurface = builder.compile();
                if (compactSurface)
                {
                    surman = new IVP_SurfaceManager_Polygon(compactSurface);
                    if (surman)
                        AddCollisionSurface(collisionSurface, surman, compactSurface);
                    else
                        ivp_free_aligned(compactSurface);
                }
            }
            else
            {
                m_Context->OutputToConsoleEx("Error: incorrect mesh for %s !\n", target->GetName());
            }
        }

        if (!surman)
            return CKERR_INVALIDPARAMETER;

        obj = CreatePhysicsPolygon(target->GetName(), mass, material, linearSpeedDampening, rotSpeedDampening, target,
                                   startFrozen, fixed, collisionGroup, enableCollision, surman, shiftMassCenter);
    }

    if (!obj)
    {
        m_Context->OutputToConsoleEx("Error: failed to create physics object for %s !\n", target->GetName());
        return CKERR_INVALIDPARAMETER;
    }

    obj->client_data = target;

    PhysicsObject po;
    po.m_RealObject = obj;
    m_PhysicsObjects.Insert(target->GetID(), po);

    return CK_OK;
}

IVP_Polygon *CKIpionManager::CreatePhysicsMultiBall(CKSTRING name, float mass, int ballCount,
                                                    VxVector *ballPositions, float *ballRadii,
                                                    IVP_Material *material, float linearSpeedDampening,
                                                    float rotSpeedDampening, CK3dEntity *target,
                                                    CKBOOL startFrozen, CKBOOL fixed, CKSTRING collisionGroup,
                                                    CKBOOL enableCollision, VxVector *shiftMassCenter, VxVector *scale)
{
    if (ballCount <= 0 || !ballPositions || !ballRadii)
        return NULL;

    IVP_SurfaceBuilder_Ledge_Soup builder;
    int ledgeCount = 0;
    for (int i = 0; i < ballCount; ++i)
    {
        ledgeCount += AddBallSurface(&builder, ballPositions[i], ballRadii[i], scale);
    }

    if (ledgeCount == 0)
        return NULL;

    IVP_Compact_Surface *compactSurface = builder.compile();
    if (!compactSurface)
        return NULL;

    IVP_SurfaceManager_Polygon *surman = new IVP_SurfaceManager_Polygon(compactSurface);
    if (!surman)
    {
        ivp_free_aligned(compactSurface);
        return NULL;
    }

    IVP_Polygon *polygon = CreatePhysicsPolygon(name, mass, material, linearSpeedDampening, rotSpeedDampening, target,
                                                startFrozen, fixed, collisionGroup, enableCollision, surman,
                                                shiftMassCenter);
    if (!polygon)
    {
        delete surman;
        ivp_free_aligned(compactSurface);
        return NULL;
    }

    OwnPrivateCollisionSurface(target, surman, compactSurface);
    return polygon;
}

IVP_Ball *CKIpionManager::CreatePhysicsBall(CKSTRING name, float mass, float ballRadius, IVP_Material *material,
                                            float linearSpeedDampening, float rotSpeedDampening,
                                            CK3dEntity *target, CKBOOL startFrozen, CKBOOL fixed,
                                            CKSTRING collisionGroup, CKBOOL enableCollision, VxVector *shiftMassCenter)
{
    IVP_Template_Real_Object objectTemplate;
    IVP_U_Point position;
    IVP_U_Quat quaternion;
    IVP_U_Matrix massCenter;

    FillTemplateInfo(&objectTemplate, &position, &quaternion, name, mass, material, linearSpeedDampening, rotSpeedDampening,
                     target, fixed, collisionGroup, &massCenter, shiftMassCenter);

    IVP_Template_Ball ballTemplate;
    ballTemplate.radius = ballRadius;

    IVP_Ball *ball = m_Environment->create_ball(&ballTemplate, &objectTemplate, &quaternion, &position);
    if (ball)
    {
        if (!startFrozen)
            ball->ensure_in_simulation();
        if (enableCollision)
            ball->enable_collision_detection();
    }

    return ball;
}

IVP_Polygon *CKIpionManager::CreatePhysicsPolygon(CKSTRING name, float mass, IVP_Material *material,
                                                  float linearSpeedDampening, float rotSpeedDampening,
                                                  CK3dEntity *target, CKBOOL startFrozen, CKBOOL fixed,
                                                  CKSTRING collisionGroup, CKBOOL enableCollision,
                                                  IVP_SurfaceManager *surman, VxVector *shiftMassCenter)
{
    IVP_Template_Real_Object objectTemplate;
    IVP_U_Point position;
    IVP_U_Quat quaternion;
    IVP_U_Matrix massCenter;

    FillTemplateInfo(&objectTemplate, &position, &quaternion, name, mass, material, linearSpeedDampening, rotSpeedDampening,
                     target, fixed, collisionGroup, &massCenter, shiftMassCenter);

    IVP_Polygon *polygon = m_Environment->create_polygon(surman, &objectTemplate, &quaternion, &position);
    if (polygon)
    {
        if (!startFrozen)
            polygon->ensure_in_simulation();
        if (enableCollision)
            polygon->enable_collision_detection();
    }

    return polygon;
}

void CKIpionManager::CreateEnvironment()
{
	if (m_Environment)
		return;

    IVP_Application_Environment appEnv;
    appEnv.material_manager = new IVP_Material_Manager(IVP_TRUE);
    appEnv.performancecounter = new IVP_PerformanceCounter_Simple();
    appEnv.env_active_float_manager = new IVP_U_Active_Value_Manager(IVP_TRUE);

    IVP_Collision_Filter_Coll_Group_Ident *groupCollisionFilter = new IVP_Collision_Filter_Coll_Group_Ident(IVP_TRUE);
    m_CollisionFilterExclusivePair = new IVP_Collision_Filter_Exclusive_Pair;

    IVP_Meta_Collision_Filter *collisionFilter = new IVP_Meta_Collision_Filter(IVP_TRUE);
    collisionFilter->add_collision_filter(m_CollisionFilterExclusivePair);
    collisionFilter->add_collision_filter(groupCollisionFilter);
    appEnv.collision_filter = collisionFilter;

    IVP_Environment_Manager *envManager = IVP_Environment_Manager::get_environment_manager();
    m_Environment = envManager->create_environment(&appEnv, "NeMo", 0x7EFAD621);

    IVP_U_Point gravity = IVP_U_Point(0.0, -9.81, 0.0);
    m_Environment->set_gravity(&gravity);

    m_PreSimulateCallbacks = new PhysicsCallbackContainer(this);
    m_PostSimulateCallbacks = new PhysicsCallbackContainer(this);

    m_CollisionListener = new PhysicsCollisionListener(this);
    m_Environment->add_listener_collision_global(m_CollisionListener);

    m_ObjectListener = new PhysicsObjectListener(this);
    m_Environment->add_listener_object_global(m_ObjectListener);

    m_ContactManager = new PhysicsContactManager(this);
    m_ContactManager->SetupContactID();
    SetupCollisionDetectID();
}

void CKIpionManager::DestroyEnvironment(CKBOOL resetBehaviorHandles)
{
    if (resetBehaviorHandles)
        ResetPhysicsBehaviorHandles();

    m_CollisionFilterExclusivePair = NULL;

    if (m_PreSimulateCallbacks)
    {
        delete m_PreSimulateCallbacks;
        m_PreSimulateCallbacks = NULL;
    }

    if (m_PostSimulateCallbacks)
    {
        delete m_PostSimulateCallbacks;
        m_PostSimulateCallbacks = NULL;
    }

    if (m_ObjectListener)
    {
        if (m_Environment)
            m_Environment->remove_listener_object_global(m_ObjectListener);

        delete m_ObjectListener;
        m_ObjectListener = NULL;
    }

    if (m_CollisionListener)
    {
        if (m_Environment)
            m_Environment->remove_listener_collision_global(m_CollisionListener);

        delete m_CollisionListener;
        m_CollisionListener = NULL;
    }

    // Release per-object contact state while the contact manager is still valid.
    m_PhysicsObjects.Clear();

    if (m_ContactManager)
    {
        delete m_ContactManager;
        m_ContactManager = NULL;
    }

    for (int i = m_Entities.len() - 1; i >= 0; --i)
    {
        CK3dEntity *entity = m_Entities.element_at(i);
        m_Context->DestroyObject(entity);
    }
    m_Entities.clear();

    if (m_Environment)
    {
        delete m_Environment;
        m_Environment = NULL;
    }

    DeletePrivateCollisionSurfaces();

    m_MovableObjects.clear();

    DeleteMaterials();

    ClearLiquidSurfaces();
}

void CKIpionManager::Simulate(float deltaTime)
{
    if (ProcessPendingReset())
        return;

    SetDeltaTime(deltaTime);

    // Opt-in wall-clock trace for validating the complete CK -> physics_RT ->
    // IVP time pipeline on native platforms.  It deliberately aggregates one
    // second at a time so the diagnostic does not perturb the simulation.
    const char *tracePath = std::getenv("BALLANCE_PHYSICS_TRACE");
    using TraceClock = std::chrono::steady_clock;
    static CKIpionManager *traceManager = NULL;
    static TraceClock::time_point traceWallStart;
    static float traceSimulationStart = 0.0f;
    static double traceCkMilliseconds = 0.0;
    static double traceSubmittedSeconds = 0.0;
    static unsigned traceCalls = 0;
    const TraceClock::time_point traceNow = TraceClock::now();
    const float simulationBefore = m_Environment
        ? m_Environment->get_current_time().get_time() : 0.0f;
    if (tracePath && *tracePath && traceManager != this)
    {
        traceManager = this;
        traceWallStart = traceNow;
        traceSimulationStart = simulationBefore;
        traceCkMilliseconds = 0.0;
        traceSubmittedSeconds = 0.0;
        traceCalls = 0;
    }
    if (tracePath && *tracePath)
    {
        traceCkMilliseconds += deltaTime;
        traceSubmittedSeconds += m_PhysicsDeltaTime;
        ++traceCalls;
    }

    if (m_Environment)
    {
        if (m_PreSimulateCallbacks->m_HasCallbacks)
            m_PreSimulateCallbacks->Process();
        if (ProcessPendingReset())
            return;

        m_Environment->simulate_dtime(m_PhysicsDeltaTime);

        m_ContactManager->Process(m_Environment->get_current_time());

        if (m_PostSimulateCallbacks->m_HasCallbacks)
            m_PostSimulateCallbacks->Process();
        if (ProcessPendingReset())
            return;

        const int len = m_MovableObjects.len();
        for (int i = len - 1; i >= 0; --i)
        {
            IVP_Real_Object *obj = m_MovableObjects.element_at(i);
            UpdateObjectWorldMatrix(obj);
        }
    }

    if (tracePath && *tracePath)
    {
        const double wallSeconds =
            std::chrono::duration<double>(traceNow - traceWallStart).count();
        if (wallSeconds >= 1.0)
        {
            const float simulationNow = m_Environment
                ? m_Environment->get_current_time().get_time() : simulationBefore;
            double simulatedSeconds = simulationNow - traceSimulationStart;
            if (simulatedSeconds < 0.0)
                simulatedSeconds = 0.0;
            FILE *trace = std::fopen(tracePath, "a+");
            if (trace)
            {
                std::fseek(trace, 0, SEEK_END);
                if (std::ftell(trace) == 0)
                    std::fprintf(trace, "wall_s,calls,ck_ms,submitted_s,ivp_s,smoothed_ms,time_factor,psi_s\n");
                std::fprintf(trace, "%.6f,%u,%.6f,%.9f,%.9f,%.6f,%.9f,%.9f\n",
                             wallSeconds, traceCalls, traceCkMilliseconds,
                             traceSubmittedSeconds, simulatedSeconds,
                             m_DeltaTime, m_PhysicsTimeFactor,
                             m_Environment ? m_Environment->get_delta_PSI_time() : 0.0f);
                std::fclose(trace);
            }
            traceWallStart = traceNow;
            traceSimulationStart = simulationNow;
            traceCkMilliseconds = 0.0;
            traceSubmittedSeconds = 0.0;
            traceCalls = 0;
        }
    }
}

void CKIpionManager::ResetSimulationClock()
{
    m_Environment->reset_time();
    m_Environment->get_time_manager()->env_set_current_time(m_Environment, IVP_Time(0));
    m_Environment->reset_time();
}

IVP_Time CKIpionManager::GetSimulationTime() const
{
    return m_Environment->get_current_time();
}

float CKIpionManager::GetSimulationTimeStep() const
{
    return m_Environment->get_delta_PSI_time();
}

void CKIpionManager::SetSimulationTimeStep(float step)
{
    m_Environment->set_delta_PSI_time(step);
}

void CKIpionManager::SetDeltaTime(float delta)
{
    m_DeltaTime = (m_DeltaTime * 3.0f + delta) / 4;
    float fixedPhysicsScale = 1.0f;
    if (m_TimeManager && m_TimeManager->GetFixedDeltaTime() > 0.0f)
        fixedPhysicsScale = m_TimeManager->GetFixedPhysicsTimeScale();
    m_PhysicsDeltaTime = m_DeltaTime * m_PhysicsTimeFactor * fixedPhysicsScale;
}

void CKIpionManager::SetTimeFactor(float factor)
{
    if (factor < 0.0f)
        factor = 0.0f;
    m_PhysicsTimeFactor = factor * 0.001f;
}

void CKIpionManager::GetGravity(VxVector &gravity) const
{
    const IVP_U_Point *g = m_Environment->get_gravity();
    gravity.Set((float)g->k[0], (float)g->k[1], (float)g->k[2]);
}

void CKIpionManager::SetGravity(const VxVector &gravity)
{
    IVP_U_Point g(gravity.x, gravity.y, gravity.z);
    m_Environment->set_gravity(&g);
}

IVP_SurfaceManager *CKIpionManager::GetCollisionSurface(const char *name) const
{
    if (!name || !m_CollisionSurfaces)
        return NULL;

    return (IVP_SurfaceManager *)m_CollisionSurfaces->find(name);
}

void CKIpionManager::OwnCollisionSurface(IVP_SurfaceManager *collisionSurface, IVP_Compact_Surface *compactSurface)
{
    if (!collisionSurface)
        return;

    m_CollisionSurfaceOwners.add(new PhysicsCollisionSurface(collisionSurface, compactSurface));
}

void CKIpionManager::OwnPrivateCollisionSurface(CK3dEntity *owner, IVP_SurfaceManager *collisionSurface,
                                                IVP_Compact_Surface *compactSurface)
{
    if (!owner || !collisionSurface)
        return;

    m_PrivateCollisionSurfaceOwners.add(
        new PhysicsPrivateCollisionSurface(owner->GetID(), collisionSurface, compactSurface));
}

void CKIpionManager::AddCollisionSurface(const char *name, IVP_SurfaceManager *collisionSurface,
                                         IVP_Compact_Surface *compactSurface)
{
    if (!name || !collisionSurface)
        return;

    if (!m_CollisionSurfaces)
        m_CollisionSurfaces = new IVP_U_String_Hash(64);

    if (name)
    {
        m_CollisionSurfaces->add(name, collisionSurface);
        OwnCollisionSurface(collisionSurface, compactSurface);
    }
}

void CKIpionManager::DeleteCollisionSurfaces()
{
    for (int i = m_CollisionSurfaceOwners.len() - 1; i >= 0; --i)
    {
        PhysicsCollisionSurface *surface = m_CollisionSurfaceOwners.element_at(i);
        m_CollisionSurfaceOwners.remove_at(i);
        DeleteCollisionSurfaceOwner(surface);
    }

    delete m_CollisionSurfaces;
    m_CollisionSurfaces = NULL;
}

void CKIpionManager::DeletePrivateCollisionSurface(CK_ID owner)
{
    for (int i = m_PrivateCollisionSurfaceOwners.len() - 1; i >= 0; --i)
    {
        PhysicsPrivateCollisionSurface *surface = m_PrivateCollisionSurfaceOwners.element_at(i);
        if (surface && surface->m_Owner == owner)
        {
            m_PrivateCollisionSurfaceOwners.remove_at(i);
            DeleteCollisionSurfaceOwner(surface);
        }
    }
}

void CKIpionManager::DeletePrivateCollisionSurfaces()
{
    for (int i = m_PrivateCollisionSurfaceOwners.len() - 1; i >= 0; --i)
    {
        PhysicsPrivateCollisionSurface *surface = m_PrivateCollisionSurfaceOwners.element_at(i);
        m_PrivateCollisionSurfaceOwners.remove_at(i);
        DeleteCollisionSurfaceOwner(surface);
    }
}

void CKIpionManager::ClearCollisionSurfaces()
{
    DeleteCollisionSurfaces();
    m_CollisionSurfaces = new IVP_U_String_Hash(64);
}

void CKIpionManager::OwnMaterial(CK3dEntity *owner, IVP_Material *material)
{
    if (!owner || !material)
        return;

    m_MaterialOwners.add(new PhysicsMaterialOwner(owner->GetID(), material));
}

void CKIpionManager::DeleteMaterial(CK_ID owner)
{
    for (int i = m_MaterialOwners.len() - 1; i >= 0; --i)
    {
        PhysicsMaterialOwner *owned = m_MaterialOwners.element_at(i);
        if (owned && owned->m_Owner == owner)
        {
            m_MaterialOwners.remove_at(i);
            delete owned->m_Material;
            delete owned;
        }
    }
}

void CKIpionManager::DeleteMaterials()
{
    for (int i = m_MaterialOwners.len() - 1; i >= 0; --i)
    {
        PhysicsMaterialOwner *owned = m_MaterialOwners.element_at(i);
        m_MaterialOwners.remove_at(i);
        if (owned)
        {
            delete owned->m_Material;
            delete owned;
        }
    }
}

void CKIpionManager::ClearLiquidSurfaces()
{
    const int len = m_LiquidSurfaces.len();
    for (int i = len - 1; i >= 0; --i)
    {
        IVP_Liquid_Surface_Descriptor_Simple *surface = m_LiquidSurfaces.element_at(i);
        m_LiquidSurfaces.remove_at(i);
        delete surface;
    }
}

void CKIpionManager::SetupCollisionDetectID()
{
    m_CollDetectionIDAttribType = -1;

    bool found = false;
    CKAttributeManager *am = m_Context->GetAttributeManager();
    const XObjectPointerArray &array = m_Context->GetObjectListByType(CKCID_3DOBJECT, FALSE);
    for (XObjectPointerArray::Iterator it = array.Begin(); it != array.End(); ++it)
    {
        if (found)
            break;

        CK3dObject *obj = (CK3dObject *)*it;
        const int count = obj->GetAttributeCount();
        for (int i = 0; i < count; ++i)
        {
            int type = obj->GetAttributeType(i);
            CKSTRING typeName = am->GetAttributeNameByType(type);
            if (typeName && strcmp(typeName, "Coll Detection ID") == 0 && obj->GetAttributeParameter(type) != NULL)
            {
                m_CollDetectionIDAttribType = type;
                found = true;
                break;
            }
        }
    }
}

int CKIpionManager::GetCollisionDetectID(CK3dEntity *entity) const
{
    int collisionID = -1;
    if (m_CollDetectionIDAttribType != -1 && entity)
    {
        CKParameterOut *pa = entity->GetAttributeParameter(m_CollDetectionIDAttribType);
        if (pa)
            pa->GetValue(&collisionID);
    }
    return collisionID;
}

void CKIpionManager::ResetProfiler()
{
    m_HasPhysicsTime = 0.0f;
    m_DePhysicalizeTime = 0.0f;
    field_FC = 0.0f;
    field_104 = 0.0f;
    m_HasPhysicsCalls = 0;
    m_PhysicalizeCalls = 0;
    m_DePhysicalizeCalls = 0;
}

void CKIpionManager::FillTemplateInfo(IVP_Template_Real_Object *templ, IVP_U_Point *position, IVP_U_Quat *quaternion,
                                      CKSTRING name, float mass, IVP_Material *material, float linearSpeedDampening,
                                      float rotSpeedDampening, CK3dEntity *target, CKBOOL fixed,
                                      CKSTRING collisionGroup, IVP_U_Matrix *massCenterMatrix,
                                      VxVector *shiftMassCenter)
{
    templ->mass = mass;
    templ->material = material;
    templ->physical_unmoveable = fixed ? IVP_TRUE : IVP_FALSE;
    templ->set_name(name);
    templ->rot_inertia_is_factor = IVP_TRUE;
    templ->rot_speed_damp_factor.set(rotSpeedDampening, rotSpeedDampening, rotSpeedDampening);
    templ->speed_damp_factor = linearSpeedDampening;
    templ->set_nocoll_group_ident(collisionGroup);
    if (shiftMassCenter)
    {
        IVP_U_Point offset(shiftMassCenter->x, shiftMassCenter->y, shiftMassCenter->z);
        massCenterMatrix->init();
        massCenterMatrix->shift_os(&offset);
        templ->mass_center_override = massCenterMatrix;
    }

    VxMatrix mat = target->GetWorldMatrix();

    // Transpose
    XSwap(mat[0][1], mat[1][0]);
    XSwap(mat[0][2], mat[2][0]);
    XSwap(mat[1][2], mat[2][1]);

    position->k[0] = mat[3][0];
    position->k[1] = mat[3][1];
    position->k[2] = mat[3][2];

    VxQuaternion quat;
    quat.FromMatrix(mat);

    quaternion->x = quat.x;
    quaternion->y = quat.y;
    quaternion->z = quat.z;
    quaternion->w = quat.w;
}

int CKIpionManager::AddConvexSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, CKMesh *convex, VxVector *scale)
{
    if (!builder)
        return 0;

    VxVector s = (scale) ? *scale : VxVector(1.0f, 1.0f, 1.0f);

    const int vertexCount = convex->GetVertexCount();
    CKDWORD stride;
    VxVector *pos = (VxVector *)convex->GetPositionsPtr(&stride);

    VxVector **vertices = new VxVector *[vertexCount];

    int count = 0;
    VxVector **ptr = vertices;
    for (int j = 0; j < vertexCount; ++j, pos = (VxVector *)((CKBYTE *)pos + stride))
    {
        // Filter out duplicate vertices
        int i;
        for (i = 0; i < count; ++i)
        {
            if (*pos == *vertices[i])
                break;
        }
        if (i == count)
        {
            *ptr++ = pos;
            ++count;
        }
    }

    IVP_U_Vector<IVP_U_Point> points(count);
    IVP_U_Point *pts = new IVP_U_Point[count];
    for (int p = 0; p < count; ++p)
    {
        VxVector t = *vertices[p] * s;
        pts[p].set(t.x, t.y, t.z);

        points.add(&pts[p]);
    }

    delete[] vertices;

    IVP_Compact_Ledge *ledge = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&points);
    int ret = 0;
    if (ledge)
    {
        builder->insert_ledge(ledge);
        ret = 1;
    }

    delete[] pts;
    return ret;
}

void CKIpionManager::AddConcaveSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, CKMesh *concave, VxVector *scale)
{
    if (!builder)
        return;

    VxVector s = (scale) ? *scale : VxVector(1.0f, 1.0f, 1.0f);

    VxVector vertex1, vertex2, vertex3;
    IVP_U_Vector<IVP_U_Point> points(3);
    IVP_U_Point point1, point2, point3;
    points.add(&point1);
    points.add(&point2);
    points.add(&point3);

    const int faceCount = concave->GetFaceCount();
    for (int i = 0; i < faceCount; ++i)
    {
        int vi1, vi2, vi3;
        concave->GetFaceVertexIndex(i, vi1, vi2, vi3);
        concave->GetVertexPosition(vi1, &vertex1);
        concave->GetVertexPosition(vi2, &vertex2);
        concave->GetVertexPosition(vi3, &vertex3);
        VxVector t1 = vertex1 * s;
        VxVector t2 = vertex2 * s;
        VxVector t3 = vertex3 * s;
        point1.set(t1.x, t1.y, t1.z);
        point2.set(t2.x, t2.y, t2.z);
        point3.set(t3.x, t3.y, t3.z);

        IVP_Compact_Ledge *ledge = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&points);
        if (ledge)
            builder->insert_ledge(ledge);
    }
}

int CKIpionManager::AddBallSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, const VxVector &center,
                                   float radius, VxVector *scale)
{
    if (!builder || radius <= 0.0f)
        return 0;

    static const float invSqrt2 = 0.707106781f;
    static const float invSqrt3 = 0.577350269f;
    static const float directions[][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
        {invSqrt2, invSqrt2, 0.0f}, {-invSqrt2, invSqrt2, 0.0f},
        {invSqrt2, -invSqrt2, 0.0f}, {-invSqrt2, -invSqrt2, 0.0f},
        {invSqrt2, 0.0f, invSqrt2}, {-invSqrt2, 0.0f, invSqrt2},
        {invSqrt2, 0.0f, -invSqrt2}, {-invSqrt2, 0.0f, -invSqrt2},
        {0.0f, invSqrt2, invSqrt2}, {0.0f, -invSqrt2, invSqrt2},
        {0.0f, invSqrt2, -invSqrt2}, {0.0f, -invSqrt2, -invSqrt2},
        {invSqrt3, invSqrt3, invSqrt3}, {-invSqrt3, invSqrt3, invSqrt3},
        {invSqrt3, -invSqrt3, invSqrt3}, {invSqrt3, invSqrt3, -invSqrt3},
        {-invSqrt3, -invSqrt3, invSqrt3}, {-invSqrt3, invSqrt3, -invSqrt3},
        {invSqrt3, -invSqrt3, -invSqrt3}, {-invSqrt3, -invSqrt3, -invSqrt3}};

    VxVector s = (scale) ? *scale : VxVector(1.0f, 1.0f, 1.0f);

    IVP_U_Vector<IVP_U_Point> points(sizeof(directions) / sizeof(directions[0]));
    IVP_U_Point pts[sizeof(directions) / sizeof(directions[0])];
    for (int i = 0; i < (int)(sizeof(directions) / sizeof(directions[0])); ++i)
    {
        VxVector p(center.x + directions[i][0] * radius,
                   center.y + directions[i][1] * radius,
                   center.z + directions[i][2] * radius);
        VxVector t = p * s;
        pts[i].set(t.x, t.y, t.z);
        points.add(&pts[i]);
    }

    IVP_Compact_Ledge *ledge = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&points);
    if (!ledge)
        return 0;

    builder->insert_ledge(ledge);
    return 1;
}

void CKIpionManager::UpdateObjectWorldMatrix(IVP_Real_Object *obj)
{
    if (!obj)
        return;

    CK3dEntity *ent = (CK3dEntity *)obj->client_data;
    if (!ent)
        return;

    VxVector scale(1.0f, 1.0f, 1.0f);
    ent->GetScale(&scale, FALSE);

    IVP_U_Matrix mat;
    obj->get_m_world_f_object_AT(&mat);

    VxMatrix m;

    // Transpose
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[j][i] = (float)mat.get_elem(i, j);
    m[0][0] *= scale.x;
    m[0][1] *= scale.x;
    m[0][2] *= scale.x;
    m[1][0] *= scale.y;
    m[1][1] *= scale.y;
    m[1][2] *= scale.y;
    m[2][0] *= scale.z;
    m[2][1] *= scale.z;
    m[2][2] *= scale.z;
    m[0][3] = 0.0f;
    m[1][3] = 0.0f;
    m[2][3] = 0.0f;

    m[3][0] = (float)mat.vv.k[0];
    m[3][1] = (float)mat.vv.k[1];
    m[3][2] = (float)mat.vv.k[2];
    m[3][3] = 1.0f;

    ent->SetWorldMatrix(m);
}
