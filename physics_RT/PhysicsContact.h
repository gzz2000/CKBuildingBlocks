#ifndef PHYSICS_RT_PHYSICSCONTACT_H
#define PHYSICS_RT_PHYSICSCONTACT_H

#include "CKBehavior.h"

#include "ivu_types.hxx"
#include "ivu_vector.hxx"
#include "ivu_linear.hxx"
#include "ivp_listener_collision.hxx"

class CKIpionManager;
class PhysicsObject;
class PhysicsContactManager;

class PhysicsContactData
{
public:
    struct GroupOutput
    {
        CKBOOL active = FALSE;
        int number = 0;
    };

    PhysicsContactData(float timeDelayStart, float timeDelayEnd, int groupOutputCount,
                       PhysicsContactManager *man, PhysicsObject *owner,
                       CKBehavior *beh);
    ~PhysicsContactData();

    CKBehavior *GetBehavior() const;

    float m_TimeDelayStart;
    float m_TimeDelayEnd;
    int m_GroupOutputCount;
    PhysicsContactManager *m_Manager;
    PhysicsObject *m_PhysicsObject;
    CK_ID m_BehaviorID;
    GroupOutput *m_GroupOutputs;
    IVP_Listener_Collision *m_Listener;
};

struct PhysicsContactRecord
{
    IVP_Time m_Time;
    int m_Index;
    PhysicsObject *m_PhysicsObject;

    PhysicsContactRecord(IVP_Time time, int index, PhysicsObject *obj)
        : m_Time(time), m_Index(index), m_PhysicsObject(obj) {}
};

class PhysicsContactManager
{
public:
    explicit PhysicsContactManager(CKIpionManager *man)
        : m_IpionManager(man), m_NumberGroupOutput(50), m_ContactIDAttribType(0) {}

    void Process(IVP_Time time);

    int GetRecordCount() const;
    PhysicsContactRecord *GetRecord(int index);
    void AddRecord(PhysicsObject *obj, int index, IVP_Time time);
    void RemoveRecord(int index);
    void RemoveRecord(PhysicsObject *obj);
    void RemoveRecord(PhysicsObject *obj, int index);
    void ClearRecords();

    void SetupContactID();
    int GetContactID(CK3dEntity *entity) const;
    int GetNumberGroupOutput() const { return m_NumberGroupOutput; }

    int m_NumberGroupOutput;
    IVP_U_Vector<PhysicsContactRecord> m_Records;
    CKIpionManager *m_IpionManager;
    int m_ContactIDAttribType;
};

// Creates the runtime contact listener immediately. Snapshot restoration uses
// this instead of queueing the behavior callback for a later simulation tick.
int CreatePhysicsContinuousContactData(CKIpionManager *manager,
                                       PhysicsObject *object,
                                       CKBehavior *behavior);

#endif // PHYSICS_RT_PHYSICSCONTACT_H
