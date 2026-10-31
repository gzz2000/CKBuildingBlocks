// Real emission, rewind and same-address owner lifetime regression.
#include "CKAll.h"
#include "RCK3dEntity.h"
#include "ParticleSnapshot.h"
#include "ParticleGuids.h"
#include "TimePointEmitter.h"
#include "WaveEmitter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
#include <new>
extern void SetProcessorSpecific_FunctionsPtr();
using Row=std::vector<float>;
static Row Observe(ParticleEmitter& e) {
 Row r{float(e.particleCount)};int count=0;
 for(auto* p=e.particles;p;p=p->next) {
  r.insert(r.end(),{p->pos.x,p->pos.y,p->pos.z,p->m_Angle,p->m_Life,
    p->m_Color.r,p->m_Color.g,p->m_Color.b,p->m_Color.a,p->m_Density});
  if(++count>e.m_MaximumParticles){std::fprintf(stderr,"cycle in pool\n");std::exit(20);}
 }
 if(count!=e.particleCount){std::fprintf(stderr,"count mismatch\n");std::exit(21);}
 return r;
}
static void Advance(ParticleEmitter& e, RCK3dEntity* ent,bool wave,int step,bool branch=false) {
 VxVector pos=branch?VxVector(float(-step*3),2,float(step*5)):VxVector(float(step),0,float(step%3));
 ent->SetPosition(&pos);e.m_DeltaTime=17;
 if(wave)e.AddParticles3();else e.AddParticles2();
}
int main() {
 SetProcessorSpecific_FunctionsPtr();if(CKStartUp()!=CK_OK)return 2;
 CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
 CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);CKBuildClassHierarchyTable();
 CKContext* ctx=nullptr;if(CKCreateContext(&ctx,nullptr,0,0)!=CK_OK)return 3;
 unsigned failures=0,checks=0;
 for(int kind=0;kind<3;++kind){
  bool wave=kind==2;
  auto* ent=static_cast<RCK3dEntity*>(ctx->CreateObject(CKCID_3DENTITY,"particle restore fixture"));
  auto* beh=static_cast<CKBehavior*>(ctx->CreateObject(CKCID_BEHAVIOR,"snapshot emitter"));
  if(!ent||!beh)return 9;
  beh->UseFunction();
  beh->SetPrototypeGuid(wave?WAVESYSTEM_GUID:TIMEPOINTSYSTEM_GUID);
  if(beh->GetPrototypeGuid()!=(wave?WAVESYSTEM_GUID:TIMEPOINTSYSTEM_GUID))return 7;
  std::unique_ptr<ParticleEmitter> emitter;
  if(wave)emitter.reset(new WaveEmitter(ctx,ent->GetID(),"wave"));
  else {auto* t=new TimePointEmitter(ctx,ent->GetID(),"timed");t->m_EmissionDelay=30;emitter.reset(t);}
  auto& e=*emitter;e.totalParticles=100;e.emitsPerFrame=1;e.m_Active=TRUE;e.m_Life=500;e.m_Speed=0;
  auto* param=beh->CreateLocalParameter("Emitter",CKPGUID_VOIDBUF);
  auto* ptr=emitter.get();if(!param||param->SetValue(&ptr,sizeof(ptr))!=CK_OK)return 4;
  if(kind!=0)for(int i=0;i<3;++i)Advance(e,ent,wave,i);
  TT_ParticleSnapshotHandle saved=nullptr;
  for(int slot=0;slot<2;++slot){
   if(saved)TT_ParticleDestroySnapshot(saved);
   if(TT_ParticleCaptureSnapshot(ctx,&saved)!=CK_OK)return 5;
   int savedCount=e.particleCount;
   VxMatrix matrix=ent->GetWorldMatrix();
   CKSrand(12345);std::vector<Row> expected;
   for(int i=0;i<5;++i){Advance(e,ent,wave,i+10*slot);expected.push_back(Observe(e));}
   for(int repeat=0;repeat<3;++repeat){
    for(int i=0;i<4;++i)Advance(e,ent,wave,i+20,true);
    if(TT_ParticleRestoreSnapshot(ctx,saved)!=CK_OK)return 6;
    if(e.particleCount!=savedCount){std::fprintf(stderr,"snapshot did not restore pool count\n");return 8;}
    ent->SetWorldMatrix(matrix);CKSrand(12345);
    for(int i=0;i<5;++i){
     Advance(e,ent,wave,i+10*slot);auto got=Observe(e);++checks;
     bool equal=got.size()==expected[i].size()&&std::memcmp(got.data(),expected[i].data(),got.size()*sizeof(float))==0;
     if(!equal){++failures;std::printf("DIFF kind=%d slot=%d restore=%d step=%d count=%g/%g\n",kind,slot,repeat,i,got[0],expected[i][0]);}
    }
   }
  }
  // Destroy/reconstruct at the same address to exercise pointer/ID ABA.
  auto* storage=emitter.release();
  if(wave)static_cast<WaveEmitter*>(storage)->~WaveEmitter();
  else static_cast<TimePointEmitter*>(storage)->~TimePointEmitter();
  if(wave)emitter.reset(new(storage) WaveEmitter(ctx,ent->GetID(),"replacement"));
  else emitter.reset(new(storage) TimePointEmitter(ctx,ent->GetID(),"replacement"));
  ptr=emitter.get();param->SetValue(&ptr,sizeof(ptr));
  emitter->particleCount=77;
  if(TT_ParticleValidateSnapshot(ctx,saved)==CK_OK ||
     TT_ParticleRestoreSnapshot(ctx,saved)==CK_OK || emitter->particleCount!=77)return 22;
  std::printf("LIFETIME kind=%d same_address=1 rejected=1 unchanged=1\n",kind);
  TT_ParticleDestroySnapshot(saved);
  ptr=nullptr;param->SetValue(&ptr,sizeof(ptr));
  ctx->DestroyObject(beh->GetID());emitter.reset();ctx->DestroyObject(ent->GetID());
 }
 CKCloseContext(ctx);CKShutdown();std::printf("RESULT checks=%u differences=%u\n",checks,failures);return failures?1:0;
}
