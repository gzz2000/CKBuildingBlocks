#include "CKAll.h"
#include "RCK3dEntity.h"
#include "RCKRenderManager.h"
#include "RCKTexture.h"
#include "RCKMesh.h"
#include "RCKCurve.h"
#include "ParticleSnapshot.h"
#include "ParticleManager.h"
#include "ParticleGuids.h"
#include "PointEmitter.h"
#include "ObjectEmitter.h"
#include "CurveEmitter.h"
#include <memory>
#include <cstdio>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks=0,failures=0;
static void Check(bool b,const char* label,int kind){++checks;if(!b){++failures;std::printf("FAIL kind=%d %s\n",kind,label);}}
int main(){
 SetProcessorSpecific_FunctionsPtr();if(CKStartUp()!=CK_OK)return 2;
 CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
 CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);
 CKCLASSREGISTERCID(RCKTexture,CKCID_BEOBJECT);
 CKCLASSREGISTERCID(RCKMesh,CKCID_BEOBJECT);
 CKCLASSREGISTERCID(RCKCurve,CKCID_3DENTITY);CKBuildClassHierarchyTable();
 for(int kind=0;kind<7;++kind){
  CKContext* ctx=nullptr;if(CKCreateContext(&ctx,nullptr,0,0)!=CK_OK)return 3;
  new RCKRenderManager(ctx);auto* manager=new ParticleManager(ctx);
  auto* ent=static_cast<CK3dEntity*>(ctx->CreateObject(CKCID_3DENTITY,"emitter owner"));
  auto* ref=ctx->CreateObject(kind==1?CKCID_TEXTURE:kind==2?CKCID_GROUP:(kind==3||kind==4)?CKCID_MESH:kind==5?CKCID_CURVE:CKCID_3DENTITY,"saved resource");
  if(!ent||!ref)return 4;
  std::unique_ptr<ParticleEmitter> first(new PointEmitter(ctx,ent->GetID(),"first")),second;
  if(kind==4)second.reset(new ObjectEmitter(ctx,ent->GetID(),"object"));
  else if(kind==5)second.reset(new CurveEmitter(ctx,ref->GetID(),"curve"));
  else second.reset(new PointEmitter(ctx,ent->GetID(),"second"));
  // Keep typed curve reference independent of the common m_Entity reference.
  second->m_Entity=ent->GetID();
  if(kind==0)second->m_Entity=ref->GetID();
  if(kind==1)second->m_Texture=ref->GetID();
  if(kind==2)second->m_Group=ref->GetID();
  if(kind==3)second->m_Mesh=ref->GetID();
  if(kind==4)static_cast<ObjectEmitter*>(second.get())->m_Shape=static_cast<CKMesh*>(ref);
  if(kind==6){ParticleImpact impact{};impact.m_Object=static_cast<CK3dEntity*>(ref);second->m_Impacts.PushBack(impact);}
  CKBehavior* beh[2];CKParameterLocal* param[2];ParticleEmitter* emitters[]={first.get(),second.get()};
  for(int i=0;i<2;++i){
   beh[i]=static_cast<CKBehavior*>(ctx->CreateObject(CKCID_BEHAVIOR,"snapshot emitter"));
   beh[i]->UseFunction();beh[i]->SetPrototypeGuid(i==1&&kind==4?OBJECTSYSTEM_GUID:i==1&&kind==5?CURVESYSTEM_GUID:POINTSYSTEM_GUID);
   param[i]=beh[i]->CreateLocalParameter("Emitter",CKPGUID_VOIDBUF);
   if(!param[i]||param[i]->SetValue(&emitters[i],sizeof(emitters[i]))!=CK_OK)return 5;
  }
  first->m_Life=123;second->m_Life=456;manager->m_ShowInteractors=FALSE;
  TT_ParticleSnapshotHandle snap=nullptr;if(TT_ParticleCaptureSnapshot(ctx,&snap)!=CK_OK)return 6;
  first->m_Life=901;second->m_Life=902;manager->m_ShowInteractors=TRUE;
  Check(TT_ParticleRestoreSnapshot(ctx,snap)==CK_OK,"live restore",kind);
  Check(first->m_Life==123&&second->m_Life==456&&!manager->m_ShowInteractors,"live values",kind);
  first->m_Life=901;second->m_Life=902;manager->m_ShowInteractors=TRUE;
  // Pending deletion is also unsafe: the next world cleanup will destroy it.
  auto flags=ref->GetObjectFlags();ref->ModifyObjectFlags(CK_OBJECT_TOBEDELETED,0);
  Check(TT_ParticleValidateSnapshot(ctx,snap)!=CK_OK,"pending deletion preflight",kind);
  Check(TT_ParticleRestoreSnapshot(ctx,snap)!=CK_OK,"pending deletion restore",kind);
  Check(first->m_Life==901&&second->m_Life==902&&manager->m_ShowInteractors,"pending rejection atomic",kind);
  ref->ModifyObjectFlags(flags,~flags);
  first->m_Life=901;second->m_Life=902;manager->m_ShowInteractors=TRUE;
  auto id=ref->GetID();ctx->DestroyObject(id);
  Check(ctx->GetObject(id)==nullptr,"resource destroyed",kind);
  Check(TT_ParticleValidateSnapshot(ctx,snap)!=CK_OK,"destroyed preflight",kind);
  Check(TT_ParticleRestoreSnapshot(ctx,snap)!=CK_OK,"destroyed restore",kind);
  Check(first->m_Life==901&&second->m_Life==902&&manager->m_ShowInteractors,"destroyed rejection atomic",kind);
  TT_ParticleDestroySnapshot(snap);
  ParticleEmitter* nullEmitter=nullptr;for(auto* p:param)p->SetValue(&nullEmitter,sizeof(nullEmitter));
  first.reset();second.reset();CKCloseContext(ctx);
 }
 CKShutdown();std::printf("RESULT checks=%u differences=%u\n",checks,failures);return failures?1:0;
}
