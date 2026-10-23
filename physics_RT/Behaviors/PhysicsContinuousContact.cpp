/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		     Physics Continuous Contact
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"

CKObjectDeclaration *FillBehaviorPhysicsContinuousContactDecl();
CKERROR CreatePhysicsContinuousContactProto(CKBehaviorPrototype **pproto);
int PhysicsContinuousContact(const CKBehaviorContext &behcontext);
CKERROR PhysicsContinuousContactCallBack(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorPhysicsContinuousContactDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("PhysicsContinuousContact");
    od->SetDescription("PhysicsContinuousContact");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x199e4cf1, 0x545a78fe));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010001);
    od->SetCreationFunction(CreatePhysicsContinuousContactProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}

CKERROR CreatePhysicsContinuousContactProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("PhysicsContinuousContact");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("Create");
    proto->DeclareInput("Stop");

    char buf[256];
    for (int i = 1; i <= 5; ++i)
    {
        snprintf(buf, sizeof(buf), "contact on %d", i);
        proto->DeclareOutput(buf);
        snprintf(buf, sizeof(buf), "contact off %d", i);
        proto->DeclareOutput(buf);
    }

    proto->DeclareInParameter("Time Delay Start", CKPGUID_FLOAT, "0.1");
    proto->DeclareInParameter("Time Delay End", CKPGUID_FLOAT, "0.1");

    proto->DeclareSetting("Number Group Output", CKPGUID_INT, "5");

    proto->DeclareLocalParameter("IVP_Handle", CKPGUID_POINTER, "NULL");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(PhysicsContinuousContact);

    proto->SetBehaviorFlags((CK_BEHAVIOR_FLAGS)(CKBEHAVIOR_TARGETABLE | CKBEHAVIOR_INTERNALLYCREATEDINPUTPARAMS));
    proto->SetBehaviorCallbackFct(PhysicsContinuousContactCallBack);

    *pproto = proto;
    return CK_OK;
}

#define TIME_DELAY_START 0
#define TIME_DELAY_END 1

class PhysicsContactListener : public IVP_Listener_Collision
{
public:
    PhysicsContactListener(IVP_Real_Object *obj, CKIpionManager *man)
        : IVP_Listener_Collision(IVP_LISTENER_COLLISION_CALLBACK_OBJECT_DELETED |
                                 IVP_LISTENER_COLLISION_CALLBACK_FRICTION),
          m_RealObject(obj), m_IpionManager(man)
    {
        obj->add_listener_collision(this);
    }

    ~PhysicsContactListener()
    {
        if (m_RealObject)
            m_RealObject->remove_listener_collision(this);
    }

    void event_collision_object_deleted(IVP_Real_Object *obj)
    {
        if (obj == m_RealObject)
        {
            m_RealObject->remove_listener_collision(this);
            m_RealObject = NULL;
        }
    }

    virtual void event_friction_created(IVP_Event_Friction *friction)
    {
        if (!friction)
            return;

        IVP_Contact_Situation *situation = friction->contact_situation;
        IVP_Real_Object *obj = situation->objects[0];
        if (obj == m_RealObject)
            obj = situation->objects[1];

        CK3dEntity *ent = (CK3dEntity *)obj->client_data;
        if (!ent)
            return;

        int contactID = m_IpionManager->m_ContactManager->GetContactID(ent);
        if (contactID <= 0)
            return;

        ent = (CK3dEntity *)m_RealObject->client_data;
        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent);
        if (!po)
            return;

        PhysicsContactData *data = po->m_ContactData;
        if (!data)
            return;

        int index = contactID - 1;
        if (index < 0 || index >= data->m_GroupOutputCount)
            return;

        PhysicsContactData::GroupOutput &output = data->m_GroupOutputs[index];
        output.number += 1;
        if (output.number != 1)
            return;

        if (!output.active)
        {
            bool activated = false;
            IVP_Time time = m_IpionManager->GetSimulationTime();
            PhysicsContactManager *contactManager = m_IpionManager->GetContactManager();
            for (int i = contactManager->GetRecordCount() - 1; i >= 0; --i)
            {
                PhysicsContactRecord *record = contactManager->GetRecord(i);
                if (record->m_PhysicsObject == po && record->m_Index == index &&
                    data->m_TimeDelayStart < time - record->m_Time)
                {
                    contactManager->RemoveRecord(i);
                    data->m_GroupOutputs[index].active = TRUE;
                    const int outputIndex = 2 * index;
                    CKBehavior *beh = data->GetBehavior();
                    if (beh && outputIndex < beh->GetOutputCount())
                        beh->ActivateOutput(outputIndex, TRUE);
                    activated = true;
                }
            }

            if (!activated)
                m_IpionManager->GetContactManager()->AddRecord(po, index, time);
        }
        else
        {
            m_IpionManager->GetContactManager()->RemoveRecord(po, index);
        }
    }

    virtual void event_friction_deleted(IVP_Event_Friction *friction)
    {
        if (!friction)
            return;

        IVP_Contact_Situation *situation = friction->contact_situation;
        IVP_Real_Object *obj = situation->objects[0];
        if (obj == m_RealObject)
            obj = situation->objects[1];

        CK3dEntity *ent = (CK3dEntity *)obj->client_data;
        if (!ent)
            return;

        int contactID = m_IpionManager->m_ContactManager->GetContactID(ent);
        if (contactID <= 0)
            return;

        ent = (CK3dEntity *)m_RealObject->client_data;
        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent);
        if (!po)
            return;

        PhysicsContactData *data = po->m_ContactData;
        if (!data)
            return;

        int index = contactID - 1;
        if (index < 0 || index >= data->m_GroupOutputCount)
            return;

        PhysicsContactData::GroupOutput &output = data->m_GroupOutputs[index];
        --output.number;
        if (output.number < 0)
            output.number = 0;

        if (output.number == 0 && output.active)
            m_IpionManager->GetContactManager()->AddRecord(po, index, m_IpionManager->GetSimulationTime());
    }

private:
    IVP_Real_Object *m_RealObject;
    CKIpionManager *m_IpionManager;
};

static void ClearPhysicsContactData(CKBehavior *beh, CKIpionManager *man, PhysicsContactData *data)
{
    if (!beh || !data)
        return;

    if (man && man->GetEnvironment())
    {
        CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
        if (ent)
        {
            PhysicsObject *po = man->GetPhysicsObject(ent);
            if (po && po->m_ContactData == data)
                po->m_ContactData = NULL;
        }
    }
    else
    {
        data->m_Listener = NULL;
        data->m_Manager = NULL;
    }

    delete data;

    PhysicsContactData *empty = NULL;
    beh->SetLocalParameterValue(1, &empty);
}

class PhysicsContinuousContactCallback : public PhysicsCallback
{
public:
    PhysicsContinuousContactCallback(CKIpionManager *man, CKBehavior *beh) : PhysicsCallback(man, beh, 2) {}

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
            return 1;
        CreatePhysicsContinuousContactData(m_IpionManager, po, beh);
        return 1;
    }
};

int CreatePhysicsContinuousContactData(CKIpionManager *manager,
                                       PhysicsObject *object,
                                       CKBehavior *behavior)
{
    if (!manager || !object || !object->m_RealObject || !behavior)
        return CKERR_INVALIDPARAMETER;
    if (object->m_ContactData)
        return CK_OK;

    float timeDelayStart = 0.1f;
    behavior->GetInputParameterValue(TIME_DELAY_START, &timeDelayStart);
    float timeDelayEnd = 0.1f;
    behavior->GetInputParameterValue(TIME_DELAY_END, &timeDelayEnd);

    int numberGroupOutput = 0;
    behavior->GetLocalParameterValue(0, &numberGroupOutput);
    if (numberGroupOutput < 0)
        numberGroupOutput = 0;
    const int maxGroupOutput = behavior->GetOutputCount() / 2;
    if (numberGroupOutput > maxGroupOutput)
        numberGroupOutput = maxGroupOutput;

    PhysicsContactData *data = new PhysicsContactData(
        timeDelayStart, timeDelayEnd, numberGroupOutput,
        manager->GetContactManager(), object, behavior);
    object->m_ContactData = data;
    data->m_Listener = new PhysicsContactListener(object->m_RealObject, manager);
    behavior->SetLocalParameterValue(1, &data);
    return CK_OK;
}

int PhysicsContinuousContact(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    CKIpionManager *man = CKIpionManager::GetManager(context);

    if (beh->IsInputActive(0))
    {
        PhysicsContactData *data = NULL;
        beh->GetLocalParameterValue(1, &data);
        if (!data)
        {
            CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
            if (!ent)
                return CKBR_OWNERERROR;

            if (!man || !man->GetEnvironment() || !man->m_PreSimulateCallbacks)
                return CKBR_GENERICERROR;

            PhysicsContinuousContactCallback *cb = new PhysicsContinuousContactCallback(man, beh);
            man->m_PreSimulateCallbacks->Process(cb);
        }

        beh->ActivateInput(0, FALSE);
        return CKBR_ACTIVATENEXTFRAME;
    }
    else if (beh->IsInputActive(1))
    {
        PhysicsContactData *data = NULL;
        beh->GetLocalParameterValue(1, &data);
        if (data)
        {
            ClearPhysicsContactData(beh, man, data);
        }

        beh->ActivateInput(1, FALSE);
        return CKBR_OK;
    }

    return CKBR_ACTIVATENEXTFRAME;
}

CKERROR PhysicsContinuousContactCallBack(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    if (!beh->GetOwner())
        return CKBR_OWNERERROR;

    if (behcontext.CallbackMessage == CKM_BEHAVIORRESET ||
        behcontext.CallbackMessage == CKM_BEHAVIORDELETE ||
        behcontext.CallbackMessage == CKM_BEHAVIORDETACH)
    {
        PhysicsContactData *data = NULL;
        beh->GetLocalParameterValue(1, &data);
        if (data)
        {
            CKIpionManager *man = CKIpionManager::GetManager(behcontext.Context);
            ClearPhysicsContactData(beh, man, data);
        }

        void *handle = NULL;
        beh->SetLocalParameterValue(1, &handle);
        return CKBR_OK;
    }

    if (behcontext.CallbackMessage == CKM_BEHAVIORSETTINGSEDITED)
    {
        int numberGroupOutput;
        beh->GetLocalParameterValue(0, &numberGroupOutput);

        while (beh->GetOutputCount() > 0)
            beh->DeleteOutput(0);

        char buf[256];
        for (int i = 0; i < numberGroupOutput; ++i)
        {
            snprintf(buf, sizeof(buf), "contact on %d", i + 1);
            beh->AddOutput(buf);
            snprintf(buf, sizeof(buf), "contact off %d", i + 1);
            beh->AddOutput(buf);
        }
    }

    return CKBR_OK;
}
