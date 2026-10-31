#include "CKIpionManager.h"
#include "CK3dEntity.h"
#include "CKGlobals.h"
#include "CKPluginManager.h"
#include "PhysicsRuntimeApi.h"
#include "PhysicsSnapshot.h"
#include "ivp_physic_private.hxx"
#include "ivp_collision_checkpoint.hxx"
#include <memory>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void Require(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
struct Fixture {
    CKContext *context=nullptr;
    CKIpionManager *manager=nullptr;
    std::array<CK3dEntity *,2> entities{};
    std::optional<IVP_Material_Simple> external;
    IVP_Material *material=nullptr;
    TT_PhysicsSnapshotHandle saved=nullptr;
    Fixture(int owners) {
        Require(CKCreateContext(&context,nullptr,0,0)==CK_OK,"context");
        context->SetInterfaceMode(FALSE,[](CKUICallbackStruct &event,void *) -> CKERROR {
            if(event.Reason==CKUIM_OUTTOCONSOLE && event.ConsoleString)
                std::fprintf(stderr,"CK console: %s\n",event.ConsoleString);
            return CK_OK;
        });
        manager=CKIpionManager::GetManager(context);Require(manager!=nullptr,"manager");manager->CreateEnvironment();
        if(owners)material=new IVP_Material_Simple(0.4f,0.5f);
        else {external.emplace(0.4f,0.5f);material=&*external;}
        for(int i=0;i<2;++i) {
            entities[i]=static_cast<CK3dEntity *>(context->CreateObject(CKCID_3DOBJECT));
            Require(entities[i]!=nullptr,"entity");VxVector position(float(i*4),8,3);entities[i]->SetPosition(&position);
            Require(manager->CreatePhysicsObjectOnParameters(entities[i],0,nullptr,0,nullptr,nullptr,
                0,nullptr,1,nullptr,nullptr,FALSE,material,1,const_cast<char *>("mat"),FALSE,FALSE,TRUE,
                0.1f,0.1f)==CK_OK,"physical ball");
            if(i<owners)manager->OwnMaterial(entities[i],material);
        }
        manager->GetEnvironment()->simulate_dtime(0.05f);
    }
    ~Fixture() { TT_PhysicsDestroySnapshot(saved);if(context)CKCloseContext(context); }
    IVP_Real_Object *Body(int i) {
        auto *object=manager->GetPhysicsObject(entities[i]);return object?object->m_RealObject:nullptr;
    }
    void Retire(int i) { auto *body=Body(i);Require(body!=nullptr,"retire live body");body->delete_silently();Require(Body(i)==nullptr,"native retirement callback"); }
    void Save() {
        TT_PhysicsSnapshotHandle next=nullptr;Require(TT_PhysicsCaptureSnapshot(context,&next)==CK_OK,"capture");
        TT_PhysicsDestroySnapshot(saved);saved=next;
    }
    void Restore() { Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK,"material restore"); }
};
void Shared(int owners) {
    Fixture f(owners);f.Save();f.Retire(0);
    // The surviving body must still refer to a usable shared material.
    Require(f.Body(1)->l_default_material->get_friction_factor()==double(0.4f),"shared material freed with first owner");
    f.Restore();
    for(int repeat=0;repeat<3;++repeat) {
        Require(f.Body(0)->l_default_material==f.Body(1)->l_default_material,"shared material split");
        if(!owners)Require(f.Body(0)->l_default_material==&*f.external,"borrowed material identity replaced");
        Require(f.manager->m_MaterialOwners.len()==owners,"ownership associations changed");
        f.Retire(0);f.Retire(1);
        if(!owners)f.external->friction_value=0.9;
        f.Restore();f.Restore();
        Require(f.Body(0)->l_default_material->get_friction_factor()==double(0.4f),"material values not rewound");
        f.Save(); // Overwrite after graph/material rebinding.
    }
    std::printf("material shared owners=%d passed\n",owners);
}
void DeferredOwner() {
    Fixture f(1);f.Retire(0);
    Require(f.manager->m_MaterialOwners.len()==0,"retired ownership association remains");
    Require(f.Body(1)->l_default_material->get_friction_factor()==double(0.4f),"remaining user lost material");
    f.Save();
    for(int i=0;i<3;++i) {
        f.Retire(1);f.Restore();f.Restore();
        Require(f.Body(0)==nullptr && f.Body(1)!=nullptr,"wrong restored body inventory");
        Require(f.manager->m_MaterialOwners.len()==0,"invented material owner");
        f.Save();
    }
    std::printf("material deferred owner passed\n");
}
void ExpiredExternal(bool reuse) {
    Fixture f(0);f.Save();f.Retire(0);f.Retire(1);
    auto *old=f.material;f.external.reset();
    if(reuse) {f.external.emplace(0.8f,0.2f);Require(&*f.external==old,"fixture did not reuse storage");}
    // A new live body would be retired by an unsafe restore before it notices
    // the expired material. Preserve the entire current IVP graph, not just counts.
    auto *spectator=static_cast<CK3dEntity *>(f.context->CreateObject(CKCID_3DOBJECT));
    auto *spectatorMaterial=new IVP_Material_Simple(0.3,0.6);
    Require(f.manager->CreatePhysicsObjectOnParameters(spectator,0,nullptr,0,nullptr,nullptr,
        0,nullptr,1,nullptr,nullptr,FALSE,spectatorMaterial,1,const_cast<char *>("other"),
        FALSE,FALSE,TRUE,0.1f,0.1f)==CK_OK,"spectator body");
    f.manager->OwnMaterial(spectator,spectatorMaterial);
    f.manager->GetEnvironment()->simulate_dtime(0.05f);
    char error[256];
    std::unique_ptr<IVP_Collision_Checkpoint> graphBefore(IVP_Collision_Checkpoint::capture(
        f.manager->GetEnvironment(),error,sizeof(error)));
    Require(bool(graphBefore),"capture graph before rejected restore");
    const double replacementFriction=reuse?f.external->friction_value:0.0;
    const int objects=f.manager->GetPhysicsObjectCount(),owners=f.manager->m_MaterialOwners.len();
    CKIpionManager::SimulationClockState before{},after{};
    f.manager->CaptureSimulationClock(before);
    Require(TT_PhysicsRestoreSnapshot(f.context,f.saved)!=CK_OK,"accepted expired external material");
    f.manager->CaptureSimulationClock(after);
    Require(f.manager->GetPhysicsObjectCount()==objects && f.manager->m_MaterialOwners.len()==owners &&
        std::memcmp(&before,&after,sizeof(before))==0,"failed material preflight mutated world");
    std::unique_ptr<IVP_Collision_Checkpoint> graphAfter(IVP_Collision_Checkpoint::capture(
        f.manager->GetEnvironment(),error,sizeof(error)));
    Require(graphAfter && graphBefore->compare_captured_state(*graphAfter,stderr),
            "failed material preflight changed live graph/cache state");
    if(reuse)Require(f.external->friction_value==replacementFriction,"replacement material was overwritten");
    std::printf("material expired reuse=%d rejected without mutation\n",int(reuse));
}
}
int main(int argc,char **argv) {
    try {
        Require(CKStartUp()==CK_OK,"startup");
        Require(argc>=3 && CKGetPluginManager()->RegisterPlugin(argv[1])==CK_OK &&
            CKGetPluginManager()->RegisterPlugin(argv[2])==CK_OK,"plugins");
        const std::string selected=argc>3?argv[3]:"all";
        for(int mode:{0,1}) {
#ifndef IVP_EMULATE_X87
            if(mode)continue;
#endif
            Require(TT_PhysicsSetSolverMode(mode)!=0,"solver mode");
            if(selected=="all" || selected=="external")Shared(0);
            if(selected=="all" || selected=="managed-one")Shared(1);
            if(selected=="all" || selected=="managed-two")Shared(2);
            if(selected=="all" || selected=="deferred")DeferredOwner();
            if(selected=="all" || selected=="expired")ExpiredExternal(false);
            if(selected=="all" || selected=="reused")ExpiredExternal(true);
            std::printf("material mode=%d case=%s passed\n",mode,selected.c_str());
        }
        CKShutdown();return 0;
    } catch(const std::exception &e) {std::fprintf(stderr,"Material snapshot failed: %s\n",e.what());return 1;}
}
