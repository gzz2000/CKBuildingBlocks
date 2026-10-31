/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		           Physics WakeUp
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

CKObjectDeclaration *FillBehaviorPhysicsWakeUpDecl();
CKERROR CreatePhysicsWakeUpProto(CKBehaviorPrototype **pproto);
int PhysicsWakeUp(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorPhysicsWakeUpDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("Physics WakeUp");
    od->SetDescription("Wake Up Object for Simulation");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x38b851b5, 0x72ca74ac));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010000);
    od->SetCreationFunction(CreatePhysicsWakeUpProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}

CKERROR CreatePhysicsWakeUpProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("Physics WakeUp");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("In");

    proto->DeclareOutput("Out");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(PhysicsWakeUp);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);

    *pproto = proto;
    return CK_OK;
}

class PhysicsWakeUpCallback : public PhysicsCallback
{
public:
    PhysicsWakeUpCallback(CKIpionManager *man, CKBehavior *beh) : PhysicsCallback(man, beh, 2) {}

    PhysicsCallback *CloneForSnapshot() const override
    {
        // This callback has no payload beyond manager/type/behavior identity.
        return new PhysicsWakeUpCallback(*this);
    }

    virtual int Execute()
    {
        CKBehavior *beh = GetBehavior();
        if (!beh)
            return 1;

        CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
        if (!ent)
            return 1;

        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent);
        if (!po)
            return 0;

        po->m_RealObject->ensure_in_simulation();

        return 1;
    }
};

int PhysicsWakeUp(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
    if (!ent)
        return CKBR_OWNERERROR;

    CKIpionManager *man = CKIpionManager::GetManager(context);
    if (!man || !man->GetEnvironment() || !man->m_PreSimulateCallbacks)
        return CKBR_GENERICERROR;

    // Opt-in lifecycle oracle: identify the request boundary independently of
    // the later PSI at which the queued core is actually revived.
    const char *traceName = std::getenv("BALLANCE_TRACE_WAKE_OBJECT");
    if (traceName && ent->GetName() && !std::strcmp(traceName, ent->GetName()))
    {
        CKTimeManager *time = context->GetTimeManager();
        std::fprintf(stderr,
            "WAKE_REQUEST behavior=%u entity=%u name=%s env=%.17g ck_tick=%u ck_time=%.9g\n",
            static_cast<unsigned>(beh->GetID()), static_cast<unsigned>(ent->GetID()),
            ent->GetName(), man->GetEnvironment()->get_current_time().get_time(),
            time ? static_cast<unsigned>(time->GetMainTickCount()) : 0U,
            time ? static_cast<double>(time->GetAbsoluteTime()) : 0.0);
    }

    PhysicsWakeUpCallback *cb = new PhysicsWakeUpCallback(man, beh);
    man->m_PreSimulateCallbacks->Process(cb);

    beh->ActivateInput(0, FALSE);
    beh->ActivateOutput(0, TRUE);
    return CKBR_OK;
}
