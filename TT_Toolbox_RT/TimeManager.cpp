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
    if (time && time->GetFixedDeltaTime() > 0.0f) {
        // FixedProcessRate is a wall-clock pacing control. Changing it must
        // not reinterpret ticks already elapsed: one Ballance score interval
        // is always exactly 132 input/process samples. FixedDeltaTime is the
        // logical duration of one such sample (1000/264 ms in Player).
        return static_cast<float>(
            static_cast<double>(m_FixedTicks) * time->GetFixedDeltaTime());
    }
    return static_cast<float>(m_Time);
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
    if (time && time->GetFixedDeltaTime() > 0.0f) {
        ++m_FixedTicks;
    } else {
        m_Time += static_cast<double>(delta);
    }
}
