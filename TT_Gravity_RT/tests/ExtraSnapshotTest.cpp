// Exercise the actual behavior implementation and CK object ownership. The
// allocator fault affects this translation unit only, after fixture setup.
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
static int failAfter = -1;
static void *watched[4]{};
static bool freed[4]{};
void *operator new(std::size_t n) {
    if (failAfter == 0) throw std::bad_alloc();
    if (failAfter > 0) --failAfter;
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
    for (int i=0;i<4;++i) if (watched[i] && p==watched[i]) freed[i]=true;
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
#include "../Behaviors/Extra.cpp"
#include "RCK3dEntity.h"
extern void SetProcessorSpecific_FunctionsPtr();
static SmallBall *List(CKBehavior *b) {
    SmallBall *p=nullptr; b->GetLocalParameterValue(SMALLBALL_LIST,&p);return p;
}
static void Check(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr,"FAIL %s\n",why);std::exit(1); }
}
int main(int argc,char **argv) {
    const bool allocation=argc>1 && std::strcmp(argv[1],"allocation")==0;
    const bool missing=argc>1 && std::strcmp(argv[1],"missing")==0;
    SetProcessorSpecific_FunctionsPtr();Check(CKStartUp()==CK_OK,"startup");
    CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);CKBuildClassHierarchyTable();
    CKContext *ctx=nullptr;Check(CKCreateContext(&ctx,nullptr,0,0)==CK_OK,"context");
    CKBehavior *behaviors[2]{};CK3dEntity *entities[4]{};
    for (int i=0;i<2;++i) {
        auto *b=CKBehavior::Cast(ctx->CreateObject(CKCID_BEHAVIOR,"extra test"));
        Check(b!=nullptr,"behavior");behaviors[i]=b;b->UseFunction();
        b->SetPrototypeGuid(CKGUID(0x36106bd9,0x51813906));
        b->CreateLocalParameter("Activationstatus",CKPGUID_BOOL);
        auto *slot=b->CreateLocalParameter("Smallballlist",CKPGUID_POINTER);
        Check(slot!=nullptr,"list parameter");
        SmallBall *head=nullptr;
        for (int j=1;j>=0;--j) {
            int k=i*2+j;entities[k]=CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY,"small ball"));
            Check(entities[k]!=nullptr,"entity");
            head=new SmallBall(head,entities[k],VxVector(float(k),float(k+1),float(k+2)),float(k+3),j==0);
        }
        Check(slot->SetValue(&head,sizeof(head))==CK_OK,"list install");
    }
    TT_ExtraSnapshotHandle saved=nullptr;Check(TT_ExtraCaptureSnapshot(ctx,&saved)==CK_OK,"capture");
    const bool preparedMode=argc>1 && std::strcmp(argv[1],"prepared")==0;
    const bool staleMode=argc>1 && std::strcmp(argv[1],"stale")==0;
    const bool emptyMode=argc>1 && std::strcmp(argv[1],"empty")==0;
    if (preparedMode || staleMode || emptyMode) {
        TT_ExtraDestroySnapshot(saved);saved=nullptr;
        for (int cycle=0;cycle<(preparedMode?3:1);++cycle) {
            if (emptyMode) for (auto *b:behaviors) {
                DeleteSmallBallList(List(b));SmallBall *p=nullptr;b->SetLocalParameterValue(SMALLBALL_LIST,&p,sizeof(p));
            }
            for (auto *b:behaviors) if (List(b)) List(b)->force=17.0f+float(cycle);
            Check(TT_ExtraCaptureSnapshot(ctx,&saved)==CK_OK,"overwrite capture");
            for (auto *b:behaviors) {
                if (!List(b)) {
                    auto *p=new SmallBall(nullptr,entities[0],VxVector(0,0,0),99.0f,true);
                    b->SetLocalParameterValue(SMALLBALL_LIST,&p,sizeof(p));
                } else List(b)->force=99.0f;
            }
            for (int i=0;i<2;++i) {watched[i*2]=List(behaviors[i]);watched[i*2+1]=List(behaviors[i])->next;}
            std::memset(freed,0,sizeof(freed));
            TT_ExtraRestoreHandle prepared=nullptr;
            Check(TT_ExtraPrepareRestore(ctx,saved,&prepared)==CK_OK,"prepare");
            TT_ExtraDestroySnapshot(saved);saved=nullptr;
            for (int i=0;i<2;++i) Check(List(behaviors[i])==watched[i*2] && List(behaviors[i])->force==99.0f,"prepare changed live state");
            if (staleMode) ctx->DestroyObject(entities[3]->GetID());
            failAfter=0;int result=TT_ExtraCommitRestore(ctx,prepared);failAfter=-1;
            if (staleMode) {
                Check(result!=CK_OK,"stale prepared owner accepted");
                for (int i=0;i<4;++i) Check(!freed[i],"stale commit freed live nodes");
                for (int i=0;i<2;++i) Check(List(behaviors[i])==watched[i*2],"stale commit published a list");
            } else {
                Check(result==CK_OK,"allocation-free commit");
                for (int i=0;i<2;++i) {
                    Check(freed[i*2],"old list not released after commit");
                    Check(emptyMode?List(behaviors[i])==nullptr:List(behaviors[i])->force==17.0f+float(cycle),"prepared saved state");
                }
                Check(TT_ExtraCommitRestore(ctx,prepared)!=CK_OK,"duplicate commit accepted");
            }
            TT_ExtraDestroyRestore(prepared);
            for (int i=0;i<4;++i) watched[i]=nullptr;
        }
        for (auto *b:behaviors) {DeleteSmallBallList(List(b));SmallBall *p=nullptr;b->SetLocalParameterValue(SMALLBALL_LIST,&p,sizeof(p));}
        CKCloseContext(ctx);CKShutdown();std::printf("PASS mode=%s\n",argv[1]);return 0;
    }
    if (missing) ctx->DestroyObject(entities[3]->GetID());
    int failedAllocations=0;
    bool succeeded=false;
    for (int attempt=0;attempt<(allocation?100:1);++attempt) {
        for (int i=0;i<2;++i) { watched[i*2]=List(behaviors[i]);watched[i*2+1]=List(behaviors[i])->next; }
        std::memset(freed,0,sizeof(freed));
        failAfter=allocation?attempt:-1;int status=CK_OK;bool threw=false;
        try {status=TT_ExtraRestoreSnapshot(ctx,saved);} catch (...) {threw=true;}
        failAfter=-1;
        Check(!threw,"restore must report failure rather than throw");
        if (status!=CK_OK) {
            ++failedAllocations;
            for (int i=0;i<4;++i) Check(!freed[i],"failed restore freed a live node");
            for (int i=0;i<2;++i) Check(List(behaviors[i])==watched[i*2],"failed restore published a new list");
        } else {
            Check(!missing,"missing saved entity was silently skipped");
            succeeded=true;
            for (int i=0;i<2;++i) {
                auto *p=List(behaviors[i]);
                for (int j=0;j<2;++j,p=p->next) {
                    int k=i*2+j;Check(p && p->ball==entities[k] && p->position.x==float(k) && p->force==float(k+3) && p->flag==(j==0),"node state/order");
                }
                Check(p==nullptr,"list length");
            }
            break;
        }
    }
    Check(!allocation || (failedAllocations>0 && succeeded),"allocation failure sweep never reached success");
    for (int i=0;i<4;++i) watched[i]=nullptr;
    TT_ExtraDestroySnapshot(saved);
    for (auto *b:behaviors) {DeleteSmallBallList(List(b));SmallBall *p=nullptr;b->SetLocalParameterValue(SMALLBALL_LIST,&p,sizeof(p));}
    CKCloseContext(ctx);CKShutdown();
    std::printf("PASS mode=%s allocation_failures=%d\n",argc>1?argv[1]:"normal",failedAllocations);return 0;
}
