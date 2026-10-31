#ifndef PHYSICS_RT_PHYSICSSNAPSHOT_H
#define PHYSICS_RT_PHYSICSSNAPSHOT_H

class CKContext;
class CK3dEntity;

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

// Regression-test hook used only by the opt-in snapshot audit harness. It
// moves the existing IVP object without dephysicalizing it, so force/contact
// ownership remains identical to a naturally moving ball.
TT_PHYSICS_SNAPSHOT_EXPORT int TT_PhysicsAuditBeamEntity(
    CKContext *context, CK3dEntity *entity, float x, float y, float z);

#endif // PHYSICS_RT_PHYSICSSNAPSHOT_H
