#include "CKAll.h"
#include "ParticleSnapshot.h"
#include "ParticleManager.h"
#include <new>
#include <cstdio>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks=0,failures=0;
static void Check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
int main(){
 SetProcessorSpecific_FunctionsPtr();if(CKStartUp()!=CK_OK)return 2;
 CKContext* ctx=nullptr;if(CKCreateContext(&ctx,nullptr,0,0)!=CK_OK)return 3;
 auto* manager=new ParticleManager(ctx);
 for(int i=0;i<3;++i){
  TT_ParticleSnapshotHandle snap=nullptr;manager->m_ShowInteractors=FALSE;
  Check(TT_ParticleCaptureSnapshot(ctx,&snap)==CK_OK,"capture");
  manager->m_ShowInteractors=TRUE;
  Check(TT_ParticleRestoreSnapshot(ctx,snap)==CK_OK&&!manager->m_ShowInteractors,"display restored");
  // White-box retirement: keep context manager ownership coherent while
  // deliberately recycling identical storage to test the invalidation hook.
  ctx->ActivateManager(manager,FALSE);
  ctx->m_InactiveManagers.Remove(manager);
  Check(ParticleManager::GetManager(ctx)==nullptr,"manager inactive");
  auto* storage=manager;manager->~ParticleManager();
  manager=new(storage) ParticleManager(ctx);
  Check(manager==storage&&ParticleManager::GetManager(ctx)==manager,"same address registered");
  manager->m_ShowInteractors=TRUE;
  Check(TT_ParticleValidateSnapshot(ctx,snap)!=CK_OK,"old lifetime rejected");
  Check(TT_ParticleRestoreSnapshot(ctx,snap)!=CK_OK,"old restore rejected");
  Check(manager->m_ShowInteractors,"replacement unchanged");
  TT_ParticleDestroySnapshot(snap);
  TT_ParticleSnapshotHandle fresh=nullptr;
  Check(TT_ParticleCaptureSnapshot(ctx,&fresh)==CK_OK,"new lifetime captured");
  manager->m_ShowInteractors=FALSE;
  Check(TT_ParticleRestoreSnapshot(ctx,fresh)==CK_OK&&manager->m_ShowInteractors,"new lifetime restored");
  TT_ParticleDestroySnapshot(fresh);
 }
 CKCloseContext(ctx);CKShutdown();std::printf("RESULT checks=%u differences=%u\n",checks,failures);return failures?1:0;
}
