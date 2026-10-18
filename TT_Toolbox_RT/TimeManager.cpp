#include "TimeManager.h"
#include "CKTimeManager.h"

#include <cmath>

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
        // The score timer counts process/input samples, not simulation
        // substeps. Ballance advances two game ticks per sample, so deriving
        // this rate from the game-time delta would make the score run twice
        // as fast. The process rate is also separate from CK's advisory
        // behavioral-rate setting, which the stock scripts rewrite to 60.
        const double rawRate = static_cast<double>(time->GetFixedProcessRate());
        const double integralRate = std::round(rawRate);
        const double rate = std::fabs(rawRate - integralRate) < 0.001
            ? integralRate : rawRate;
        if (rate > 0.0)
            return static_cast<float>(static_cast<double>(m_FixedTicks) * 1000.0 / rate);
    }
    return static_cast<float>(m_Time);
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
