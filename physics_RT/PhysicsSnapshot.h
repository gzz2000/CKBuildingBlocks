#ifndef PHYSICS_RT_PHYSICSSNAPSHOT_H
#define PHYSICS_RT_PHYSICSSNAPSHOT_H

class CKContext;

#if defined(_WIN32)
#define TT_PHYSICS_SNAPSHOT_EXPORT extern "C" __declspec(dllexport)
#else
#define TT_PHYSICS_SNAPSHOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Opaque, process-local snapshots for practice tools. A snapshot is valid only
// while the level which created it remains loaded.
typedef void *TT_PhysicsSnapshotHandle;

TT_PHYSICS_SNAPSHOT_EXPORT int TT_PhysicsCaptureSnapshot(
    CKContext *context, TT_PhysicsSnapshotHandle *snapshot);
TT_PHYSICS_SNAPSHOT_EXPORT int TT_PhysicsRestoreSnapshot(
    CKContext *context, TT_PhysicsSnapshotHandle snapshot);
TT_PHYSICS_SNAPSHOT_EXPORT void TT_PhysicsDestroySnapshot(
    TT_PhysicsSnapshotHandle snapshot);

#endif // PHYSICS_RT_PHYSICSSNAPSHOT_H
