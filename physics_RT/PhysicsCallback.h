#ifndef PHYSICS_RT_PHYSICSCALLBACK_H
#define PHYSICS_RT_PHYSICSCALLBACK_H

#include "CKBehavior.h"

#include "ivu_types.hxx"
#include "ivu_vector.hxx"

class CKIpionManager;

class PhysicsCallback
{
public:
    PhysicsCallback() : m_IpionManager(NULL), m_Type(0), m_BehaviorID(0) {}
    PhysicsCallback(CKIpionManager *pm, CKBehavior *beh, int type)
        : m_IpionManager(pm), m_Type(type), m_BehaviorID(beh ? beh->GetID() : 0) {}
    virtual int Execute() = 0;
    virtual ~PhysicsCallback(){};

    // Capture a dormant typed operation without executing it or reading BB
    // inputs again. Each implementation owns its payload; no callback address
    // from the abandoned timeline is reused during restore.
    virtual PhysicsCallback *CloneForSnapshot() const = 0;

    CKBehavior *GetBehavior() const;

    CKIpionManager *m_IpionManager;
    int m_Type;
    CK_ID m_BehaviorID;
};

class PhysicsCallbackContainer
{
public:
    explicit PhysicsCallbackContainer(CKIpionManager *manager) : m_IpionManager(manager), m_HasCallbacks(FALSE) {}
    ~PhysicsCallbackContainer();

    void Clear();
    void ClearBehavior(CK_ID behaviorID);
    void Process();
    void Process(PhysicsCallback *pc);

    CKIpionManager *m_IpionManager;
    CKBOOL m_HasCallbacks;
    IVP_U_Vector<PhysicsCallback> m_Callbacks[3];
};

#endif // PHYSICS_RT_PHYSICSCALLBACK_H
