#pragma once

#include "CKAll.h"

#if defined(_WIN32)
#define TT_GRAVITY_SNAPSHOT_EXPORT __declspec(dllexport)
#else
#define TT_GRAVITY_SNAPSHOT_EXPORT __attribute__((visibility("default")))
#endif

using TT_SimpleShadowSnapshotHandle = void *;

extern "C" TT_GRAVITY_SNAPSHOT_EXPORT int TT_SimpleShadowCaptureSnapshot(
    CKContext *context, TT_SimpleShadowSnapshotHandle *outSnapshot);
extern "C" TT_GRAVITY_SNAPSHOT_EXPORT int TT_SimpleShadowRestoreSnapshot(
    CKContext *context, TT_SimpleShadowSnapshotHandle snapshot);
extern "C" TT_GRAVITY_SNAPSHOT_EXPORT void TT_SimpleShadowDestroySnapshot(
    TT_SimpleShadowSnapshotHandle snapshot);
extern "C" TT_GRAVITY_SNAPSHOT_EXPORT int TT_SimpleShadowIsRuntimeMaterial(
    CKContext *context, CKMaterial *material);
