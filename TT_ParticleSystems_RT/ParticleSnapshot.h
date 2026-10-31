#ifndef TT_PARTICLESYSTEMS_RT_PARTICLESNAPSHOT_H
#define TT_PARTICLESYSTEMS_RT_PARTICLESNAPSHOT_H

class CKContext;

#if defined(_WIN32)
#define TT_PARTICLE_SNAPSHOT_EXPORT extern "C" __declspec(dllexport)
#else
#define TT_PARTICLE_SNAPSHOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

typedef void *TT_ParticleSnapshotHandle;

TT_PARTICLE_SNAPSHOT_EXPORT int TT_ParticleCaptureSnapshot(
    CKContext *context, TT_ParticleSnapshotHandle *snapshot);
TT_PARTICLE_SNAPSHOT_EXPORT int TT_ParticleRestoreSnapshot(
    CKContext *context, TT_ParticleSnapshotHandle snapshot);
TT_PARTICLE_SNAPSHOT_EXPORT int TT_ParticleValidateSnapshot(
    CKContext *context, TT_ParticleSnapshotHandle snapshot);
TT_PARTICLE_SNAPSHOT_EXPORT void TT_ParticleDestroySnapshot(
    TT_ParticleSnapshotHandle snapshot);

// Internal owner-lifetime notification; no callbacks are invoked here.
class ParticleEmitter;
void TT_ParticleInvalidateEmitter(ParticleEmitter *emitter);
class ParticleManager;
void TT_ParticleInvalidateManager(ParticleManager *manager);

#endif
