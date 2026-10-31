// Particle data and renderer registration have separate snapshot owners.
#include "CKAll.h"
#include "CKRenderCallbackSnapshot.h"
#include "RCK3dEntity.h"
#include "RCKRenderManager.h"
#include "ParticleSnapshot.h"
#include "ParticleGuids.h"
#include "TimePointEmitter.h"
#include "WaveEmitter.h"
#include "ParticleSystemRenderCallbacks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
extern void SetProcessorSpecific_FunctionsPtr();
int main() {
 SetProcessorSpecific_FunctionsPtr();if(CKStartUp()!=CK_OK)return 2;
 CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
 CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);CKBuildClassHierarchyTable();
 CKContext* ctx=nullptr;if(CKCreateContext(&ctx,nullptr,0,0)!=CK_OK)return 3;
 new RCKRenderManager(ctx);
 std::setbuf(stdout,nullptr);
 unsigned failures=0,checks=0;
 for(int kind=0;kind<3;++kind){
  bool wave=kind==2;
  auto* ent=static_cast<RCK3dEntity*>(ctx->CreateObject(CKCID_3DENTITY,"particle restore fixture"));
  auto* beh=static_cast<CKBehavior*>(ctx->CreateObject(CKCID_BEHAVIOR,"snapshot emitter"));
  if(!ent||!beh||!CKIsChildClassOf(ent,CKCID_OBJECT))return 9;
  beh->UseFunction();
  beh->SetPrototypeGuid(wave?WAVESYSTEM_GUID:TIMEPOINTSYSTEM_GUID);
  if(beh->GetPrototypeGuid()!=(wave?WAVESYSTEM_GUID:TIMEPOINTSYSTEM_GUID))return 7;
  std::unique_ptr<ParticleEmitter> emitter;
  if(wave)emitter.reset(new WaveEmitter(ctx,ent->GetID(),"wave"));
  else {auto* t=new TimePointEmitter(ctx,ent->GetID(),"timed");t->m_EmissionDelay=30;emitter.reset(t);}
  auto& e=*emitter;e.totalParticles=100;e.emitsPerFrame=1;e.m_Active=TRUE;e.m_Life=500;e.m_Speed=0;
  auto* param=beh->CreateLocalParameter("Emitter",CKPGUID_VOIDBUF);
  auto* ptr=emitter.get();if(!param||param->SetValue(&ptr,sizeof(ptr))!=CK_OK)return 4;
  e.m_RenderParticlesCallback=RenderParticles_P;
  // The actual script activation/deactivation helper changes this registration.
  ShowParticles(beh,TRUE);
  if(!ent->RemovePostRenderCallBack(RenderParticles_P,&e))return 10;
  ShowParticles(beh,TRUE);
  TT_ParticleSnapshotHandle saved=nullptr;
  if(TT_ParticleCaptureSnapshot(ctx,&saved)!=CK_OK)return 5;
  CKRenderCallbackSnapshot render=nullptr;
  if(CKCaptureRenderCallbacks(ctx,&render)!=CK_OK)return 12;
  ShowParticles(beh,FALSE);
  if(ent->RemovePostRenderCallBack(RenderParticles_P,&e))return 11;
  if(TT_ParticleRestoreSnapshot(ctx,saved)!=CK_OK)return 6;
  if(CKRestoreRenderCallbacks(ctx,render)!=CK_OK)return 13;
  CKDestroyRenderCallbacks(render);
  bool present=ent->RemovePostRenderCallBack(RenderParticles_P,&e)!=FALSE;
  ++checks;if(!present)++failures;
  std::printf("CALLBACK kind=%d expected=1 actual=%d\n",kind,present);
  TT_ParticleDestroySnapshot(saved);
  ptr=nullptr;param->SetValue(&ptr,sizeof(ptr));
  ctx->DestroyObject(beh->GetID());emitter.reset();ctx->DestroyObject(ent->GetID());
 }
 CKCloseContext(ctx);CKShutdown();std::printf("RESULT checks=%u differences=%u\n",checks,failures);return failures?1:0;
}
