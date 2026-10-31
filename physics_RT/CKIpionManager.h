#ifndef PHYSICS_RT_IPIONMANAGER_H
#define PHYSICS_RT_IPIONMANAGER_H

#include <cstdint>
#include <unordered_map>
#include <memory>

#include "CKBaseManager.h"
#include "CKAttributeManager.h"
#include "CKContext.h"
#include "XNHashTable.h"
#include "VxTimeProfiler.h"

#include "ivu_types.hxx"
#include "ivu_string_hash.hxx"
#include "ivp_physics.hxx"
#include "ivp_time.hxx"
#include "ivp_real_object.hxx"
#include "ivp_ball.hxx"
#include "ivp_polygon.hxx"
#include "ivp_material.hxx"
#include "ivp_constraint.hxx"
#include "ivp_phantom.hxx"
#include "ivp_templates.hxx"
#include "ivp_template_constraint.hxx"
#include "ivp_controller_factory.hxx"
#include "ivp_actuator_spring.hxx"
#include "ivp_listener_object.hxx"
#include "ivp_collision_filter.hxx"
#include "ivp_surbuild_ledge_soup.hxx"
#include "ivp_controller_buoyancy.hxx"
#include "ivp_liquid_surface_descript.hxx"

#include "PhysicsCallback.h"
#include "PhysicsContact.h"

#define TERRATOOLS_GUID CKGUID(0x56495254, 0x4f4f4c53)
#define TT_PHYSICS_MANAGER_GUID CKGUID(0x6BED328B, 0x141F5148)

class PhysicsObjectListener;
class PhysicsCollisionListener;

class PhysicsCollisionSurface
{
public:
    PhysicsCollisionSurface(IVP_SurfaceManager *surfaceManager = NULL, IVP_Compact_Surface *compactSurface = NULL)
        : m_SurfaceManager(surfaceManager), m_CompactSurface(compactSurface) {}

    IVP_SurfaceManager *m_SurfaceManager;
    IVP_Compact_Surface *m_CompactSurface;
};

class PhysicsPrivateCollisionSurface : public PhysicsCollisionSurface
{
public:
    PhysicsPrivateCollisionSurface(CK_ID owner = 0, IVP_SurfaceManager *surfaceManager = NULL,
                                   IVP_Compact_Surface *compactSurface = NULL)
        : PhysicsCollisionSurface(surfaceManager, compactSurface), m_Owner(owner) {}

    CK_ID m_Owner;
};

class PhysicsMaterialOwner
{
public:
    PhysicsMaterialOwner(CK_ID owner = 0, IVP_Material *material = NULL)
        : m_Owner(owner), m_Material(material) {}

    CK_ID m_Owner;
    IVP_Material *m_Material;
};

class PhysicsObject
{
public:
    PhysicsObject() : m_RealObject(NULL),
                      m_FrictionCount(0), m_FrictionTime(0),
                      m_ContactData(NULL) {}

    ~PhysicsObject()
    {
        if (m_ContactData)
        {
            delete m_ContactData;
            m_ContactData = NULL;
        }
    }

    // Stable native inspection/control surface used by deterministic tooling.
    // These deliberately describe semantic physics state instead of exposing
    // the old 32-bit binary layout that legacy TAS plug-ins mirrored.
    const char *GetName() const;
    CK3dEntity *GetEntity() const;
    void Wake();
    bool IsStatic() const;
    void EnableCollisions(bool enable);
    float GetMass() const;
    float GetInvMass() const;
    void GetInertia(VxVector &inertia) const;
    void GetInvInertia(VxVector &inertia) const;
    void GetDamping(float *speed, float *rotation) const;
    void GetPosition(VxVector *worldPosition, VxVector *angles) const;
    void GetPositionMatrix(VxMatrix &positionMatrix) const;
    void GetVelocity(VxVector *velocity, VxVector *angularVelocity) const;
    void SetVelocity(const VxVector *velocity, const VxVector *angularVelocity);

    IVP_Real_Object *m_RealObject;
    int m_FrictionCount;
    IVP_Time m_FrictionTime;
    PhysicsContactData *m_ContactData;
};

typedef XNHashTable<PhysicsObject, CK_ID> PhysicsObjectTable;

class CKIpionManager : public CKBaseManager
{
public:
    explicit CKIpionManager(CKContext *context);
    virtual ~CKIpionManager();

    virtual CKERROR OnCKInit();
    virtual CKERROR OnCKEnd();
    virtual CKERROR OnCKPlay();
    virtual CKERROR OnCKReset();
    virtual CKERROR OnCKPostReset();
    virtual CKERROR PostClearAll();
    virtual CKERROR PostProcess();
    virtual CKERROR SequenceToBeDeleted(CK_ID *iIDs, int iCount);
    virtual CKDWORD GetValidFunctionsMask()
    {
        return CKMANAGER_FUNC_PostProcess |
               CKMANAGER_FUNC_PostClearAll |
               CKMANAGER_FUNC_OnCKInit |
               CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnSequenceToBeDeleted |
               CKMANAGER_FUNC_OnCKPlay |
               CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_OnCKPostReset;
    }

    virtual void Reset();
    void ResetPhysicsBehaviorHandles();
    void ClearBehaviorCallbacks(CK_ID behaviorID);
    CKBOOL ProcessPendingReset();

    int GetPhysicsObjectCount() const;
    PhysicsObject *GetPhysicsObject(CK3dEntity *entity, CKBOOL logging = FALSE);
    CKBOOL IsPhysicsObjectLive(PhysicsObject *object) const;
    void RemovePhysicsObject(CK3dEntity *entity);
    void RegisterPhysicsObject(CK_ID owner, const PhysicsObject &object);
    uint64_t GetPhysicsObjectLifetime(CK_ID owner) const;
    void ArmClockResetOnNextPhysicsObject();

    int CreatePhysicsObjectOnParameters(CK3dEntity *target, int convexCount, CKMesh **convexes,
                                        int ballCount, VxVector *ballPositions, float *ballRadii,
                                        int concaveCount, CKMesh **concaves, float ballRadius,
                                        CKSTRING collisionSurface, VxVector *shiftMassCenter, CKBOOL fixed,
                                        IVP_Material *material, float mass, CKSTRING collisionGroup,
                                        CKBOOL startFrozen, CKBOOL enableCollision, CKBOOL autoCalcMassCenter,
                                        float linearSpeedDampening, float rotSpeedDampening);

    IVP_Ball *CreatePhysicsBall(CKSTRING name, float mass, float ballRadius, IVP_Material *material,
                                float linearSpeedDampening, float rotSpeedDampening, CK3dEntity *entity,
                                CKBOOL startFrozen, CKBOOL fixed, CKSTRING collisionGroup,
                                CKBOOL enableCollision, VxVector *shiftMassCenter);

    IVP_Polygon *CreatePhysicsMultiBall(CKSTRING name, float mass, int ballCount, VxVector *ballPositions,
                                        float *ballRadii, IVP_Material *material,
                                        float linearSpeedDampening, float rotSpeedDampening,
                                        CK3dEntity *target, CKBOOL startFrozen, CKBOOL fixed,
                                        CKSTRING collisionGroup, CKBOOL enableCollision,
                                        VxVector *shiftMassCenter, VxVector *scale);

    IVP_Polygon *CreatePhysicsPolygon(CKSTRING name, float mass, IVP_Material *material,
                                      float linearSpeedDampening, float rotSpeedDampening,
                                      CK3dEntity *target, CKBOOL startFrozen, CKBOOL fixed,
                                      CKSTRING collisionGroup, CKBOOL enableCollision,
                                      IVP_SurfaceManager *surman, VxVector *shiftMassCenter);

    IVP_Constraint *CreateConstraint(const IVP_Template_Constraint *tmpl)
    {
        return IVP_Controller_Factory::create_constraint(m_Environment, tmpl);
    }

    IVP_Actuator_Spring *CreateSpring(IVP_Template_Spring *tmpl)
    {
        return IVP_Controller_Factory::create_spring(m_Environment, tmpl);
    }

    IVP_Environment *GetEnvironment() const { return m_Environment; }
    float GetDeltaTime() const { return m_DeltaTime; }
    float GetPhysicsDeltaTime() const { return m_PhysicsDeltaTime; }
    void SetPhysicsDeltaTime(float value) { m_PhysicsDeltaTime = value; }
    float GetPhysicsTimeFactor() const { return m_PhysicsTimeFactor; }
    void SetPhysicsTimeFactor(float value) { m_PhysicsTimeFactor = value; }
    void CreateEnvironment();
    void DestroyEnvironment(CKBOOL resetBehaviorHandles = TRUE);

    void Simulate(float deltaTime);
    void SetExternalPostProcess(CKBOOL external) { m_ExternalPostProcess = external; }
    CKBOOL IsExternalPostProcess() const { return m_ExternalPostProcess; }
    CKERROR RunExternalPostProcess();

    void ResetSimulationClock();
    // Shift IVP timestamps for explicit time-origin maintenance. Snapshot
    // capture must remain observational and must not call this operation.
    void CanonicalizeSimulationClock();
    struct SimulationClockState
    {
        double currentTime;
        double lastPsiTime;
        double nextPsiTime;
        int currentTimeCode;
        int mindistEventTimestamp;
        short nextMovementCheck;
        double timeManagerBaseTime;
        double timeManagerLastTime;
        double nextEventTime;
        int eventCount;
    };
    void CaptureSimulationClock(SimulationClockState &state) const;
    void RestoreSimulationClock(const SimulationClockState &state);
    // Keep separate from SimulationClockState: existing diagnostic plug-ins
    // allocate that structure themselves and depend on its original layout.
    struct SimulationStepState
    {
        double duration;
        double inverseDuration;
    };
    void CaptureSimulationStep(SimulationStepState &state) const;
    void RestoreSimulationStep(const SimulationStepState &state);
    short GetNextMovementCheck() const;
    void SetNextMovementCheck(short value);

    IVP_Time GetSimulationTime() const;

    float GetSimulationTimeStep() const;
    void SetSimulationTimeStep(float step);

    void SetDeltaTime(float delta);
    void SetDeltaTimeImmediate(float delta);
    void SetTimeFactor(float factor);

    void GetGravity(VxVector &gravity) const;
    void SetGravity(const VxVector &gravity);

    PhysicsContactManager *GetContactManager() const { return m_ContactManager; }

    IVP_SurfaceManager *GetCollisionSurface(const char *name) const;
    void OwnCollisionSurface(IVP_SurfaceManager *collisionSurface, IVP_Compact_Surface *compactSurface);
    void OwnPrivateCollisionSurface(CK3dEntity *owner, IVP_SurfaceManager *collisionSurface,
                                    IVP_Compact_Surface *compactSurface);
    void AddCollisionSurface(const char *name, IVP_SurfaceManager *collisionSurface,
                             IVP_Compact_Surface *compactSurface);

    void DeleteCollisionSurfaces();
    void DeletePrivateCollisionSurface(CK_ID owner);
    void DeletePrivateCollisionSurfaces();
    void ClearCollisionSurfaces();

    void OwnMaterial(CK3dEntity *owner, IVP_Material *material);
    // Asset ownership is separate from per-entity associations: a shared asset
    // remains alive while any registered physics body still references it.
    void RetainMaterial(IVP_Material *material);
    bool OwnsMaterial(const IVP_Material *material) const;
    void ReleaseUnusedMaterial(IVP_Material *material);
    void DeleteMaterial(CK_ID owner);
    void DeleteMaterials();

    void ClearLiquidSurfaces();

    void SetupCollisionDetectID();
    int GetCollisionDetectID(CK3dEntity *entity) const;

    void ResetProfiler();

    static void FillTemplateInfo(IVP_Template_Real_Object *templ, IVP_U_Point *position, IVP_U_Quat *quaternion,
                                 CKSTRING name, float mass, IVP_Material *material,
                                 float linearSpeedDampening, float rotSpeedDampening,
                                 CK3dEntity *target, CKBOOL fixed, CKSTRING collisionGroup,
                                 IVP_U_Matrix *massCenterMatrix, VxVector *shiftMassCenter);

    static int AddConvexSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, CKMesh *convex, VxVector *scale);
    static void AddConcaveSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, CKMesh *concave, VxVector *scale);
    static int AddBallSurface(IVP_SurfaceBuilder_Ledge_Soup *builder, const VxVector &center,
                              float radius, VxVector *scale);

    static void UpdateObjectWorldMatrix(IVP_Real_Object *obj);

    static CKIpionManager *GetManager(CKContext *context)
    {
        return (CKIpionManager *)context->GetManagerByGuid(TT_PHYSICS_MANAGER_GUID);
    }

    IVP_U_Vector<IVP_Real_Object> m_MovableObjects;
    IVP_U_Vector<CK3dEntity> m_Entities;
    IVP_U_Vector<PhysicsMaterialOwner> m_MaterialOwners;
    IVP_U_Vector<IVP_Liquid_Surface_Descriptor_Simple> m_LiquidSurfaces;
    IVP_U_Vector<PhysicsCollisionSurface> m_CollisionSurfaceOwners;
    IVP_U_Vector<PhysicsPrivateCollisionSurface> m_PrivateCollisionSurfaceOwners;
    PhysicsCallbackContainer *m_PreSimulateCallbacks;
    PhysicsCallbackContainer *m_PostSimulateCallbacks;
    PhysicsContactManager *m_ContactManager;
    PhysicsCollisionListener *m_CollisionListener;
    PhysicsObjectListener *m_ObjectListener;
    int m_CollDetectionIDAttribType;
    IVP_Collision_Filter_Exclusive_Pair *m_CollisionFilterExclusivePair;
    IVP_U_String_Hash *m_CollisionSurfaces;
    IVP_Environment *m_Environment;
    CKTimeManager *m_TimeManager;
    float m_DeltaTime;
    float m_PhysicsDeltaTime;
    float m_PhysicsTimeFactor;
    int m_CallbackProcessingDepth;
    CKBOOL m_ResetRequested;
    CKBOOL m_ExternalPostProcess;
    int m_HasPhysicsCalls;
    int m_PhysicalizeCalls;
    int m_DePhysicalizeCalls;
    float m_HasPhysicsTime;
    float m_DePhysicalizeTime;
    float field_FC;
    float field_104;
    PhysicsObjectTable m_PhysicsObjects;

private:
    // Allocation addresses can be reused after a ball is dephysicalized. These
    // process-local identities are monotonic and are never rewound with a TAS.
    uint64_t m_NextPhysicsObjectLifetime = 1;
    std::unordered_map<CK_ID, uint64_t> m_PhysicsObjectLifetimes;
    std::unordered_map<IVP_Material *, std::unique_ptr<IVP_Material>> m_OwnedMaterials;
};

#endif // PHYSICS_RT_IPIONMANAGER_H
