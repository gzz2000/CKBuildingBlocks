#include "CKIpionManager.h"
#include "CK3dEntity.h"
#include "CKGlobals.h"
#include "CKPluginManager.h"
#include "PhysicsRuntimeApi.h"
#include "PhysicsSnapshot.h"
#include "ivp_physic_private.hxx"
#include "ivp_cache_object.hxx"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct CacheImage {
    IVP_Time_CODE code;
    std::array<unsigned char, sizeof(IVP_U_Quat)> rotation;
    std::array<unsigned char, sizeof(IVP_U_Matrix)> matrix;
    std::array<unsigned char, sizeof(IVP_U_Point)> position;
    explicit CacheImage(IVP_Real_Object *object) {
        const auto *cache=object->get_cache_object_no_lock();
        code=cache->get_cached_time_code();
        std::memcpy(rotation.data(),&cache->q_world_f_object,rotation.size());
        std::memcpy(matrix.data(),&cache->m_world_f_object,matrix.size());
        std::memcpy(position.data(),&cache->core_pos,position.size());
    }
    bool Same(const CacheImage &other) const {
        return code==other.code && rotation==other.rotation && matrix==other.matrix && position==other.position;
    }
};
struct Fixture {
    CKContext *context=nullptr;
    CKIpionManager *manager=nullptr;
    CK3dEntity *entity=nullptr;
    IVP_Material_Simple *material=nullptr;
    TT_PhysicsSnapshotHandle saved=nullptr;
    Fixture() {
        Require(CKCreateContext(&context,nullptr,0,0)==CK_OK,"create context");
        context->SetInterfaceMode(FALSE,[](CKUICallbackStruct &event,void *) -> CKERROR {
            if(event.Reason==CKUIM_OUTTOCONSOLE && event.ConsoleString)
                std::fprintf(stderr,"CK console: %s\n",event.ConsoleString);
            return CK_OK;
        });
        manager=CKIpionManager::GetManager(context);
        Require(manager!=nullptr,"physics manager");
        manager->CreateEnvironment();
        entity=static_cast<CK3dEntity *>(context->CreateObject(CKCID_3DOBJECT));
        Require(entity!=nullptr,"create entity");
        VxVector initial(7.0f,11.0f,13.0f);
        entity->SetPosition(&initial);
        // Match Physicalize: the manager owns materials used by game bodies.
        material=new IVP_Material_Simple(0.4f,0.5f);
        manager->OwnMaterial(entity,material);
        Require(manager->CreatePhysicsObjectOnParameters(entity,0,nullptr,
            0,nullptr,nullptr,0,nullptr,1.0f,nullptr,nullptr,FALSE,
            material,1.0f,const_cast<char *>("cache"),FALSE,FALSE,TRUE,
            0.1f,0.1f)==CK_OK,"create physical ball");
        Advance();
    }
    ~Fixture() { TT_PhysicsDestroySnapshot(saved);if(context)CKCloseContext(context); }
    IVP_Real_Object *Body() { return manager->GetPhysicsObject(entity)->m_RealObject; }
    void Advance() { manager->GetEnvironment()->simulate_dtime(0.05f); }
    void Save() {
        TT_PhysicsSnapshotHandle next=nullptr;
        Require(TT_PhysicsCaptureSnapshot(context,&next)==CK_OK,"capture snapshot");
        TT_PhysicsDestroySnapshot(saved);saved=next;
    }
    void PoolReuseRoundTrip() {
        // Keep the legacy reset contract: allocation at code 0 can expose an
        // unowned slot's prior contents instead of immediately recomputing it.
        Advance();
        const CacheImage populated(Body());
        manager->ResetSimulationClock();
        Save();
        auto consume=[&]() {
            std::vector<CacheImage> images;
            for(int i=0;i<1024;++i) {
                IVP_Cache_Object_Manager::invalid_cache_object(Body());
                images.emplace_back(Body());
            }
            return images;
        };
        const auto expected=consume();
        for(int repeat=0;repeat<3;++repeat) {
            Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK,"restore cache pool history");
            const auto actual=consume();
            Require(actual.size()==expected.size(),"cache allocation count");
            for(size_t i=0;i<actual.size();++i)
                Require(actual[i].Same(expected[i]),"cache pool allocation history differs");
        }
        Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK,"restore after cache eviction branches");
        Require(CacheImage(Body()).Same(populated),"original cache owner lost after pool reuse");
        std::printf("cache pool 1024-eviction branches passed\n");
    }
    void OverwriteAndRecreate() {
        // A prior TAS reset intentionally leaves positive-generation caches.
        // Advance real simulation until this cache is eligible for refresh.
        const auto staleGeneration=CacheImage(Body()).code;
        for(int i=0;i<1024 && manager->GetEnvironment()->get_current_time_code()<=staleGeneration;++i)
            Advance();
        Require(manager->GetEnvironment()->get_current_time_code()>staleGeneration,
                "simulation failed to advance beyond old cache generation");
        const CacheImage first(Body());
        Save();
        Advance();
        const CacheImage replacement(Body());
        Require(!first.Same(replacement), "overwrite fixture did not advance cache");
        Save(); // Replace the prior snapshot, then retire the live physics body.
        const auto savedBody=reinterpret_cast<uintptr_t>(Body());
        const auto savedMaterial=reinterpret_cast<uintptr_t>(Body()->l_default_material);
        Body()->delete_silently(); // Physicalize off-input: deletion callback removes host entry.
        Require(manager->GetPhysicsObject(entity)==nullptr, "body was not removed");
        for(int repeat=0;repeat<3;++repeat) {
            const int restored=TT_PhysicsRestoreSnapshot(context,saved);
            std::fprintf(stderr,"cache lifecycle repeat=%d body_reused=%d material_reused=%d result=%d\n",
                repeat,int(reinterpret_cast<uintptr_t>(Body())==savedBody),
                int(reinterpret_cast<uintptr_t>(Body()->l_default_material)==savedMaterial),restored);
            Require(restored==CK_OK, "restore retired cache owner");
            Require(CacheImage(Body()).Same(replacement), "recreated owner cache differs");
            Advance();
            CacheImage abandoned(Body());
        }
        Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK, "restore before borrowed-cache test");
        auto *borrowed=Body()->get_cache_object();
        const CacheImage before(Body());
        TT_PhysicsCoreState coreBefore{},coreAfter{};
        Require(TT_PhysicsGetCoreState(Body(),&coreBefore)!=0,"observe locked core");
        TT_PhysicsSnapshotHandle rejected=nullptr;
        const int capture=TT_PhysicsCaptureSnapshot(context,&rejected);
        const int restore=TT_PhysicsRestoreSnapshot(context,saved);
        const CacheImage after(Body());
        Require(TT_PhysicsGetCoreState(Body(),&coreAfter)!=0,"observe core after rejection");
        borrowed->remove_reference();
        TT_PhysicsDestroySnapshot(rejected);
        Require(capture!=CK_OK && rejected==nullptr && restore!=CK_OK,
                "accepted snapshot operation while transform cache borrowed");
        Require(before.Same(after) && std::memcmp(&coreBefore,&coreAfter,sizeof(coreBefore))==0,
                "borrowed-cache rejection mutated body state");
        Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK,"restore after releasing cache");
        Require(CacheImage(Body()).Same(replacement), "released cache failed rewind");
        std::printf("cache overwrite/recreate/reject-borrowed passed\n");
    }
    bool RoundTrip(bool resetClock) {
        const CacheImage populated(Body());
        Require(populated.code>0,"positive-generation cache was not populated");
        if(resetClock) {
            manager->ResetSimulationClock();
            Require(manager->GetEnvironment()->get_current_time_code()==0,"actual TAS clock reset");
        }
        const CacheImage expected(Body());
        if(resetClock)Require(expected.Same(populated),"TAS reset unexpectedly refreshed cache");
        Save();
        bool pass=true;
        for(int repeat=0;repeat<3;++repeat) {
            Advance();
            const CacheImage abandoned(Body());
            Require(TT_PhysicsRestoreSnapshot(context,saved)==CK_OK,"restore snapshot");
            const CacheImage actual(Body());
            const bool equal=actual.Same(expected);pass=equal && pass;
            std::printf("cache reset=%d repeat=%d saved_code=%d actual_code=%d transform_equal=%d pass=%d\n",
                int(resetClock),repeat,int(expected.code),int(actual.code),
                int(actual.matrix==expected.matrix && actual.rotation==expected.rotation && actual.position==expected.position),int(equal));
        }
        return pass;
    }
};
}
int main(int argc,char **argv) {
    try {
        Require(CKStartUp()==CK_OK,"startup");
        Require(argc==3 && CKGetPluginManager()->RegisterPlugin(argv[1])==CK_OK &&
            CKGetPluginManager()->RegisterPlugin(argv[2])==CK_OK,"register plugins");
        bool pass=true;
        for(int mode:{0,1}) {
#ifndef IVP_EMULATE_X87
            if(mode==1)continue;
#endif
            Require(TT_PhysicsSetSolverMode(mode)!=0,"set solver mode");
            for(bool reset:{false,true}) {
                Fixture fixture;
                const bool result=fixture.RoundTrip(reset);pass=result && pass;
                fixture.PoolReuseRoundTrip();
                fixture.OverwriteAndRecreate();
                std::printf("cache mode=%d reset=%d result=%d\n",mode,int(reset),int(result));
            }
        }
        CKShutdown();
        return pass?0:1;
    } catch(const std::exception &e) {
        std::fprintf(stderr,"Cache snapshot test failed: %s\n",e.what());return 2;
    }
}
