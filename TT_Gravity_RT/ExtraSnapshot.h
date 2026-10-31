#ifndef TT_GRAVITY_RT_EXTRASNAPSHOT_H
#define TT_GRAVITY_RT_EXTRASNAPSHOT_H

class CKContext;

#if defined(_WIN32)
#define TT_EXTRA_SNAPSHOT_EXPORT extern "C" __declspec(dllexport)
#else
#define TT_EXTRA_SNAPSHOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

typedef void *TT_ExtraSnapshotHandle;
typedef void *TT_ExtraRestoreHandle;

TT_EXTRA_SNAPSHOT_EXPORT int TT_ExtraCaptureSnapshot(
    CKContext *context, TT_ExtraSnapshotHandle *snapshot);
TT_EXTRA_SNAPSHOT_EXPORT int TT_ExtraRestoreSnapshot(
    CKContext *context, TT_ExtraSnapshotHandle snapshot);
// Preparation validates every owner and allocates all replacement nodes without
// modifying CK state. The prepared handle owns its data independently of snapshot.
// Commit revalidates owners and publishes all lists without further allocation.
// A committed handle cannot be committed again; destroy it in either case.
TT_EXTRA_SNAPSHOT_EXPORT int TT_ExtraPrepareRestore(
    CKContext *context, TT_ExtraSnapshotHandle snapshot, TT_ExtraRestoreHandle *prepared);
TT_EXTRA_SNAPSHOT_EXPORT int TT_ExtraCommitRestore(
    CKContext *context, TT_ExtraRestoreHandle prepared);
TT_EXTRA_SNAPSHOT_EXPORT void TT_ExtraDestroyRestore(TT_ExtraRestoreHandle prepared);
TT_EXTRA_SNAPSHOT_EXPORT void TT_ExtraDestroySnapshot(
    TT_ExtraSnapshotHandle snapshot);

#endif
