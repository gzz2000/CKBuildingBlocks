#pragma once

#include "TimePointEmitter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// Default-off observation of existing values. Never advances RNG, updates a
// transform/bounding cache, allocates a particle, or changes behavior activity.
class ParticleStepTrace {
public:
    ParticleStepTrace(const CKBehaviorContext &context, ParticleEmitter *emitter)
        : context(context), emitter(emitter) {
        static const char *selected = std::getenv("BALLANCE_TRACE_PARTICLE_ENTITY");
        if (!selected || !*selected || !emitter) return;
        auto *entity = context.Context->GetObject(emitter->m_Entity);
        active = entity && entity->GetName() && !std::strcmp(selected, entity->GetName());
        if (active) Write("before");
    }
    ~ParticleStepTrace() { if (active) Write("after"); }

private:
    void Write(const char *phase) const {
        auto *behavior = context.Behavior;
        int activity = 0;
        float emissionTime = 0;
        behavior->GetLocalParameterValue(1, &activity);
        behavior->GetLocalParameterValue(2, &emissionTime);
        std::uint64_t digest = 14695981039346656037ull;
        const auto bytes = [&](const void *data, std::size_t n) {
            auto *p = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < n; ++i) { digest ^= p[i]; digest *= 1099511628211ull; }
        };
        int visited = 0;
        // Only always-initialized active payload fields, no padding, pointer
        // addresses or free-slot / optional texture data.
        for (auto *p = emitter->particles; p && visited <= emitter->m_MaximumParticles; p = p->next) {
            bytes(&p->pos, sizeof(p->pos)); bytes(&p->dir, sizeof(p->dir));
            bytes(&p->m_Life, sizeof(p->m_Life)); bytes(&p->m_Size, sizeof(p->m_Size));
            ++visited;
        }
        unsigned inputs = 0;
        for (int i = 0; i < behavior->GetInputCount() && i < 32; ++i)
            if (behavior->IsInputActive(i)) inputs |= 1u << i;
        const auto *timed = emitter->m_IsTimePointEmitter
            ? static_cast<const TimePointEmitter *>(emitter) : nullptr;
        std::fprintf(stderr,
            "PARTICLE_STEP phase=%s tick=%u behavior=%u rng=%u dt=%a activity=%d inputs=%u "
            "count=%d visited=%d payload=%016llx emission=%a lastdt=%a active=%d first=%d "
            "timed=%d history_time=%a delay=%a px=%a py=%a pz=%a\n",
            phase, context.Context->GetTimeManager()->GetMainTickCount(), behavior->GetID(),
            CKGetRandomState(), double(context.DeltaTime), activity, inputs,
            emitter->particleCount, visited, static_cast<unsigned long long>(digest),
            double(emissionTime), double(emitter->m_DeltaTime), int(emitter->m_Active), int(emitter->m_Flag0x10C),
            timed != nullptr, timed ? double(timed->m_Time) : 0.0, timed ? double(timed->m_EmissionDelay) : 0.0,
            timed ? double(timed->m_Position.x) : 0.0, timed ? double(timed->m_Position.y) : 0.0,
            timed ? double(timed->m_Position.z) : 0.0);
    }
    const CKBehaviorContext &context;
    ParticleEmitter *emitter;
    bool active = false;
};
