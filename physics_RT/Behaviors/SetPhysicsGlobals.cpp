/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		          Set Physics Globals
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"

#include <cstdio>
#include <cstdlib>

static void TracePhysicsGlobals(const char *phase, CKBehavior *behavior,
                                CKContext *context, CKIpionManager *manager,
                                float requestedFactor)
{
    if (!std::getenv("BALLANCE_TRACE_PHYSICS_GLOBALS"))
        return;
    CKBehavior *parent = behavior->GetParent();
    CKTimeManager *time = context->GetTimeManager();
    std::fprintf(stderr,
        "PHYSICS_GLOBALS %s tick=%u behavior=%u parent=%u inputs=(%d %d) "
        "requested=%a stored=%a delta=%a submitted=%a time=%a parent_name=%s\n",
        phase, time ? (unsigned)time->GetMainTickCount() : 0u,
        (unsigned)behavior->GetID(), parent ? (unsigned)parent->GetID() : 0u,
        (int)behavior->IsInputActive(0), (int)behavior->IsInputActive(1),
        (double)requestedFactor, (double)manager->GetPhysicsTimeFactor(),
        (double)manager->GetDeltaTime(), (double)manager->GetPhysicsDeltaTime(),
        manager->GetEnvironment() ? manager->GetEnvironment()->get_current_time().get_time() : 0.0,
        parent && parent->GetName() ? parent->GetName() : "(none)");
}

CKObjectDeclaration *FillBehaviorSetPhysicsGlobalsDecl();
CKERROR CreateSetPhysicsGlobalsProto(CKBehaviorPrototype **pproto);
int SetPhysicsGlobals(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorSetPhysicsGlobalsDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("Set Physics Globals");
    od->SetDescription("Set the Physics Global Variables...");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x72af347c, 0x3da71e1));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010000);
    od->SetCreationFunction(CreateSetPhysicsGlobalsProto);
    od->SetCompatibleClassId(CKCID_BEOBJECT);
    return od;
}

CKERROR CreateSetPhysicsGlobalsProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("Set Physics Globals");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("Set Values");
    proto->DeclareInput("Clean Physics World");

    proto->DeclareOutput("Out");

    proto->DeclareInParameter("Gravity", CKPGUID_VECTOR, "0,-9.81,0");
    proto->DeclareInParameter("Physic Time Factor", CKPGUID_FLOAT, "1");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(SetPhysicsGlobals);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);

    *pproto = proto;
    return CK_OK;
}

int SetPhysicsGlobals(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    VxVector gravity(0.0f, -9.81f, 0.0f);
    beh->GetInputParameterValue(0, &gravity);

    float physicsTimeFactor = 1.0f;
    beh->GetInputParameterValue(1, &physicsTimeFactor);

    CKIpionManager *man = CKIpionManager::GetManager(context);
    if (!man)
        return CKBR_GENERICERROR;

    TracePhysicsGlobals("in", beh, context, man, physicsTimeFactor);
    if (beh->IsInputActive(0))
    {
        beh->ActivateInput(0, FALSE);
        if (man->GetEnvironment())
        {
            man->SetTimeFactor(physicsTimeFactor);
            man->SetGravity(gravity);
        }
    }
    if (beh->IsInputActive(1))
    {
        beh->ActivateInput(1, FALSE);
        man->Reset();
    }

    TracePhysicsGlobals("out", beh, context, man, physicsTimeFactor);
    beh->ActivateOutput(0, TRUE);
    return CKBR_OK;
}
