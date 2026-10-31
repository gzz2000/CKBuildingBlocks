#ifndef PHYSICS_COLLISION_DETECTION_CHECKPOINT_H
#define PHYSICS_COLLISION_DETECTION_CHECKPOINT_H

#include "CKBehavior.h"

class CKIpionManager;
class CK3dEntity;
class IVP_Listener_Collision;

// Body identity is the actual attachment, not the behavior's mutable target.
// A detached listener is represented by a null body and a zero body ID.
struct PhysicsCollisionDetectionState {
    CK_ID bodyId = 0;
    CK3dEntity *body = nullptr;
    double lastCollisionTime = 0;
    float sleepAfterwards = 0;
    float minSpeed = 0;
    float maxSpeed = 0;
    int collisionId = 0;
};

bool CapturePhysicsCollisionDetection(CKIpionManager *, CKBehavior *,
                                     PhysicsCollisionDetectionState &);
bool RestorePhysicsCollisionDetection(CKIpionManager *, CKBehavior *,
                                     const PhysicsCollisionDetectionState &, bool create);
IVP_Listener_Collision *GetPhysicsCollisionDetectionListener(CKBehavior *);

#endif
