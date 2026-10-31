#include <cstring>
#include "TimeManager.h"
#include "CKTimeManager.h"

TimeManager::TimeManager(CKContext *context) : CKBaseManager(context, TT_TIME_MANAGER_GUID, "TT Time Manager")
{
    context->RegisterNewManager(this);
    m_Timing = true;
    m_Time = 0.0;
    m_FixedTicks = 0;
}

TimeManager::~TimeManager()
{
}

float TimeManager::Now() const
{
    CKTimeManager *time = m_Context ? m_Context->GetTimeManager() : nullptr;
    if (!m_RecordedClock && time && time->GetFixedDeltaTime() > 0.0f) {
        // FixedProcessRate is a wall-clock pacing control. Changing it must
        // not reinterpret ticks already elapsed: one ordinary score interval
        // is always exactly 132 input/process samples. FixedDeltaTime is the
        // logical duration of one such sample (1000/264 ms in Player).
        return static_cast<float>(
            m_Time + static_cast<double>(m_FixedTicks) * time->GetFixedDeltaTime());
    }
    return static_cast<float>(m_Time);
}

void TimeManager::SetRecordedClock(bool enabled)
{
    if (m_RecordedClock == enabled)
        return;
    // Keep the current elapsed time when a live TAS session is entered/left.
    // Subsequent ticks use the selected clock; practice-rate changes never
    // change either logical clock's accumulated past.
    m_Time = Now();
    m_FixedTicks = 0;
    m_RecordedClock = enabled;
}

extern "C" DLL_EXPORT int TT_TimeSetRecordedClock(CKContext *context, CKBOOL enabled)
{
    if (!context) return CKERR_INVALIDPARAMETER;
    auto *manager = TimeManager::GetManager(context);
    if (!manager) return CKERR_NOTINITIALIZED;
    manager->SetRecordedClock(enabled != FALSE);
    return CK_OK;
}

extern "C" DLL_EXPORT int TT_TimeCaptureRuntimeStateV2(
    CKContext *context, std::uint64_t *ticks, double *time, CKBOOL *timing, CKBOOL *recorded)
{
    if (!context || !ticks || !time || !timing || !recorded) return CKERR_INVALIDPARAMETER;
    auto *manager = TimeManager::GetManager(context);
    if (!manager) return CKERR_NOTINITIALIZED;
    bool active = false;
    manager->GetRuntimeState(*ticks, *time, active);
    *timing = active ? TRUE : FALSE;
    *recorded = manager->UsesRecordedClock() ? TRUE : FALSE;
    return CK_OK;
}

extern "C" DLL_EXPORT int TT_TimeRestoreRuntimeStateV2(
    CKContext *context, std::uint64_t ticks, double time, CKBOOL timing, CKBOOL recorded)
{
    if (!context) return CKERR_INVALIDPARAMETER;
    auto *manager = TimeManager::GetManager(context);
    if (!manager) return CKERR_NOTINITIALIZED;
    manager->RestoreRuntimeStateV2(ticks, time, timing != FALSE, recorded != FALSE);
    return CK_OK;
}

extern "C" DLL_EXPORT int TT_TimeCaptureRuntimeState(
    CKContext *context, std::uint64_t *fixedTicks, double *time, CKBOOL *timing)
{
    if (!context || !fixedTicks || !time || !timing)
        return CKERR_INVALIDPARAMETER;
    TimeManager *manager = TimeManager::GetManager(context);
    if (!manager)
        return CKERR_NOTINITIALIZED;
    bool active = false;
    manager->GetRuntimeState(*fixedTicks, *time, active);
    *timing = active ? TRUE : FALSE;
    return CK_OK;
}

extern "C" DLL_EXPORT int TT_TimeRestoreRuntimeState(
    CKContext *context, std::uint64_t fixedTicks, double time, CKBOOL timing)
{
    if (!context)
        return CKERR_INVALIDPARAMETER;
    TimeManager *manager = TimeManager::GetManager(context);
    if (!manager)
        return CKERR_NOTINITIALIZED;
    manager->RestoreRuntimeState(fixedTicks, time, timing != FALSE);
    return CK_OK;
}

void TimeManager::Elapse(float delta)
{
    if (!m_Timing)
        return;

    CKTimeManager *time = m_Context ? m_Context->GetTimeManager() : nullptr;
    if (m_RecordedClock) {
        // Retail TT_TimeManager stores the sum as binary32 after every BB
        // execution. A double accumulator or host fixed tick is a different
        // clock, especially for 132/240 Hz and variable-delta recordings.
        m_Time = static_cast<float>(static_cast<float>(m_Time) + delta);
    } else if (time && time->GetFixedDeltaTime() > 0.0f) {
        ++m_FixedTicks;
    } else {
        m_Time += static_cast<double>(delta);
    }
}

// Semantic APIs remain available to hosts without a dynamic linker.
void *TT_TimeResolveStaticFunction(const char *name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "TT_TimeSetRecordedClock") == 0) return reinterpret_cast<void *>(&TT_TimeSetRecordedClock);
    if (std::strcmp(name, "TT_TimeCaptureRuntimeStateV2") == 0) return reinterpret_cast<void *>(&TT_TimeCaptureRuntimeStateV2);
    if (std::strcmp(name, "TT_TimeRestoreRuntimeStateV2") == 0) return reinterpret_cast<void *>(&TT_TimeRestoreRuntimeStateV2);
    if (std::strcmp(name, "TT_TimeCaptureRuntimeState") == 0) return reinterpret_cast<void *>(&TT_TimeCaptureRuntimeState);
    if (std::strcmp(name, "TT_TimeRestoreRuntimeState") == 0) return reinterpret_cast<void *>(&TT_TimeRestoreRuntimeState);
    return nullptr;
}
