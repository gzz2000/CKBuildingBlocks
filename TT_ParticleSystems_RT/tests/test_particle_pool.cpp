#include "CKAll.h"
#include "RCK3dEntity.h"
#include "RCKRenderManager.h"
#include "ParticleSnapshot.h"
#include "ParticleManager.h"
#include "ParticleGuids.h"
#include "PointEmitter.h"
#include <vector>
#include <array>
#include <memory>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdio>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks=0,failures=0;
static void Check(bool b,const char* label){++checks;if(!b){++failures;std::printf("FAIL %s\n",label);}}
static Particle* Base(ParticleEmitter& e){return reinterpret_cast<Particle*>((reinterpret_cast<std::uintptr_t>(e.m_BackPool+15))&~std::uintptr_t(15));}
static std::vector<unsigned char> Payload(ParticleEmitter& e){
 std::vector<unsigned char> bytes(e.m_MaximumParticles*sizeof(Particle));std::memcpy(bytes.data(),Base(e),bytes.size());
 for(int i=0;i<e.m_MaximumParticles;++i){std::memset(bytes.data()+i*sizeof(Particle)+offsetof(Particle,prev),0,sizeof(Particle*));std::memset(bytes.data()+i*sizeof(Particle)+offsetof(Particle,next),0,sizeof(Particle*));}return bytes;
}
static std::vector<int> Links(ParticleEmitter& e){
 auto* base=Base(e);std::vector<int> r{e.particleCount};
 for(auto* p:e.particles?std::array<Particle*,2>{e.particles,e.m_Pool}:std::array<Particle*,2>{nullptr,e.m_Pool}){
  int count=0;for(;p;p=p->next){r.push_back(int(p-base));if(++count>e.m_MaximumParticles)return {-999};}r.push_back(-1);
 }return r;
}
int main(){
 SetProcessorSpecific_FunctionsPtr();if(CKStartUp()!=CK_OK)return 2;
 CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);CKBuildClassHierarchyTable();
 CKContext* ctx=nullptr;if(CKCreateContext(&ctx,nullptr,0,0)!=CK_OK)return 3;
 new RCKRenderManager(ctx);new ParticleManager(ctx);
 auto* ent=static_cast<CK3dEntity*>(ctx->CreateObject(CKCID_3DENTITY,"pool emitter owner"));
 auto* beh=static_cast<CKBehavior*>(ctx->CreateObject(CKCID_BEHAVIOR,"pool emitter"));beh->UseFunction();beh->SetPrototypeGuid(POINTSYSTEM_GUID);
 std::unique_ptr<PointEmitter> ptr(new PointEmitter(ctx,ent->GetID(),"point"));auto& e=*ptr;
 auto* param=beh->CreateLocalParameter("Emitter",CKPGUID_VOIDBUF);ParticleEmitter* raw=ptr.get();if(param->SetValue(&raw,sizeof(raw))!=CK_OK)return 4;
 e.totalParticles=e.m_MaximumParticles;e.emitsPerFrame=2;e.m_Life=20;e.m_Speed=0;
 // Initialize fixture storage only. Production allocator is NOT changed.
 auto* base=Base(e);for(int i=0;i<e.m_MaximumParticles;++i){std::memset(base+i,0x3f,sizeof(Particle));base[i].next=i+1<e.m_MaximumParticles?base+i+1:nullptr;base[i].prev=reinterpret_cast<Particle*>(std::uintptr_t(1));}
 TT_ParticleSnapshotHandle saved=nullptr;
 for(int slot=0;slot<3;++slot){
  if(slot==1)e.AddParticles();
  if(slot==2){e.UpdateParticles(100);e.UpdateParticles(100);e.AddParticles();}
  if(saved)TT_ParticleDestroySnapshot(saved);
  Check(TT_ParticleCaptureSnapshot(ctx,&saved)==CK_OK,"valid capture");
  auto expectedPayload=Payload(e);auto expectedLinks=Links(e);
  CKSrand(315);e.AddParticles();auto nextPayload=Payload(e);auto nextLinks=Links(e);
  for(int repeat=0;repeat<3;++repeat){
   e.UpdateParticles(100);e.UpdateParticles(100);e.AddParticles();e.AddParticles();
   Check(TT_ParticleRestoreSnapshot(ctx,saved)==CK_OK,"restore after reuse");
   Check(Payload(e)==expectedPayload,"all non-pointer slot bytes restored");
   Check(Links(e)==expectedLinks,"pool order restored");
   CKSrand(315);e.AddParticles();
   Check(Payload(e)==nextPayload&&Links(e)==nextLinks,"next real emissions preserved");
  }
 }
 TT_ParticleDestroySnapshot(saved);saved=nullptr;
 // Establish two valid live nodes plus remaining free pool for each mutation.
 e.UpdateParticles(100);e.UpdateParticles(100);e.AddParticles();
 Check(e.particleCount==2&&e.particles&&e.particles->next&&e.m_Pool,"malformed fixture precondition");
 std::vector<unsigned char> original(e.m_MaximumParticles*sizeof(Particle));std::memcpy(original.data(),base,original.size());
 auto* active=e.particles;auto* freeHead=e.m_Pool;int count=e.particleCount;
 const char* labels[]={"free cycle","active cycle","overlapping lists","outside free head","misaligned next","wrong count","broken active backlink","orphaned slots","outside active head"};
 for(int bad=0;bad<9;++bad){
  std::memcpy(base,original.data(),original.size());e.particles=active;e.m_Pool=freeHead;e.particleCount=count;
  switch(bad){case 0:freeHead->next=freeHead;break;case 1:active->next->next=active;break;case 2:e.m_Pool=active;break;case 3:e.m_Pool=base+e.m_MaximumParticles;break;case 4:freeHead->next=reinterpret_cast<Particle*>(reinterpret_cast<unsigned char*>(base)+1);break;case 5:++e.particleCount;break;case 6:active->next->prev=nullptr;break;case 7:freeHead->next=nullptr;break;case 8:e.particles=base+e.m_MaximumParticles;break;}
  auto before=std::vector<unsigned char>(original.size());std::memcpy(before.data(),base,before.size());auto* ah=e.particles;auto* fh=e.m_Pool;int c=e.particleCount;
  TT_ParticleSnapshotHandle invalid=nullptr;int result=TT_ParticleCaptureSnapshot(ctx,&invalid);
  Check(result!=CK_OK&&invalid==nullptr,labels[bad]);
  Check(std::memcmp(before.data(),base,before.size())==0&&e.particles==ah&&e.m_Pool==fh&&e.particleCount==c,"invalid capture is read-only");
  if(invalid)TT_ParticleDestroySnapshot(invalid);
 }
 std::memcpy(base,original.data(),original.size());e.particles=active;e.m_Pool=freeHead;e.particleCount=count;
 raw=nullptr;param->SetValue(&raw,sizeof(raw));ptr.reset();CKCloseContext(ctx);CKShutdown();
 std::printf("RESULT checks=%u differences=%u\n",checks,failures);return failures?1:0;
}
