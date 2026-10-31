#include "CKAll.h"
#include "CKIpionManager.h"
#include "PhysicsContact.h"
#include <cstdio>

int main() {
    if (CKStartUp()!=CK_OK) return 90;
    CKContext *context=nullptr;
    if (CKCreateContext(&context,nullptr,0,0)!=CK_OK) return 91;
    auto *manager=new CKIpionManager(context);
    manager->CreateEnvironment();
    auto *a=static_cast<CKBeObject *>(context->CreateObject(CKCID_BEOBJECT,"owner A"));
    auto *b=static_cast<CKBeObject *>(context->CreateObject(CKCID_BEOBJECT,"owner B"));
    auto *behavior=static_cast<CKBehavior *>(context->CreateObject(CKCID_BEHAVIOR,"contact"));
    if (!a || !b || !behavior) return 92;
    behavior->SetCompatibleClassID(CKCID_BEOBJECT);
    behavior->SetAsTargetable(TRUE);
    if (behavior->UseTarget(TRUE)!=CK_OK) return 93;
    auto *target=context->CreateCKParameterLocal("contact target",CKPGUID_BEOBJECT);
    if (!target || !behavior->GetTargetParameter()) return 94;
    behavior->GetTargetParameter()->SetDirectSource(target);
    if (!behavior->CreateLocalParameter("count",CKPGUID_INT) ||
        !behavior->CreateLocalParameter("handle",CKPGUID_POINTER)) return 95;
    manager->m_PhysicsObjects.Insert(a->GetID(),PhysicsObject());
    manager->m_PhysicsObjects.Insert(b->GetID(),PhysicsObject());
    auto *owner=&*manager->m_PhysicsObjects.Find(a->GetID());
    auto *other=&*manager->m_PhysicsObjects.Find(b->GetID());
    auto *contacts=manager->GetContactManager();
    int failed=0;
    for (int mode=0;mode<3;++mode) {
        CK_ID targetId=mode==1?0:b->GetID();
        target->SetValue(&targetId,sizeof(targetId));
        if (behavior->GetTarget()!=(mode==1?nullptr:b)) return 96;
        auto *data=new PhysicsContactData(.1f,.2f,1,contacts,owner,behavior);
        owner->m_ContactData=data;
        contacts->AddRecord(owner,0,manager->GetSimulationTime());
        PhysicsContactData *replacement=mode==2
            ? new PhysicsContactData(.3f,.4f,1,contacts,other,behavior):nullptr;
        other->m_ContactData=replacement;
        auto *handle=replacement?replacement:data;
        behavior->SetLocalParameterValue(1,&handle);
        delete data;
        handle=nullptr;behavior->GetLocalParameterValue(1,&handle);
        bool detached=owner->m_ContactData==nullptr;
        bool records=contacts->GetRecordCount()==0;
        bool bound=handle==replacement;
        bool otherIntact=other->m_ContactData==replacement;
        std::printf("mode=%d owner_detached=%d records_removed=%d handle_correct=%d other_intact=%d\n",mode,detached,records,bound,otherIntact);
        failed+=!(detached && records && bound && otherIntact);
        // A failing pre-fix run must record the violation without a second free.
        owner->m_ContactData=nullptr;
        other->m_ContactData=nullptr;
        delete replacement;
        handle=nullptr;behavior->SetLocalParameterValue(1,&handle);
    }
    auto *first=new PhysicsContactData(.1f,.2f,1,contacts,owner,behavior);
    auto *second=new PhysicsContactData(.1f,.2f,1,contacts,other,behavior);
    owner->m_ContactData=first;other->m_ContactData=second;
    contacts->AddRecord(owner,0,manager->GetSimulationTime());
    contacts->AddRecord(other,0,manager->GetSimulationTime());
    behavior->SetLocalParameterValue(1,&second);
    if (contacts->GetRecordCount()!=2) return 97;
    manager->m_PhysicsObjects.Clear();
    PhysicsContactData *remaining=nullptr;behavior->GetLocalParameterValue(1,&remaining);
    bool tableClean=manager->m_PhysicsObjects.Size()==0 && contacts->GetRecordCount()==0 && remaining==nullptr;
    std::printf("whole_table_clean=%d\n",tableClean);
    failed+=!tableClean;
    CKCloseContext(context);CKShutdown();
    std::printf("ownership cases=4 failed=%d\n",failed);
    return failed?1:0;
}
