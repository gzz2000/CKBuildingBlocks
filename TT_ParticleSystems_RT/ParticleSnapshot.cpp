#include "ParticleSnapshot.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <array>

#include "CKAll.h"
#include "ParticleEmitter.h"
#include "ParticleGuids.h"
#include "TimePointEmitter.h"
#include "WaveEmitter.h"
#include "ObjectEmitter.h"
#include "CurveEmitter.h"
#include "ParticleManager.h"

namespace {
// Explicit non-owning input/settings caches. Keep this list auditable against
// ParticleEmitter.h; allocation, linked lists, histories and owners are handled
// separately. ReadInputs is conditional and is not a substitute for restoring
// the cache the renderer or the next emission will actually consume.
#define PARTICLE_CACHE_FIELDS(F) \
    F(m_Entity) F(m_Texture) F(m_Group) F(m_Mesh) F(m_MessageType) \
    F(m_RenderParticlesCallback) F(m_SrcBlend) F(m_DestBlend) \
    F(m_YawVariation) F(m_PitchVariation) F(m_Speed) F(m_SpeedVariation) \
    F(m_AngularSpeed) F(m_AngularSpeedVariation) \
    F(m_StartSize) F(m_StartSizeVar) F(m_EndSize) F(m_EndSizeVar) \
    F(m_Weight) F(m_WeightVariation) F(m_Surface) F(m_SurfaceVariation) \
    F(m_Bounce) F(m_BounceVariation) F(m_Life) F(m_LifeVariation) \
    F(m_StartColor) F(m_StartColorVar) F(m_EndColor) F(m_EndColorVar) \
    F(m_InitialTextureFrame) F(m_InitialTextureFrameVariance) \
    F(m_SpeedTextureFrame) F(m_SpeedTextureFrameVariance) \
    F(m_TextureFrameCount) F(m_TextureFrameloop) \
    F(m_EvolutionsFlags) F(m_VariancesFlags) F(m_InteractorsFlags) \
    F(m_DeflectorsFlags) F(m_RenderMode)

struct EmitterCache {
#define DECLARE_CACHE(name) decltype(ParticleEmitter::name) name{};
    PARTICLE_CACHE_FIELDS(DECLARE_CACHE)
#undef DECLARE_CACHE
};
struct ObjectIdentity {
    CK_ID id = 0;
    CKObject *pointer = nullptr;
};
// Some payload fields are initialized only by particular emitter/render modes.
// Preserve their representation without evaluating indeterminate floats/ints.
// This is an in-process checkpoint, not a portable Particle serialization ABI.
static_assert(std::is_trivially_copyable<Particle>::value, "Particle payload must support byte preservation");
static_assert(std::is_standard_layout<Particle>::value, "Particle link offsets must be well-defined");
constexpr std::size_t kLinksBegin = offsetof(Particle, prev);
constexpr std::size_t kLinksEnd = offsetof(Particle, next) + sizeof(Particle *);
static_assert(offsetof(Particle, next) == kLinksBegin + sizeof(Particle *), "Particle links must remain adjacent");
struct ParticleSlotState {
    std::array<unsigned char, sizeof(Particle)> payload{};
    int previous = -1;
    int next = -1;
};

struct ImpactState {
    ParticleImpact value;
    CK_ID objectId = 0;
    CK3dEntity *identity = NULL;
};

struct EmitterState {
    CK_ID behaviorId = 0;
    CKBehavior *behaviorIdentity = NULL;
    ParticleEmitter *emitterIdentity = NULL;
    int maximumParticles = 0;
    int activeHead = -1;
    int freeHead = -1;
    int totalParticles = 0;
    int particleCount = 0;
    int emitsPerFrame = 0;
    int emitsVariance = 0;
    int currentImpact = 0;
    float deltaTime = 0.0f;
    CKBOOL active = FALSE;
    CKBOOL firstEmission = FALSE;
    CKBOOL timePoint = FALSE;
    CKBOOL wave = FALSE;
    VxVector previousPosition = VxVector::axis0();
    float emissionTime = 0.0f;
    float previousAngle = 0.0f;
    EmitterCache cache;
    std::array<ObjectIdentity, 4> resources;
    CKGUID kind;
    ObjectIdentity shapeOrCurve;
    float emissionDelay = 0.0f;
    std::vector<ParticleSlotState> slots;
    std::vector<ImpactState> impacts;
};

struct ParticleSnapshot {
    CKContext *context = NULL;
    bool valid = true;
    ParticleManager *manager = nullptr;
    CKBOOL showInteractors = FALSE;
    std::vector<EmitterState> emitters;
};
std::unordered_set<ParticleSnapshot *> snapshots;

bool IsParticleSystem(const CKGUID &guid)
{
    return guid == POINTSYSTEM_GUID || guid == PLANARSYSTEM_GUID ||
           guid == CUBICSYSTEM_GUID || guid == LINEARSYSTEM_GUID ||
           guid == DISCSYSTEM_GUID || guid == OBJECTSYSTEM_GUID ||
           guid == CURVESYSTEM_GUID || guid == CYLINDRICALSYSTEM_GUID ||
           guid == SPHERICALSYSTEM_GUID || guid == TIMEPOINTSYSTEM_GUID ||
           guid == WAVESYSTEM_GUID;
}

Particle *PoolBase(const ParticleEmitter *emitter)
{
    return emitter && emitter->m_BackPool
        ? reinterpret_cast<Particle *>((reinterpret_cast<std::uintptr_t>(
              emitter->m_BackPool + 15)) & ~static_cast<std::uintptr_t>(15))
        : NULL;
}

int PoolIndex(const ParticleEmitter *emitter, const Particle *particle)
{
    Particle *base = PoolBase(emitter);
    if (!emitter || !particle || !base || emitter->m_MaximumParticles <= 0)
        return -1;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(particle);
    const std::uintptr_t bytes = static_cast<std::uintptr_t>(
        emitter->m_MaximumParticles) * sizeof(Particle);
    if (address < begin || address >= begin + bytes ||
        (address - begin) % sizeof(Particle) != 0)
        return -1;
    return static_cast<int>((address - begin) / sizeof(Particle));
}

// Active nodes form a doubly linked list; the free list is singly linked.
// In particular, free-slot prev is uninitialized on allocation and stale after
// death. Never evaluate it. Reject corrupt topology instead of converting an
// out-of-pool pointer into a valid-looking null link in the checkpoint.
bool CapturePool(const ParticleEmitter *emitter, EmitterState &state)
{
    Particle *base = PoolBase(emitter);
    std::vector<unsigned char> seen(state.maximumParticles, 0);
    state.slots.resize(state.maximumParticles);
    int visited = 0, activeCount = 0;
    const auto walk = [&](Particle *node, bool active) {
        Particle *previous = nullptr;
        int previousIndex = -1;
        while (node) {
            const int index = PoolIndex(emitter, node);
            if (index < 0 || seen[index]) return false;
            seen[index] = 1;
            ++visited;
            if (active && node->prev != previous) return false;
            Particle *next = node->next;
            const int nextIndex = PoolIndex(emitter, next);
            if (next && nextIndex < 0) return false;
            auto &slot = state.slots[index];
            slot.previous = active ? previousIndex : -1;
            slot.next = nextIndex;
            if (active) ++activeCount;
            previous = node;
            previousIndex = index;
            node = next;
        }
        return true;
    };
    if (!walk(emitter->particles, true) || !walk(emitter->m_Pool, false) ||
        activeCount != emitter->particleCount || visited != state.maximumParticles)
        return false;
    for (int i = 0; i < state.maximumParticles; ++i) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(&base[i]);
        auto &payload = state.slots[i].payload;
        std::memcpy(payload.data(), bytes, kLinksBegin);
        std::memcpy(payload.data() + kLinksEnd, bytes + kLinksEnd, sizeof(Particle) - kLinksEnd);
    }
    return true;
}

ParticleEmitter *GetEmitter(CKBehavior *behavior)
{
    if (!behavior || behavior->GetLocalParameterCount() < 1)
        return NULL;
    ParticleEmitter *emitter = NULL;
    behavior->GetLocalParameterValue(0, &emitter);
    return emitter;
}
}

int TT_ParticleCaptureSnapshot(CKContext *context, TT_ParticleSnapshotHandle *snapshot)
{
    if (!context || !snapshot)
        return CKERR_INVALIDPARAMETER;
    *snapshot = NULL;
    try
    {
        std::unique_ptr<ParticleSnapshot> result(new ParticleSnapshot());
        result->context = context;
        result->manager = ParticleManager::GetManager(context);
        if (result->manager) result->showInteractors = result->manager->m_ShowInteractors;
        std::unordered_map<CKObject *, CK_ID> live;
        for (auto *object : context->GetObjectListByType(CKCID_OBJECT, TRUE))
            if (object) live.emplace(object, object->GetID());
        const XObjectPointerArray &behaviors =
            context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
        for (XObjectPointerArray::ConstIterator it = behaviors.Begin();
             it != behaviors.End(); ++it)
        {
            CKBehavior *behavior = static_cast<CKBehavior *>(*it);
            if (!behavior || !IsParticleSystem(behavior->GetPrototypeGuid()))
                continue;
            ParticleEmitter *emitter = GetEmitter(behavior);
            Particle *base = PoolBase(emitter);
            if (!emitter || !base || emitter->m_MaximumParticles <= 0)
                continue;

            EmitterState state;
            state.behaviorId = behavior->GetID();
            state.behaviorIdentity = behavior;
            state.emitterIdentity = emitter;
            state.kind = behavior->GetPrototypeGuid();
#define CAPTURE_CACHE(name) state.cache.name = emitter->name;
            PARTICLE_CACHE_FIELDS(CAPTURE_CACHE)
#undef CAPTURE_CACHE
            const CK_ID resourceIds[] = {emitter->m_Entity, emitter->m_Texture,
                                        emitter->m_Group, emitter->m_Mesh};
            for (std::size_t i = 0; i < state.resources.size(); ++i)
                state.resources[i] = {resourceIds[i], context->GetObject(resourceIds[i])};
            CKObject *shapeOrCurve = nullptr;
            if (state.kind == OBJECTSYSTEM_GUID)
                shapeOrCurve = reinterpret_cast<CKObject *>(static_cast<ObjectEmitter *>(emitter)->m_Shape);
            else if (state.kind == CURVESYSTEM_GUID)
                shapeOrCurve = reinterpret_cast<CKObject *>(static_cast<CurveEmitter *>(emitter)->m_Curve);
            if (shapeOrCurve) {
                const auto found = live.find(shapeOrCurve);
                if (found == live.end()) return CKERR_INVALIDOBJECT;
                state.shapeOrCurve = {found->second, shapeOrCurve};
            }
            state.maximumParticles = emitter->m_MaximumParticles;
            state.activeHead = PoolIndex(emitter, emitter->particles);
            state.freeHead = PoolIndex(emitter, emitter->m_Pool);
            state.totalParticles = emitter->totalParticles;
            state.particleCount = emitter->particleCount;
            state.emitsPerFrame = emitter->emitsPerFrame;
            state.emitsVariance = emitter->emitsVar;
            state.currentImpact = emitter->m_CurrentImpact;
            state.deltaTime = emitter->m_DeltaTime;
            state.active = emitter->m_Active;
            // These are histories, not input parameters: rereading the behavior
            // cannot reconstruct the emission phase or the previous motion.
            state.firstEmission = emitter->m_Flag0x10C;
            state.timePoint = emitter->m_IsTimePointEmitter;
            state.wave = emitter->m_IsWaveEmitter;
            if (state.timePoint)
            {
                const auto *timed = static_cast<const TimePointEmitter *>(emitter);
                state.previousPosition = timed->m_Position;
                state.emissionTime = timed->m_Time;
                state.emissionDelay = timed->m_EmissionDelay;
            }
            else if (state.wave)
            {
                const auto *wave = static_cast<const WaveEmitter *>(emitter);
                state.previousPosition = wave->m_Position;
                state.previousAngle = wave->m_Angle;
            }
            if (!CapturePool(emitter, state))
                return CKERR_INVALIDOBJECT;
            state.impacts.reserve(emitter->m_Impacts.Size());
            for (int i = 0; i < emitter->m_Impacts.Size(); ++i)
            {
                ImpactState impact;
                impact.value = emitter->m_Impacts[i];
                impact.identity = impact.value.m_Object;
                if (impact.identity) {
                    const auto found = live.find(impact.identity);
                    if (found == live.end()) return CKERR_INVALIDOBJECT;
                    impact.objectId = found->second;
                }
                impact.value.m_Object = NULL;
                state.impacts.push_back(impact);
            }
            result->emitters.push_back(state);
        }
        snapshots.insert(result.get());
        *snapshot = result.release();
        return CK_OK;
    }
    catch (...)
    {
        return CKERR_OUTOFMEMORY;
    }
}

void TT_ParticleInvalidateEmitter(ParticleEmitter *emitter)
{
    for (auto *saved : snapshots)
        for (const auto &state : saved->emitters)
            if (state.emitterIdentity == emitter) saved->valid = false;
}

void TT_ParticleInvalidateManager(ParticleManager *manager)
{
    for (auto *saved : snapshots)
        if (saved->manager == manager) saved->valid = false;
}

int TT_ParticleValidateSnapshot(CKContext *context, TT_ParticleSnapshotHandle snapshot)
{
    ParticleSnapshot *saved = static_cast<ParticleSnapshot *>(snapshot);
    if (!context || !saved || !snapshots.count(saved))
        return CKERR_INVALIDPARAMETER;
    if (!saved->valid || saved->context != context)
        return CKERR_INVALIDOBJECT;
    if (ParticleManager::GetManager(context) != saved->manager)
        return CKERR_INVALIDOBJECT;
    for (const EmitterState &state : saved->emitters)
    {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(state.behaviorId));
        if (!behavior || behavior != state.behaviorIdentity || behavior->IsToBeDeleted())
            return CKERR_INVALIDOBJECT;
        ParticleEmitter *emitter = GetEmitter(behavior);
        if (!emitter || emitter != state.emitterIdentity || !PoolBase(emitter) ||
            emitter->m_MaximumParticles != state.maximumParticles ||
            emitter->m_Context != context || behavior->GetPrototypeGuid() != state.kind ||
            emitter->m_IsTimePointEmitter != state.timePoint || emitter->m_IsWaveEmitter != state.wave ||
            static_cast<int>(state.slots.size()) != state.maximumParticles)
            return CKERR_INVALIDOBJECT;
        for (const auto &resource : state.resources)
            if (context->GetObject(resource.id) != resource.pointer ||
                (resource.pointer && resource.pointer->IsToBeDeleted()))
                return CKERR_INVALIDOBJECT;
        if (context->GetObject(state.shapeOrCurve.id) != state.shapeOrCurve.pointer ||
            (state.shapeOrCurve.pointer && state.shapeOrCurve.pointer->IsToBeDeleted()))
            return CKERR_INVALIDOBJECT;
        for (const auto &impact : state.impacts)
            if (context->GetObject(impact.objectId) != impact.identity ||
                (impact.identity && impact.identity->IsToBeDeleted()))
                return CKERR_INVALIDOBJECT;
    }
    return CK_OK;
}

int TT_ParticleRestoreSnapshot(CKContext *context, TT_ParticleSnapshotHandle snapshot)
{
    const int validation = TT_ParticleValidateSnapshot(context, snapshot);
    if (validation != CK_OK) return validation;
    auto *saved = static_cast<ParticleSnapshot *>(snapshot);
    for (const EmitterState &state : saved->emitters)
    {
        auto *emitter = state.emitterIdentity;
        Particle *base = PoolBase(emitter);
#define RESTORE_CACHE(name) emitter->name = state.cache.name;
        PARTICLE_CACHE_FIELDS(RESTORE_CACHE)
#undef RESTORE_CACHE
        if (state.kind == OBJECTSYSTEM_GUID)
            static_cast<ObjectEmitter *>(emitter)->m_Shape = reinterpret_cast<CKMesh *>(state.shapeOrCurve.pointer);
        else if (state.kind == CURVESYSTEM_GUID)
            static_cast<CurveEmitter *>(emitter)->m_Curve = reinterpret_cast<CKCurve *>(state.shapeOrCurve.pointer);

        for (int i = 0; i < state.maximumParticles; ++i)
        {
            auto *bytes = reinterpret_cast<unsigned char *>(&base[i]);
            const auto &payload = state.slots[i].payload;
            std::memcpy(bytes, payload.data(), kLinksBegin);
            std::memcpy(bytes + kLinksEnd, payload.data() + kLinksEnd, sizeof(Particle) - kLinksEnd);
            const int previous = state.slots[i].previous;
            const int next = state.slots[i].next;
            base[i].prev = previous >= 0 ? &base[previous] : NULL;
            base[i].next = next >= 0 ? &base[next] : NULL;
        }
        emitter->particles = state.activeHead >= 0
            ? &base[state.activeHead] : NULL;
        emitter->m_Pool = state.freeHead >= 0
            ? &base[state.freeHead] : NULL;
        emitter->totalParticles = state.totalParticles;
        emitter->particleCount = state.particleCount;
        emitter->emitsPerFrame = state.emitsPerFrame;
        emitter->emitsVar = state.emitsVariance;
        emitter->m_DeltaTime = state.deltaTime;
        emitter->m_Active = state.active;
        emitter->m_Flag0x10C = state.firstEmission;
        if (state.timePoint)
        {
            auto *timed = static_cast<TimePointEmitter *>(emitter);
            timed->m_Position = state.previousPosition;
            timed->m_Time = state.emissionTime;
            timed->m_EmissionDelay = state.emissionDelay;
        }
        else if (state.wave)
        {
            auto *wave = static_cast<WaveEmitter *>(emitter);
            wave->m_Position = state.previousPosition;
            wave->m_Angle = state.previousAngle;
        }
        emitter->m_Impacts.Clear();
        for (const ImpactState &savedImpact : state.impacts)
        {
            ParticleImpact impact = savedImpact.value;
            CKObject *object = savedImpact.objectId
                ? context->GetObject(savedImpact.objectId) : NULL;
            impact.m_Object = object == savedImpact.identity
                ? CK3dEntity::Cast(object) : NULL;
            emitter->m_Impacts.PushBack(impact);
        }
        emitter->m_CurrentImpact = state.currentImpact;
    }
    // World transforms/meshes and local parameter values are restored by the
    // enclosing checkpoint. Calling ShowInteractors here would mutate them.
    if (saved->manager) saved->manager->m_ShowInteractors = saved->showInteractors;
    return CK_OK;
}

#undef PARTICLE_CACHE_FIELDS

void TT_ParticleDestroySnapshot(TT_ParticleSnapshotHandle snapshot)
{
    auto *saved = static_cast<ParticleSnapshot *>(snapshot);
    if (snapshots.erase(saved)) delete saved;
}
