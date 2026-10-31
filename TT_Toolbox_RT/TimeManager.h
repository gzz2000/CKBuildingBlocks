#ifndef TOOLBOX_TIMEMANAGER_H
#define TOOLBOX_TIMEMANAGER_H

#include "CKBaseManager.h"
#include "CKContext.h"

#include <cstdint>

#define TT_TIME_MANAGER_GUID CKGUID(0x60632E28, 0x7D3B3C7D)

class TimeManager : public CKBaseManager
{
public:
    TimeManager(CKContext *ctx);
    ~TimeManager();

    CKERROR OnCKInit() { return CK_OK; }
    CKERROR OnCKEnd() { return CK_OK; }
    CKERROR OnCKPlay() { return CK_OK; }
    CKERROR OnCKReset() { return CK_OK; }
    CKERROR PreClearAll() { return CK_OK; }
    CKERROR PostClearAll() { return CK_OK; }
    CKERROR PostProcess() { return CK_OK; }
    CKDWORD GetValidFunctionsMask()
    {
        return CKMANAGER_FUNC_PostProcess |
               CKMANAGER_FUNC_PreClearAll |
               CKMANAGER_FUNC_PostClearAll |
               CKMANAGER_FUNC_OnCKInit |
               CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnCKPlay |
               CKMANAGER_FUNC_OnCKReset;
    }

    void Reset() { m_Timing = true; m_Time = 0.0; m_FixedTicks = 0; }

    void Pause() { m_Timing = false; }

    void Play() { m_Timing = true; }

    float Now() const;

    void Elapse(float delta);
    bool UsesRecordedClock() const { return m_RecordedClock; }
    void SetRecordedClock(bool enabled);

    void GetRuntimeState(std::uint64_t &fixedTicks, double &time, bool &timing) const
    {
        fixedTicks = m_FixedTicks;
        time = m_Time;
        timing = m_Timing;
    }

    void RestoreRuntimeState(std::uint64_t fixedTicks, double time, bool timing)
    {
        m_FixedTicks = fixedTicks;
        m_Time = time;
        m_Timing = timing;
    }

    void RestoreRuntimeStateV2(std::uint64_t fixedTicks, double time, bool timing, bool recorded)
    {
        RestoreRuntimeState(fixedTicks, time, timing);
        m_RecordedClock = recorded;
    }

    static TimeManager *GetManager(CKContext *context)
    {
        return (TimeManager *)context->GetManagerByGuid(TT_TIME_MANAGER_GUID);
    }

protected:
    bool m_Timing;
    double m_Time;
    std::uint64_t m_FixedTicks;
    bool m_RecordedClock = false;
};

#endif // TOOLBOX_TIMEMANAGER_H
