/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		        Physics Ball Joint
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"

CKObjectDeclaration *FillBehaviorPhysicsBallJointDecl();
CKERROR CreatePhysicsBallJointProto(CKBehaviorPrototype **pproto);
int PhysicsBallJoint(const CKBehaviorContext &behcontext);
CKERROR PhysicsBallJointCallBack(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorPhysicsBallJointDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("Set Physics Ball Joint");
    od->SetDescription("Sets a Physics Balljoint ...");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x5e624f0a, 0x35160450));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010001);
    od->SetCreationFunction(CreatePhysicsBallJointProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}

CKERROR CreatePhysicsBallJointProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("Set Physics Ball Joint");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("Create");
    proto->DeclareInput("Shutdown");

    proto->DeclareOutput("Out1");
    proto->DeclareOutput("Out2");

    proto->DeclareInParameter("Object2", CKPGUID_3DENTITY);
    proto->DeclareInParameter("Position 1", CKPGUID_VECTOR, "0,0,0");
    proto->DeclareInParameter("Referential 1", CKPGUID_3DENTITY);

    proto->DeclareLocalParameter("IVP_Handle", CKPGUID_POINTER);

    proto->DeclareSetting("Specify 2 Points ?", CKPGUID_BOOL, "FALSE");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(PhysicsBallJoint);

    proto->SetBehaviorFlags((CK_BEHAVIOR_FLAGS)(CKBEHAVIOR_TARGETABLE | CKBEHAVIOR_INTERNALLYCREATEDINPUTPARAMS));
    proto->SetBehaviorCallbackFct(PhysicsBallJointCallBack);

    *pproto = proto;
    return CK_OK;
}

#define OBJECT2 0
#define POSITION1 1
#define REFERENTIAL1 2
#define POSITION2 3
#define REFERENTIAL2 4

#define IVP_HANDLE 0
#define SPECIFY_2_POINTS 1

class PhysicsBallJointCallback : public PhysicsCallback
{
public:
    PhysicsBallJointCallback(CKIpionManager *man, CKBehavior *beh) : PhysicsCallback(man, beh, 2) {}

    PhysicsCallback *CloneForSnapshot() const override
    {
        // This callback has no payload beyond manager/type/behavior identity.
        return new PhysicsBallJointCallback(*this);
    }

    virtual int Execute()
    {
        CKBehavior *beh = GetBehavior();
        if (!beh)
            return CKBR_ACTIVATENEXTFRAME;

        CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
        if (!ent)
            return CKBR_ACTIVATENEXTFRAME;

        CK3dEntity *object2 = (CK3dEntity *)beh->GetInputParameterObject(OBJECT2);
        if (!object2)
            return CKBR_ACTIVATENEXTFRAME;

        PhysicsObject *poR = m_IpionManager->GetPhysicsObject(ent, TRUE);
        if (!poR)
            return CKBR_OK;

        IVP_Real_Object *objR = poR->m_RealObject;

        PhysicsObject *poA = m_IpionManager->GetPhysicsObject(object2, TRUE);
        if (!poA)
            return CKBR_OK;

        IVP_Real_Object *objA = poA->m_RealObject;

        VxVector position1;
        beh->GetInputParameterValue(POSITION1, &position1);

        CK3dEntity *referential = (CK3dEntity *)beh->GetInputParameterObject(REFERENTIAL1);

        IVP_Template_Constraint tmpl;

        VxVector pos1;
        if (referential)
            referential->Transform(&pos1, &position1);
        else
            pos1 = position1;

        CKBOOL specify2Points = FALSE;
        beh->GetLocalParameterValue(SPECIFY_2_POINTS, &specify2Points);

        if (specify2Points && beh->GetInputParameterCount() >= 5)
        {
            VxVector position2;
            beh->GetInputParameterValue(POSITION2, &position2);

            CK3dEntity *referential2 = (CK3dEntity *)beh->GetInputParameterObject(REFERENTIAL2);

            VxVector pos2;
            if (referential2)
                referential2->Transform(&pos2, &position2);
            else
                pos2 = position2;

            IVP_U_Matrix mWorldObject;
            objR->get_m_world_f_object_AT(&mWorldObject);

            IVP_U_Point anchorWs(pos1.x, pos1.y, pos1.z);
            IVP_U_Point anchorRos;
            mWorldObject.vimult4(&anchorWs, &anchorRos);

            VxVector delta = pos2 - pos1;
            IVP_U_Point deltaWs(delta.x, delta.y, delta.z);
            IVP_U_Point deltaRos;
            mWorldObject.vimult3(&deltaWs, &deltaRos);

            tmpl.set_ballsocket_tense_Ros(objR, &anchorRos, objA, &deltaRos);
        }
        else
        {
            IVP_U_Point anchor(pos1.x, pos1.y, pos1.z);
            tmpl.set_ballsocket_ws(objR, &anchor, objA);
        }

        IVP_Constraint *constraint = m_IpionManager->CreateConstraint(&tmpl);
        beh->SetLocalParameterValue(IVP_HANDLE, &constraint);

        return CKBR_ACTIVATENEXTFRAME;
    }
};

int PhysicsBallJoint(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    IVP_Constraint *constraint = NULL;
    beh->GetLocalParameterValue(IVP_HANDLE, &constraint);

    if (beh->IsInputActive(0))
    {
        if (!constraint)
        {
            CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
            if (!ent)
                return CKBR_OWNERERROR;

            CKIpionManager *man = CKIpionManager::GetManager(context);
            if (!man || !man->GetEnvironment() || !man->m_PreSimulateCallbacks)
                return CKBR_GENERICERROR;

            PhysicsBallJointCallback *cb = new PhysicsBallJointCallback(man, beh);
            man->m_PreSimulateCallbacks->Process(cb);
        }

        beh->ActivateInput(0, FALSE);
        beh->ActivateOutput(0, TRUE);
    }
    else
    {
        if (constraint)
        {
            delete constraint;
            constraint = NULL;
            beh->SetLocalParameterValue(IVP_HANDLE, &constraint);
        }

        beh->ActivateInput(1, FALSE);
        beh->ActivateOutput(1, TRUE);
    }

    return CKBR_OK;
}

CKERROR PhysicsBallJointCallBack(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    if (!beh->GetOwner())
        return CKBR_OWNERERROR;

    switch (behcontext.CallbackMessage)
    {
    case CKM_BEHAVIORRESET:
    case CKM_BEHAVIORDELETE:
    case CKM_BEHAVIORDETACH:
    {
        IVP_Constraint *constraint = NULL;
        beh->GetLocalParameterValue(IVP_HANDLE, &constraint);

        CKIpionManager *man = CKIpionManager::GetManager(behcontext.Context);
        if (constraint && man && man->GetEnvironment())
            delete constraint;

        constraint = NULL;
        beh->SetLocalParameterValue(IVP_HANDLE, &constraint);
        return CKBR_OK;
    }
    case CKM_BEHAVIORSETTINGSEDITED:
    {
        CKBOOL specify2Points = FALSE;
        beh->GetLocalParameterValue(SPECIFY_2_POINTS, &specify2Points);

        int count = beh->GetInputParameterCount();
        for (int i = count - 1; i >= 3; --i)
        {
            CKParameterIn *pin = beh->RemoveInputParameter(i);
            CKDestroyObject(pin);
        }

        if (specify2Points)
        {
            beh->CreateInputParameter("Position 2", CKPGUID_VECTOR);
            beh->CreateInputParameter("Referential 2", CKPGUID_3DENTITY);
        }
    }
    default:
        break;
    }

    return CKBR_OK;
}
