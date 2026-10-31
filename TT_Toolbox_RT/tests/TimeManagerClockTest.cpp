// Link against CK2/VxMath; include the manager implementation so this fixture
// exercises its clock and public snapshot ABI without loading game assets.
#include "../TimeManager.cpp"
#include "CKGlobals.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static int checks = 0;
static void Check(bool ok, const char *why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", why); std::exit(1); }
}
int main() {
    Check(CKStartUp() == CK_OK, "startup");
    CKContext *context = nullptr;
    Check(CKCreateContext(&context, nullptr, 0, 0) == CK_OK, "context");
    auto *clock = new TimeManager(context);
    auto *host = context->GetTimeManager();
    host->SetFixedDeltaTime(500.0f / 132.0f);
    // Practice rate is pacing only, even when supplied frame delta differs.
    for (float rate : {30.0f, 60.0f, 120.0f, 264.0f}) {
        clock->Reset(); host->SetFixedProcessRate(rate);
        for (int i = 0; i < 132; ++i) clock->Elapse(1000.0f / rate);
        Check(clock->Now() == 500.0f, "ordinary 132 samples per score interval");
    }
    const float before = clock->Now();
    Check(TT_TimeSetRecordedClock(context, TRUE) == CK_OK, "enter recorded clock");
    Check(clock->Now() == before, "enter preserves elapsed time");
    clock->Reset();
    // 132 Hz tape advances twice the ordinary logical time per sample.
    for (int i = 0; i < 66; ++i) clock->Elapse(1000.0f / 132.0f);
    Check(clock->Now() > 499.99f && clock->Now() < 500.01f, "recorded 132 Hz time");
    std::uint64_t ticks = 0; double time = 0; CKBOOL timing = FALSE, recorded = FALSE;
    Check(TT_TimeCaptureRuntimeStateV2(context, &ticks, &time, &timing, &recorded) == CK_OK,
          "capture recorded mode");
    Check(recorded && timing && ticks == 0, "recorded metadata");
    float branch[100]{};
    for (int i=0;i<100;++i) { clock->Elapse(i % 2 ? 1000.0f/240.0f : 1000.0f/132.0f); branch[i]=clock->Now(); }
    for (int attempt=0;attempt<3;++attempt) {
        Check(TT_TimeSetRecordedClock(context, FALSE)==CK_OK,"switch to ordinary");
        for (int i=0;i<20;++i) clock->Elapse(1.0f);
        Check(TT_TimeRestoreRuntimeStateV2(context,ticks,time,timing,recorded)==CK_OK,"restore clock mode and value");
        Check(clock->UsesRecordedClock(),"restored mode");
        for (int i=0;i<100;++i) {
            clock->Elapse(i % 2 ? 1000.0f/240.0f : 1000.0f/132.0f);
            Check(clock->Now()==branch[i],"variable-delta repeated restore");
        }
    }
    clock->Pause(); const float paused = clock->Now();
    for(int i=0;i<100;++i) clock->Elapse(1000.0f/132.0f);
    Check(clock->Now()==paused,"pause recorded clock");
    Check(TT_TimeSetRecordedClock(context,FALSE)==CK_OK && clock->Now()==paused,"exit preserves elapsed time");
    clock->Play();clock->Reset();
    for(int i=0;i<132;++i) clock->Elapse(1000.0f/132.0f);
    Check(clock->Now()==500.0f,"ordinary rule restored");
    Check(TT_TimeCaptureRuntimeStateV2(nullptr,&ticks,&time,&timing,&recorded)==CKERR_INVALIDPARAMETER,"null context");
    CKCloseContext(context);CKShutdown();
    std::printf("PASS %d clock checks\n",checks);
}
