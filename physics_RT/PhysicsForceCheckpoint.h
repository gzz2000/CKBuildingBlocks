#ifndef PHYSICS_FORCE_CHECKPOINT_H
#define PHYSICS_FORCE_CHECKPOINT_H
class IVP_Controller;
struct IVP_Checkpoint_Controller_Payload;
// Reads actual installed values; does not execute the behavior or recompute
// transforms/normalization. Host fills and validates behavior owner separately.
bool TT_CapturePhysicsForceController(IVP_Controller *controller,
                                     IVP_Checkpoint_Controller_Payload &payload);
// Owns scalar values only; m_Core stays null until a future atomic publication.
IVP_Controller *TT_PreparePhysicsForceController(const IVP_Checkpoint_Controller_Payload &payload);
bool TT_TransferPhysicsForceControllers(IVP_Controller *oldController, IVP_Controller *newController, const IVP_Checkpoint_Controller_Payload &, bool commit);
#endif
