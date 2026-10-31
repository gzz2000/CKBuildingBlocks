#include "PhysicsSnapshot.h"
#include "PhysicsForceCheckpoint.h"
#include "PhysicsCollisionDetectionCheckpoint.h"
#include "ivp_constraint_local.hxx"
#include "ivp_actuator_spring.hxx"
#include <stdexcept>
#include <typeinfo>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CKContext.h"
#include "CK3dEntity.h"
#include "CKBehavior.h"
#include "CKIpionManager.h"
#include "PhysicsContact.h"
#include "ivp_friction.hxx"
#include "ivp_cache_object.hxx"
#include "ivp_mindist.hxx"
#include "ivp_collision_checkpoint.hxx"
#include "ivp_templates.hxx"
#include "ivp_ball.hxx"
#include "ivp_polygon.hxx"
#include "ivp_surman_polygon.hxx"
#include "ivp_compact_surface.hxx"
#include "ivp_material.hxx"
#include "ivp_hull_manager.hxx"
#include "ivp_physic_private.hxx"
#include "ivp_compact_ledge.hxx"
#include <cstdio>

namespace {
// Observation only: do not recalculate mindists or borrow transform caches.
// Feature orientation is historical state, even for a geometrically shared edge.
void TraceSnapshotMindists(CKIpionManager *manager, const char *phase)
{
    if (!std::getenv("BALLANCE_TRACE_SNAPSHOT_FEATURES"))
        return;
    const char *target = std::getenv("BALLANCE_TRACE_CORE_OBJECT");
    if (!target || !*target)
        return;
    IVP_Environment *env = manager->GetEnvironment();
    std::fprintf(stderr, "SNAPFEATURE phase=%s time=%a code=%u begin\n", phase,
        env->get_current_time().get_time(), unsigned(env->get_current_time_code()));
    auto dump = [&](IVP_Mindist *md, const char *list, unsigned rank, double key) {
        IVP_Synapse_Real *syn[2] = {md->get_synapse(0), md->get_synapse(1)};
        const char *names[2] = {syn[0]->get_object()->get_name(), syn[1]->get_object()->get_name()};
        if ((!names[0] || std::strcmp(names[0], target)) &&
            (!names[1] || std::strcmp(names[1], target))) return;
        std::fprintf(stderr, "SNAPFEATURE md=%p list=%s rank=%u key=%a state=%u sorted=%d len=%a selector=%u decrease=%u plane=(%a %a %a) angular=%a A=%s B=%s\n",
            static_cast<void *>(md),list,rank,key,unsigned(md->mindist_status),
            md->get_sorted_synapse(0)==syn[1],double(md->len_numerator),
            unsigned(md->coll_dist_selector),unsigned(md->coll_dist_decrease_counter),
            double(md->contact_plane.k[0]),double(md->contact_plane.k[1]),double(md->contact_plane.k[2]),
            md->sum_angular_hull_time,names[0]?names[0]:"<null>",names[1]?names[1]:"<null>");
        for (int side=0;side<2;++side) {
            const IVP_Compact_Edge *edge=syn[side]->get_edge();
            const IVP_Compact_Ledge *ledge=edge->get_compact_ledge();
            std::fprintf(stderr,"SNAPFEATURE side=%d status=%d edge=%p ledge=%p start=%d slot=%d vertices=",
                side,int(syn[side]->get_status()),static_cast<const void *>(edge),
                static_cast<const void *>(ledge),edge->get_start_point_index(),edge->get_edge_index());
            for(int i=0;i<3;++i) {
                const auto *v=edge->get_triangle()->get_edge(i)->get_start_point(ledge);
                std::fprintf(stderr," %a %a %a",double(v->k[0]),double(v->k[1]),double(v->k[2]));
            }
            std::fprintf(stderr,"\n");
        }
    };
    IVP_Mindist_Manager *mm=env->get_mindist_manager();
    unsigned rank=0;
    for(IVP_Mindist *md=mm->exact_mindists;md;md=md->next) dump(md,"exact",rank++,0);
    rank=0;
    for(IVP_Mindist *md=mm->invalid_mindists;md;md=md->next) dump(md,"invalid",rank++,0);
    for(PhysicsObjectTable::Iterator it=manager->m_PhysicsObjects.Begin();it!=manager->m_PhysicsObjects.End();++it) {
        IVP_Real_Object *object=(*it).m_RealObject;
        if(!object || !object->get_name() || std::strcmp(object->get_name(),target)) continue;
        IVP_U_Min_List_Enumerator iter(object->get_hull_manager()->get_sorted_synapses());
        rank=0;
        while(auto *entry=iter.get_next_element_header()) {
            auto *syn=dynamic_cast<IVP_Synapse *>(static_cast<IVP_Listener_Hull *>(entry->element));
            if(syn) dump(static_cast<IVP_Mindist *>(syn->get_synapse_mindist()),"hull",rank,double(entry->value));
            ++rank;
        }
    }
    std::fprintf(stderr,"SNAPFEATURE phase=%s end\n",phase);
}
struct PhysicsObjectState {
    struct ListenerOwner {
        // Continuous contact is owned by this PhysicsObject; detection by a BB.
        bool contact = false;
        CK_ID behaviorId = 0;
        CKBehavior *behavior = nullptr;
    };
    std::vector<ListenerOwner> listenerOrder;
    struct ContactOutputState {
        CKBOOL active = FALSE;
        int number = 0;
    };
    struct ContactRecordState {
        int index = 0;
        double age = 0.0;
    };

    CK_ID entityId = 0;
    CK3dEntity *identity = NULL;
    IVP_Real_Object *physicsIdentity = NULL;
    uint64_t physicsLifetime = 0;
    IVP_Material *materialIdentity = NULL;
    CK_ID physicalizeBehaviorId = 0;
    CKBehavior *physicalizeIdentity = NULL;
    IVP_U_Quat rotation;
    IVP_U_Point position;
    IVP_U_Float_Point speed;
    IVP_U_Float_Point rotationSpeed;
    IVP_U_Float_Point speedChange;
    IVP_U_Float_Point rotationSpeedChange;
    // IVP advances a core only at PSI boundaries and interpolates between the
    // two surrounding PSI states.  These are authoritative simulation state,
    // not render caches: beam_object_to_new_position() deliberately clears
    // them, which otherwise makes the first replay tick stand still.
    IVP_BOOL temporarilyUnmovable = IVP_FALSE;
    short impactsSinceLastPsi = 0;
    IVP_Time coreTimeOfLastPsi;
    IVP_FLOAT inverseDeltaTime = 0.0f;
    IVP_U_Point positionLastPsi;
    IVP_U_Float_Point positionDeltaPerSecond;
    IVP_U_Quat rotationLastPsi;
    IVP_U_Quat rotationNextPsi;
    IVP_U_Matrix matrixLastPsi;
    IVP_U_Float_Point rotationAxisWorld;
    IVP_FLOAT currentSpeed = 0.0f;
    IVP_FLOAT absoluteOmega = 0.0f;
    IVP_FLOAT maxSurfaceRotationSpeed = 0.0f;
    IVP_Time calmReferenceTime[2];
    IVP_U_Float_Quat calmReferenceRotation[2];
    IVP_U_Float_Point calmReferencePosition[2];
    IVP_Time_CODE mindistEventAlreadyDone = 0;
    IVP_Movement_Type movement = IVP_MT_UNDEFINED;
    IVP_BOOL collisionEnabled = IVP_FALSE;
    int frictionCount = 0;
    IVP_Time frictionTime;
    CKBOOL hasContactData = FALSE;
    CK_ID contactBehaviorId = 0;
    CKBehavior *contactBehaviorIdentity = NULL;
    float contactDelayStart = 0.0f;
    float contactDelayEnd = 0.0f;
    std::vector<ContactOutputState> contactOutputs;
    std::vector<ContactRecordState> contactRecords;
};

struct PhysicsBehaviorState {
    CK_ID behaviorId = 0;
    CKBehavior *identity = NULL;
    CKGUID guid;
    PhysicsCollisionDetectionState collision;
};

struct PendingPhysicsBehaviorState {
    CK_ID behaviorId = 0;
    CKBehavior *identity = NULL;
    CKGUID guid;
    int phase = 0; // 0: pre-simulation, 1: post-simulation
    int priority = 0;
    std::shared_ptr<const PhysicsCallback> operation;
};

struct OrderedHostContactRecord {
    CK_ID entityId = 0;
    CK3dEntity *identity = NULL;
    int index = 0;
    IVP_Time time;
};
struct HostMaterialOwner {
    CK_ID entityId = 0;
    CK3dEntity *identity = NULL;
    IVP_Material *materialToken = NULL; // match to checkpoint material identity
};
struct HostObjectIdentity {
    CK_ID entityId = 0;
    CK3dEntity *identity = NULL;
};
struct HostMaterialState {
    IVP_Material *identity = nullptr;
    uint64_t lifetime = 0;
    bool managed = false;
};
struct PhysicsSnapshot {
    std::unique_ptr<IVP_Collision_Checkpoint> collisionCheckpoint;
    std::vector<PhysicsObjectState> objects;
    std::vector<PhysicsBehaviorState> activeBehaviors;
    std::vector<PendingPhysicsBehaviorState> pendingBehaviors;
    CKBOOL callbackFlags[2] = {FALSE, FALSE};
    std::vector<OrderedHostContactRecord> orderedContacts;
    std::vector<HostObjectIdentity> movableOrder;
    std::vector<HostMaterialOwner> materialOwners;
    std::vector<HostMaterialState> materials;
    VxVector gravity;
    float timeFactor = 0.001f;
    int randomSeed = 1;
    CKIpionManager::SimulationClockState clock = {};
    CKIpionManager::SimulationStepState step = {};
    IVP_Real_Object *environmentStaticIdentity = NULL;
    IVP_Material *environmentStaticMaterial = NULL;
};

const CKGUID kPhysicalizeGuid(0x7522370e, 0x37ec15ec);
const CKGUID kBallJointGuid(0x5e624f0a, 0x35160450);
const CKGUID kBuoyancyGuid(0x2c015f2b, 0x5b147512);
const CKGUID kCollisionDetectionGuid(0x7435488d, 0x201d1188);
const CKGUID kContinuousContactGuid(0x199e4cf1, 0x545a78fe);
const CKGUID kForceGuid(0x56e20c57, 0x0b926068);
const CKGUID kHingeGuid(0x41cd3653, 0x0de60c1d);
const CKGUID kSliderGuid(0x2973360e, 0x23d31aa7);
const CKGUID kSpringGuid(0x24a06a3a, 0x07100fce);

bool IsControllerOwner(CKBehavior *owner, const std::string &schema)
{
    if (!owner) return false;
    const auto guid = owner->GetPrototypeGuid();
    if (schema == "IVP.Spring/v1") return guid == kSpringGuid;
    if (schema == "CKBB.PhysicsForce/v1") return guid == kForceGuid;
    if (schema == "IVP.ConstraintLocal/v1")
        return guid == kBallJointGuid || guid == kHingeGuid || guid == kSliderGuid;
    return false;
}
bool CaptureExternalController(IVP_Controller *controller,
    IVP_Checkpoint_Controller_Payload &payload, void *captureContext)
{
    if (!TT_CapturePhysicsForceController(controller, payload)) {
        auto *constraint = dynamic_cast<IVP_Constraint_Local *>(controller);
        if (!constraint || !constraint->capture_checkpoint(payload)) {
            auto *spring = dynamic_cast<IVP_Actuator_Spring *>(controller);
            if (!spring || !spring->capture_checkpoint(payload)) return false;
        }
    }
    auto *context = static_cast<CKContext *>(captureContext);
    CKBehavior *owner = NULL;
    const auto &behaviors = context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    for (auto it = behaviors.Begin(); it != behaviors.End(); ++it) {
        auto *behavior = static_cast<CKBehavior *>(*it);
        if (!IsControllerOwner(behavior, payload.schema)) continue;
        void *installed = NULL;
        behavior->GetLocalParameterValue(0, &installed);
        if (installed != controller) continue;
        if (owner) throw std::runtime_error("controller has multiple behavior owners");
        owner = behavior;
    }
    if (!owner) throw std::runtime_error("controller lacks behavior owner");
    payload.owner_id = owner->GetID();
    payload.owner_token = owner;
    return true;
}

IVP_Controller *PrepareExternalController(const IVP_Checkpoint_Controller_Payload &payload,
                                          void *prepareContext)
{
    auto *context = static_cast<CKContext *>(prepareContext);
    auto *owner = CKBehavior::Cast(context->GetObject(static_cast<CK_ID>(payload.owner_id)));
    if (!owner || owner != payload.owner_token || !IsControllerOwner(owner, payload.schema))
        throw std::runtime_error("restored controller owner identity mismatch");
    IVP_Controller *controller = payload.schema == "IVP.ConstraintLocal/v1"
        ? IVP_Constraint_Local::prepare_checkpoint(payload)
        : payload.schema == "IVP.Spring/v1" ? IVP_Actuator_Spring::prepare_checkpoint(payload)
        : TT_PreparePhysicsForceController(payload);
    if (!controller) throw std::runtime_error("unsupported or invalid host controller payload");
    return controller;
}

bool ApplyExistingExternalController(IVP_Controller *oldController, IVP_Controller *newController,
    const IVP_Checkpoint_Controller_Payload &payload, bool commit, void *opaque)
{
    auto *context=static_cast<CKContext *>(opaque);
    auto *owner=CKBehavior::Cast(context->GetObject(CK_ID(payload.owner_id)));
    if(!owner || owner!=payload.owner_token || !IsControllerOwner(owner,payload.schema))return false;
    void *installed=NULL;owner->GetLocalParameterValue(0,&installed);
    if(installed!=oldController)return false;
    bool transferred=payload.schema=="IVP.ConstraintLocal/v1"
        ? IVP_Constraint_Local::transfer_checkpoint(oldController,newController,payload,commit)
        : payload.schema=="IVP.Spring/v1"
        ? IVP_Actuator_Spring::transfer_checkpoint(oldController,newController,payload,commit)
        : TT_TransferPhysicsForceControllers(oldController,newController,payload,commit);
    if(!transferred)return false;
    if(commit)owner->SetLocalParameterValue(0,&newController);
    return true;
}
template<class T> void AdoptHostVector(IVP_U_Vector<T> &target, IVP_U_Vector<T> &source)
{
    target.clear();target.elems=source.elems;target.memsize=source.memsize;target.n_elems=source.n_elems;
    source.elems=NULL;source.memsize=source.n_elems=0;
}
// Prepare dormant queue entries without executing BB code. BB inputs/targets
// are restored separately; enqueueing must neither re-read them now nor create
// forces/listeners ahead of their original simulation boundary.
class PreparedPhysicsCallbacks {
public:
    PreparedPhysicsCallbacks(CKIpionManager *manager, const PhysicsSnapshot &saved)
        : m_Manager(manager) {
        if (!manager || manager->m_CallbackProcessingDepth)
            throw std::runtime_error("cannot restore a processing callback queue");
        m_Owned.reserve(saved.pendingBehaviors.size());
        for (int phase=0; phase<2; ++phase) {
            m_Flags[phase]=saved.callbackFlags[phase];
            if (!Container(phase))
                throw std::runtime_error("physics callback container unavailable");
        }
        for (const auto &state : saved.pendingBehaviors) {
            if (state.phase<0 || state.phase>1 || state.priority<0 || state.priority>2 ||
                !state.operation || state.operation->m_Type!=state.priority ||
                state.operation->m_BehaviorID!=state.behaviorId ||
                manager->m_Context->GetObject(state.behaviorId)!=state.identity ||
                !state.identity || state.identity->GetPrototypeGuid()!=state.guid)
                throw std::runtime_error("pending physics callback identity/schema changed");
            std::unique_ptr<PhysicsCallback> copy(state.operation->CloneForSnapshot());
            if (!copy || copy->m_Type!=state.priority || copy->m_BehaviorID!=state.behaviorId ||
                typeid(*copy)!=typeid(*state.operation))
                throw std::runtime_error("pending physics callback clone changed type/payload");
            copy->m_IpionManager=manager;
            m_Queues[state.phase][state.priority].add(copy.get());
            m_Owned.push_back(std::move(copy));
        }
        for (int phase=0; phase<2; ++phase) {
            bool present=false;
            for (int priority=0; priority<3; ++priority)
                present|=m_Queues[phase][priority].len()!=0;
            if (bool(m_Flags[phase])!=present)
                throw std::runtime_error("pending physics callback flag/queue mismatch");
        }
    }

    void Commit() {
        for (int phase=0; phase<2; ++phase) {
            auto *container=Container(phase);
            container->Clear();
            for (int priority=0; priority<3; ++priority)
                AdoptHostVector(container->m_Callbacks[priority],m_Queues[phase][priority]);
            container->m_HasCallbacks=m_Flags[phase];
        }
        for (auto &owner:m_Owned) owner.release();
        std::fprintf(stderr,"PHYSICS_PENDING_COMMIT count=%zu pre=%d post=%d\n",
            m_Owned.size(),int(m_Flags[0]),int(m_Flags[1]));
    }
private:
    PhysicsCallbackContainer *Container(int phase) const {
        return phase ? m_Manager->m_PostSimulateCallbacks : m_Manager->m_PreSimulateCallbacks;
    }
    CKIpionManager *m_Manager;
    CKBOOL m_Flags[2];
    IVP_U_Vector<PhysicsCallback> m_Queues[2][3];
    std::vector<std::unique_ptr<PhysicsCallback>> m_Owned;
};

int RestoreExactExistingGraph(CKContext *context,CKIpionManager *manager,const PhysicsSnapshot &saved,
                              bool reboundOrder=false, PreparedPhysicsCallbacks *preparedCallbacks=nullptr)
{
    try {
        if(!saved.collisionCheckpoint)throw std::runtime_error("exact graph was not captured");
        std::unique_ptr<PreparedPhysicsCallbacks> ownedCallbacks;
        if (!preparedCallbacks) {
            ownedCallbacks.reset(new PreparedPhysicsCallbacks(manager,saved));
            preparedCallbacks=ownedCallbacks.get();
        }
        if(manager->GetPhysicsObjectCount()!=int(saved.objects.size()))throw std::runtime_error("host object topology changed");
        for (const auto &state : saved.activeBehaviors) if (state.guid == kCollisionDetectionGuid) {
            PhysicsCollisionDetectionState current;
            if (context->GetObject(state.behaviorId) != state.identity ||
                !CapturePhysicsCollisionDetection(manager, state.identity, current) ||
                current.bodyId != state.collision.bodyId || current.body != state.collision.body)
                throw std::runtime_error("collision listener attachment changed");
        }
        std::unordered_map<CK_ID,PhysicsObject *> objects;
        auto *cluster = manager->GetEnvironment()->get_cluster_manager();
        std::vector<std::pair<IVP_Real_Object *,std::vector<IVP_Listener_Collision *>>> listenerOrders;
        for(const auto &state:saved.objects) {
            auto *entity=CK3dEntity::Cast(context->GetObject(state.entityId));
            if(entity!=state.identity)throw std::runtime_error("host object identity changed");
            auto *object=manager->GetPhysicsObject(entity,FALSE);
            if(!object || object->m_RealObject!=state.physicsIdentity ||
                manager->GetPhysicsObjectLifetime(state.entityId)!=state.physicsLifetime)
                throw std::runtime_error("native object lifetime changed");
            auto *contact=object->m_ContactData;
            if(bool(contact)!=bool(state.hasContactData) || (contact &&
                (contact->m_BehaviorID!=state.contactBehaviorId || size_t(contact->m_GroupOutputCount)!=state.contactOutputs.size())))
                throw std::runtime_error("host contact listener topology changed");
            std::vector<IVP_Listener_Collision *> order;
            for (const auto &owner : state.listenerOrder) {
                IVP_Listener_Collision *listener = nullptr;
                if (owner.contact) {
                    if (contact) listener = contact->m_Listener;
                } else if (context->GetObject(owner.behaviorId) == owner.behavior && owner.behavior &&
                           owner.behavior->GetPrototypeGuid() == kCollisionDetectionGuid) {
                    listener = GetPhysicsCollisionDetectionListener(owner.behavior);
                }
                if (!listener) throw std::runtime_error("saved collision callback owner unavailable");
                order.push_back(listener);
            }
            if (!cluster->reorder_collision_listeners(object->m_RealObject, order.data(), int(order.size()), false))
                throw std::runtime_error("collision callback identity set changed");
            listenerOrders.emplace_back(object->m_RealObject,std::move(order));
            objects.emplace(state.entityId,object);
        }
        if(size_t(manager->m_MaterialOwners.len())!=saved.materialOwners.size())throw std::runtime_error("host material ownership changed");
        std::unique_ptr<IVP_U_Vector<PhysicsMaterialOwner>> materialOrder;
        if(reboundOrder)materialOrder.reset(new IVP_U_Vector<PhysicsMaterialOwner>(int(saved.materialOwners.size())));
        for(size_t i=0;i<saved.materialOwners.size();++i) {
            const auto &state=saved.materialOwners[i];auto *now=manager->m_MaterialOwners.element_at(int(i));
            if(reboundOrder) {
                now=NULL;
                for(int j=0;j<manager->m_MaterialOwners.len();++j) {
                    auto *candidate=manager->m_MaterialOwners.element_at(j);
                    if(candidate && candidate->m_Owner==state.entityId && candidate->m_Material==state.materialToken) {
                        if(now)throw std::runtime_error("duplicate rebound material owner");
                        now=candidate;
                    }
                }
            }
            if(!now || now->m_Owner!=state.entityId || now->m_Material!=state.materialToken || context->GetObject(state.entityId)!=state.identity)
                throw std::runtime_error("host material identity/order changed");
            if(materialOrder)materialOrder->add(now);
        }
        auto *contacts=manager->GetContactManager();
        if(!contacts && !saved.orderedContacts.empty())throw std::runtime_error("contact manager unavailable");
        IVP_U_Vector<PhysicsContactRecord> records(int(saved.orderedContacts.size()));
        std::vector<std::unique_ptr<PhysicsContactRecord>> record_owners;
        for(const auto &state:saved.orderedContacts) {
            auto *object=objects.at(state.entityId);
            if(context->GetObject(state.entityId)!=state.identity)throw std::runtime_error("record owner changed");
            auto record=std::unique_ptr<PhysicsContactRecord>(new PhysicsContactRecord(state.time,state.index,object));
            records.add(record.get());record_owners.push_back(std::move(record));
        }
        IVP_U_Vector<IVP_Real_Object> movable(int(saved.movableOrder.size()));
        for(const auto &state:saved.movableOrder) {
            if(context->GetObject(state.entityId)!=state.identity)throw std::runtime_error("movable owner changed");
            movable.add(objects.at(state.entityId)->m_RealObject);
        }
        char error[256];
        const bool restored=reboundOrder
            ? saved.collisionCheckpoint->restore_rebound_objects(manager->GetEnvironment(),CaptureExternalController,
                PrepareExternalController,ApplyExistingExternalController,context,error,sizeof(error))
            : saved.collisionCheckpoint->restore_same_objects(manager->GetEnvironment(),CaptureExternalController,
                PrepareExternalController,ApplyExistingExternalController,context,error,sizeof(error));
        if(!restored)
            throw std::runtime_error(error);
        size_t orderedListeners = 0;
        for (const auto &order : listenerOrders) {
            if (!cluster->reorder_collision_listeners(order.first, order.second.data(), int(order.second.size()), true))
                throw std::runtime_error("collision callback set changed during native commit");
            for (size_t i = 0; i < order.second.size(); ++i)
                if (cluster->get_collision_listener(order.first,int(i)) != order.second[i])
                    throw std::runtime_error("collision callback order verification failed");
            orderedListeners += order.second.size();
        }
        // Native graph has committed; all host allocations and mappings above
        // already succeeded. Do not invoke behavior reset/create callbacks here.
        manager->m_PhysicsTimeFactor=saved.timeFactor;
        ivp_srand(saved.randomSeed);
        size_t listeners = 0;
        for (const auto &state : saved.activeBehaviors) if (state.guid == kCollisionDetectionGuid) {
            if (!RestorePhysicsCollisionDetection(manager, state.identity, state.collision, false))
                throw std::runtime_error("collision listener changed during graph commit");
            PhysicsCollisionDetectionState actual;
            if (!CapturePhysicsCollisionDetection(manager,state.identity,actual) ||
                actual.bodyId != state.collision.bodyId || actual.body != state.collision.body ||
                actual.collisionId != state.collision.collisionId ||
                std::memcmp(&actual.lastCollisionTime,&state.collision.lastCollisionTime,sizeof(double)) ||
                std::memcmp(&actual.sleepAfterwards,&state.collision.sleepAfterwards,sizeof(float)) ||
                std::memcmp(&actual.minSpeed,&state.collision.minSpeed,sizeof(float)) ||
                std::memcmp(&actual.maxSpeed,&state.collision.maxSpeed,sizeof(float)))
                throw std::runtime_error("collision listener scalar verification failed");
            ++listeners;
        }
        std::fprintf(stderr,"PHYSICS_LISTENERS_COMMIT count=%zu ordered=%zu scalar_differences=0 order_differences=0\n",listeners,orderedListeners);
        for(const auto &state:saved.objects) {
            auto *object=objects.find(state.entityId)->second;
            object->m_FrictionCount=state.frictionCount;object->m_FrictionTime=state.frictionTime;
            if(auto *contact=object->m_ContactData) {
                contact->m_TimeDelayStart=state.contactDelayStart;contact->m_TimeDelayEnd=state.contactDelayEnd;
                for(size_t i=0;i<state.contactOutputs.size();++i) {
                    contact->m_GroupOutputs[i].active=state.contactOutputs[i].active;
                    contact->m_GroupOutputs[i].number=state.contactOutputs[i].number;
                }
            }
        }
        if(contacts){contacts->ClearRecords();AdoptHostVector(contacts->m_Records,records);}
        for(auto &record:record_owners)record.release();
        AdoptHostVector(manager->m_MovableObjects,movable);
        if(materialOrder)AdoptHostVector(manager->m_MaterialOwners,*materialOrder);
        preparedCallbacks->Commit();
        std::fprintf(stderr,"PHYSICS_HOST_COMMIT objects=%zu records=%zu\n",saved.objects.size(),saved.orderedContacts.size());
        return CK_OK;
    } catch(const std::bad_alloc &){return CKERR_OUTOFMEMORY;}
      catch(const std::exception &error){context->OutputToConsoleEx("Exact graph restore rejected: %s",error.what());return CKERR_INVALIDPARAMETER;}
}

bool IsRebuildablePhysicsBehavior(const CKGUID &guid)
{
    return guid == kBallJointGuid || guid == kBuoyancyGuid ||
           guid == kCollisionDetectionGuid || guid == kContinuousContactGuid ||
           guid == kForceGuid || guid == kHingeGuid ||
           guid == kSliderGuid || guid == kSpringGuid;
}

bool HasInstalledPhysicsRuntime(CKBehavior *behavior)
{
    if (!behavior)
        return false;
    const CKGUID guid = behavior->GetPrototypeGuid();
    if (guid == kBuoyancyGuid)
    {
        CKBOOL installed = FALSE;
        behavior->GetLocalParameterValue(0, &installed);
        return installed != FALSE;
    }

    void *handle = NULL;
    const int handleIndex = guid == kContinuousContactGuid ? 1 : 0;
    behavior->GetLocalParameterValue(handleIndex, &handle);
    return handle != NULL;
}

CKIpionManager *GetPhysicsManager(CKContext *context)
{
    return context ? CKIpionManager::GetManager(context) : NULL;
}

// Bodies can survive a Stop input while their collision listener or controller
// is retired. Body pointer equality alone therefore cannot select the in-place
// path. Compare the same host runtime inventory that capture serializes; the
// recreation path below owns rebuilding it before exact IVP graph restoration.
bool HostRuntimeTopologyChanged(CKContext *context, CKIpionManager *manager,
                                const PhysicsSnapshot &saved)
{
    bool changed = false;
    std::unordered_map<CK_ID, const PhysicsBehaviorState *> expected;
    for (const auto &state : saved.activeBehaviors) {
        if (context->GetObject(state.behaviorId) != state.identity ||
            state.identity->GetPrototypeGuid() != state.guid ||
            !expected.emplace(state.behaviorId, &state).second)
            throw std::runtime_error("saved runtime owner identity/schema changed");
        if (!HasInstalledPhysicsRuntime(state.identity)) {
            changed = true;
            std::fprintf(stderr, "PHYSICS_RUNTIME_TOPOLOGY missing behavior=%u\n", unsigned(state.behaviorId));
        } else if (state.guid == kCollisionDetectionGuid) {
            PhysicsCollisionDetectionState current;
            if (!CapturePhysicsCollisionDetection(manager, state.identity, current))
                throw std::runtime_error("current collision listener ownership invalid");
            if (current.bodyId != state.collision.bodyId || current.body != state.collision.body) {
                changed = true;
                std::fprintf(stderr, "PHYSICS_RUNTIME_TOPOLOGY attachment behavior=%u current=%u saved=%u\n",
                    unsigned(state.behaviorId), unsigned(current.bodyId), unsigned(state.collision.bodyId));
            }
        }
    }
    const auto &behaviors = context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    for (auto it = behaviors.Begin(); it != behaviors.End(); ++it) {
        auto *behavior = CKBehavior::Cast(*it);
        if (behavior && IsRebuildablePhysicsBehavior(behavior->GetPrototypeGuid()) &&
            behavior->GetPrototypeGuid() != kContinuousContactGuid &&
            HasInstalledPhysicsRuntime(behavior) && !expected.count(behavior->GetID())) {
            changed = true;
            std::fprintf(stderr, "PHYSICS_RUNTIME_TOPOLOGY extra behavior=%u\n", unsigned(behavior->GetID()));
        }
    }
    auto *cluster = manager->GetEnvironment()->get_cluster_manager();
    for (const auto &state : saved.objects) {
        auto *object = manager->GetPhysicsObject(state.identity, FALSE);
        if (!object || object->m_RealObject != state.physicsIdentity ||
            manager->GetPhysicsObjectLifetime(state.entityId) != state.physicsLifetime) {
            changed = true;
            continue;
        }
        auto *contact = object->m_ContactData;
        if (bool(contact) != bool(state.hasContactData) || (contact &&
            (contact->m_BehaviorID != state.contactBehaviorId ||
             size_t(contact->m_GroupOutputCount) != state.contactOutputs.size())))
            changed = true;
        std::vector<IVP_Listener_Collision *> order;
        for (const auto &owner : state.listenerOrder) {
            if (!owner.contact && (context->GetObject(owner.behaviorId) != owner.behavior ||
                !owner.behavior || owner.behavior->GetPrototypeGuid() != kCollisionDetectionGuid))
                throw std::runtime_error("saved collision callback owner identity changed");
            auto *listener = owner.contact ? (contact ? contact->m_Listener : nullptr)
                : GetPhysicsCollisionDetectionListener(owner.behavior);
            if (!listener) changed = true;
            order.push_back(listener);
        }
        if (!cluster->reorder_collision_listeners(object->m_RealObject, order.data(), int(order.size()), false))
            changed = true;
    }
    return changed;
}

// Experimental lifecycle adapter. Exact graph validation remains mandatory;
// this is not the application's eventual prepare/commit/rollback transaction.
int RestoreRecreatedGraph(CKContext *context,CKIpionManager *manager,const PhysicsSnapshot &saved)
{
    const char *phase="preflight";
    try {
        auto *env=manager->GetEnvironment();
        if(!saved.collisionCheckpoint)
            throw std::runtime_error("missing collision checkpoint");
        for(const auto &material:saved.materials)if(!material.managed) {
            if(IVP_Material::get_lifetime(material.identity)!=material.lifetime ||
                manager->OwnsMaterial(material.identity))
                throw std::runtime_error("external material lifetime or ownership changed");
        }
        PreparedPhysicsCallbacks preparedCallbacks(manager,saved);
        bool changed=manager->GetPhysicsObjectCount()!=int(saved.objects.size());
        std::unordered_map<CK_ID,const PhysicsObjectState *> desired;
        std::vector<const PhysicsObjectState *> recreate;
        std::unordered_map<CK_ID,IVP_Collision_Checkpoint::ObjectRecipe> descriptions;
        std::unordered_map<const void *,IVP_SurfaceManager *> shapes;
        for(const auto &state:saved.objects) {
            if(context->GetObject(state.entityId)!=state.identity || !desired.emplace(state.entityId,&state).second)
                throw std::runtime_error("saved CK entity missing/reused/duplicated");
            auto *now=manager->GetPhysicsObject(state.identity,FALSE);
            changed|=!now || now->m_RealObject!=state.physicsIdentity ||
                manager->GetPhysicsObjectLifetime(state.entityId)!=state.physicsLifetime;
            auto &description=descriptions[state.entityId];
            if(!saved.collisionCheckpoint->describe_object(state.physicsIdentity,description))
                throw std::runtime_error("missing captured object descriptor");
            if(!now)recreate.push_back(&state);
            else if(description.shape_identity) {
                auto inserted=shapes.emplace(description.shape_identity,now->m_RealObject->get_surface_manager());
                if(!inserted.second && inserted.first->second!=now->m_RealObject->get_surface_manager())
                    throw std::runtime_error("current shared shape manager alias changed");
            }
            if(state.hasContactData && (!state.contactBehaviorIdentity ||
                context->GetObject(state.contactBehaviorId)!=state.contactBehaviorIdentity ||
                state.contactBehaviorIdentity->GetTarget()!=state.identity))
                throw std::runtime_error("saved contact behavior identity/target changed");
        }
        changed |= HostRuntimeTopologyChanged(context, manager, saved);
        if(!changed)return RestoreExactExistingGraph(context,manager,saved,false,&preparedCallbacks);
        if(env->get_static_object()!=saved.environmentStaticIdentity)
            throw std::runtime_error("environment static identity changed");
        for(const auto &state:saved.activeBehaviors) {
            if(context->GetObject(state.behaviorId)!=state.identity || state.identity->GetPrototypeGuid()!=state.guid)
                throw std::runtime_error("saved controller owner identity changed");
            if(state.guid==kBuoyancyGuid)throw std::runtime_error("buoyancy lifecycle codec not available");
        }
        std::vector<IVP_Real_Object *> retire;
        for(auto it=manager->m_PhysicsObjects.Begin();it!=manager->m_PhysicsObjects.End();++it) {
            auto *entity=context->GetObject(it.GetKey());
            if(!entity || !(*it).m_RealObject)throw std::runtime_error("orphan current host physics entry");
            if(!desired.count(it.GetKey()))retire.push_back((*it).m_RealObject);
        }
        std::vector<CKBehavior *> reset;
        const auto &behaviors=context->GetObjectListByType(CKCID_BEHAVIOR,TRUE);
        for(auto it=behaviors.Begin();it!=behaviors.End();++it) {
            auto *behavior=CKBehavior::Cast(*it);
            if(behavior && IsRebuildablePhysicsBehavior(behavior->GetPrototypeGuid()))reset.push_back(behavior);
        }
        // Named/shared collision caches outlive dephysicalized objects. Reuse
        // only an explicitly captured surface identity, never a guessed name.
        for(const auto &entry:descriptions)if(entry.second.shape_identity && !shapes.count(entry.second.shape_identity))
            for(int i=0;i<manager->m_CollisionSurfaceOwners.len();++i) {
                auto *owned=manager->m_CollisionSurfaceOwners.element_at(i);
                if(owned && owned->m_CompactSurface==entry.second.shape_identity)
                    shapes.emplace(entry.second.shape_identity,owned->m_SurfaceManager);
            }
        std::unordered_map<const IVP_Material *,IVP_Material *> plannedMaterials;
        std::vector<std::unique_ptr<IVP_Material>> createdMaterials;
        for(const auto &material:saved.materials) {
            IVP_Material *target=material.managed?nullptr:material.identity;
            for(const auto &state:saved.objects)if(state.materialIdentity==material.identity) {
                auto *now=manager->GetPhysicsObject(state.identity,FALSE);
                if(!now)continue;
                auto *current=now->m_RealObject->l_default_material;
                if(!current || (target && target!=current))
                    throw std::runtime_error("surviving material aliases changed");
                target=current;
            }
            if(material.identity==saved.environmentStaticMaterial) {
                auto *current=env->get_static_object()->l_default_material;
                if(target && target!=current)throw std::runtime_error("static material alias changed");
                target=current;
            }
            if(target && manager->OwnsMaterial(target)!=material.managed)
                throw std::runtime_error("surviving material ownership changed");
            if(!target) {
                std::unique_ptr<IVP_Material> created(saved.collisionCheckpoint->recreate_material(material.identity));
                if(!created)throw std::runtime_error("material reconstruction failed");
                target=created.get();createdMaterials.push_back(std::move(created));
            }
            plannedMaterials.emplace(material.identity,target);
        }
        std::unordered_set<IVP_Material *> uniqueMaterials;
        for(const auto &entry:plannedMaterials)
            if(!uniqueMaterials.insert(entry.second).second)
                throw std::runtime_error("distinct saved material aliases merged");
        IVP_U_Vector<PhysicsMaterialOwner> plannedOwners(int(saved.materialOwners.size()));
        std::vector<std::unique_ptr<PhysicsMaterialOwner>> createdOwners;
        for(const auto &owner:saved.materialOwners) {
            if(context->GetObject(owner.entityId)!=owner.identity)
                throw std::runtime_error("saved material owner identity changed");
            auto entry=std::unique_ptr<PhysicsMaterialOwner>(new PhysicsMaterialOwner(owner.entityId,plannedMaterials.at(owner.materialToken)));
            plannedOwners.add(entry.get());createdOwners.push_back(std::move(entry));
        }
        std::fprintf(stderr,"PHYSICS_RECREATE preflight saved=%zu retire=%zu create=%zu\n",saved.objects.size(),retire.size(),recreate.size());
        phase="retire runtime";
        for(auto *behavior:reset) {
            manager->ClearBehaviorCallbacks(behavior->GetID());
            behavior->CallCallbackFunction(CKM_BEHAVIORRESET);
        }
        if(manager->GetContactManager())manager->GetContactManager()->ClearRecords();
        for(auto *object:retire)object->delete_silently();
        phase="create objects";
        for(auto &material:createdMaterials) {
            manager->RetainMaterial(material.release());
        }
        for(const auto *state:recreate) {
            const auto &description=descriptions.at(state->entityId);
            IVP_SurfaceManager *surface=NULL;
            if(description.type==IVP_POLYGON) {
                auto found=shapes.find(description.shape_identity);
                if(found!=shapes.end())surface=found->second;
                else {
                    auto *bytes=static_cast<IVP_Compact_Surface *>(ivp_malloc_aligned(description.compact_surface.size(),16));
                    if(!bytes)throw std::bad_alloc();
                    std::memcpy(bytes,description.compact_surface.data(),description.compact_surface.size());
                    surface=new IVP_SurfaceManager_Polygon(bytes);
                    if(description.shape_references==1)manager->OwnPrivateCollisionSurface(state->identity,surface,bytes);
                    else manager->OwnCollisionSurface(surface,bytes);
                    shapes.emplace(description.shape_identity,surface);
                }
            } else if(description.type!=IVP_BALL)throw std::runtime_error("unsupported body recreation type");
            IVP_Material *material=NULL;
            if(description.material_identity) {
                material=plannedMaterials.at(description.material_identity);
            }
            IVP_Template_Real_Object objectTemplate;
            objectTemplate.set_name(description.name.c_str());objectTemplate.mass=1;
            objectTemplate.physical_unmoveable=description.unmoveable?IVP_TRUE:IVP_FALSE;
            objectTemplate.material=material;
            IVP_Real_Object *object=NULL;
            if(description.type==IVP_BALL) {
                IVP_Template_Ball ball;ball.radius=description.radius;
                object=env->create_ball(&ball,&objectTemplate,&state->rotation,&state->position);
            } else object=env->create_polygon(surface,&objectTemplate,&state->rotation,&state->position);
            if(!object)throw std::runtime_error("native body construction failed");
            object->client_data=state->identity;
            PhysicsObject host;host.m_RealObject=object;
            manager->RegisterPhysicsObject(state->entityId,host);
            std::fprintf(stderr,"PHYSICS_RECREATE body entity=%u type=%d name=%s\n",unsigned(state->entityId),description.type,description.name.c_str());
        }
        phase="create listeners";
        for(const auto &state:saved.activeBehaviors)if(state.guid==kCollisionDetectionGuid)
            if(!RestorePhysicsCollisionDetection(manager,state.identity,state.collision,true))
                throw std::runtime_error("collision listener recreation failed");
        for(const auto &state:saved.objects)if(state.hasContactData) {
            auto *object=manager->GetPhysicsObject(state.identity,FALSE);
            if(!object || CreatePhysicsContinuousContactData(manager,object,state.contactBehaviorIdentity)!=CKBR_OK)
                throw std::runtime_error("contact listener recreation failed");
        }
        // Publish the captured association list; asset ownership remains unique
        // and separate. Retired association entries never directly delete assets.
        IVP_U_Vector<PhysicsMaterialOwner> previousOwners;
        AdoptHostVector(previousOwners,manager->m_MaterialOwners);
        AdoptHostVector(manager->m_MaterialOwners,plannedOwners);
        for(auto &owner:createdOwners)owner.release();
        for(int i=0;i<previousOwners.len();++i) {
            auto *owner=previousOwners.element_at(i);
            manager->ReleaseUnusedMaterial(owner->m_Material);
            delete owner;
        }
        struct Mapping {
            std::unordered_map<const IVP_Real_Object *,IVP_Real_Object *> objects;
            std::unordered_map<const IVP_Material *,IVP_Material *> materials;
        } mapping;
        mapping.objects.emplace(saved.environmentStaticIdentity,env->get_static_object());
        if(saved.environmentStaticMaterial)
            mapping.materials.emplace(saved.environmentStaticMaterial,env->get_static_object()->l_default_material);
        PhysicsSnapshot rebound;
        rebound.objects=saved.objects;rebound.orderedContacts=saved.orderedContacts;
        rebound.movableOrder=saved.movableOrder;rebound.materialOwners=saved.materialOwners;
        rebound.materials=saved.materials;
        for(auto &material:rebound.materials) {
            material.identity=plannedMaterials.at(material.identity);
            material.lifetime=IVP_Material::get_lifetime(material.identity);
        }
        rebound.activeBehaviors=saved.activeBehaviors;rebound.pendingBehaviors=saved.pendingBehaviors;
        std::copy(saved.callbackFlags,saved.callbackFlags+2,rebound.callbackFlags);
        rebound.gravity=saved.gravity;rebound.timeFactor=saved.timeFactor;rebound.randomSeed=saved.randomSeed;
        rebound.clock=saved.clock;rebound.step=saved.step;
        rebound.environmentStaticIdentity=saved.environmentStaticIdentity;
        rebound.environmentStaticMaterial=saved.environmentStaticMaterial;
        for(auto &state:rebound.objects) {
            auto *object=manager->GetPhysicsObject(state.identity,FALSE);
            if(!object || !object->m_RealObject)throw std::runtime_error("recreated object absent");
            if(!mapping.objects.emplace(state.physicsIdentity,object->m_RealObject).second)
                throw std::runtime_error("duplicate saved native object identity");
            auto *material=object->m_RealObject->l_default_material;
            if(state.materialIdentity) {
                auto inserted=mapping.materials.emplace(state.materialIdentity,material);
                if(!material || (!inserted.second && inserted.first->second!=material))
                    throw std::runtime_error("recreated host material alias split");
            } else if(material)throw std::runtime_error("unexpected recreated default material");
            state.physicsIdentity=object->m_RealObject;state.materialIdentity=material;
            state.physicsLifetime=manager->GetPhysicsObjectLifetime(state.entityId);
        }
        for(auto &owner:rebound.materialOwners)owner.materialToken=mapping.materials.at(owner.materialToken);
        phase="rebind exact graph";
        auto object=[](const IVP_Real_Object *key,void *opaque)->IVP_Real_Object * {
            auto &map=static_cast<Mapping *>(opaque)->objects;auto found=map.find(key);return found==map.end()?NULL:found->second;
        };
        auto material=[](const IVP_Material *key,void *opaque)->IVP_Material * {
            auto &map=static_cast<Mapping *>(opaque)->materials;auto found=map.find(key);return found==map.end()?NULL:found->second;
        };
        char error[512];
        rebound.collisionCheckpoint.reset(saved.collisionCheckpoint->rebind_objects(env,object,material,&mapping,error,sizeof(error)));
        if(!rebound.collisionCheckpoint)throw std::runtime_error(error);
        const int result=RestoreExactExistingGraph(context,manager,rebound,true,&preparedCallbacks);
        std::fprintf(stderr,"PHYSICS_RECREATE complete result=%d objects=%zu\n",result,rebound.objects.size());
        return result;
    } catch(const std::exception &error) {
        context->OutputToConsoleEx("Physics lifecycle restore rejected phase=%s: %s",phase,error.what());
        return CKERR_INVALIDPARAMETER;
    }
}

void CapturePendingCallbacks(PhysicsCallbackContainer *container,
                             PhysicsSnapshot *snapshot, int phase)
{
    if (!container || !snapshot || phase<0 || phase>1)
        throw std::runtime_error("callback capture container unavailable");
    if (container->m_IpionManager->m_CallbackProcessingDepth)
        throw std::runtime_error("cannot capture a processing callback queue");
    snapshot->callbackFlags[phase]=container->m_HasCallbacks;
    for (int priority = 0; priority < 3; ++priority)
    {
        IVP_U_Vector<PhysicsCallback> &callbacks = container->m_Callbacks[priority];
        for (int index = 0; index < callbacks.len(); ++index)
        {
            PhysicsCallback *callback = callbacks.element_at(index);
            CKBehavior *behavior = callback ? callback->GetBehavior() : NULL;
            if (!behavior || callback->m_Type!=priority ||
                callback->m_IpionManager!=container->m_IpionManager)
                throw std::runtime_error("invalid pending callback owner/priority");
            std::shared_ptr<const PhysicsCallback> copy(callback->CloneForSnapshot());
            if (!copy || typeid(*copy)!=typeid(*callback))
                throw std::runtime_error("pending callback cannot be captured");
            snapshot->pendingBehaviors.push_back(
                {behavior->GetID(), behavior, behavior->GetPrototypeGuid(),phase,priority,std::move(copy)});
            std::fprintf(stderr,"PHYSICS_PENDING_CAPTURE phase=%d priority=%d index=%d behavior=%u guid=%08x:%08x\n",
                phase,priority,index,unsigned(behavior->GetID()),unsigned(behavior->GetPrototypeGuid().d1),unsigned(behavior->GetPrototypeGuid().d2));
        }
    }
}
}

int TT_PhysicsCaptureSnapshot(CKContext *context, TT_PhysicsSnapshotHandle *snapshot)
{
    if (!snapshot)
        return CKERR_INVALIDPARAMETER;
    *snapshot = NULL;

    CKIpionManager *manager = GetPhysicsManager(context);
    if (!manager || !manager->GetEnvironment())
        return CKERR_NOTINITIALIZED;

    try {
        std::unique_ptr<PhysicsSnapshot> result(new PhysicsSnapshot());
        result->environmentStaticIdentity=manager->GetEnvironment()->get_static_object();
        result->environmentStaticMaterial=result->environmentStaticIdentity->l_default_material;
        // Capture is observational: shifting the live IVP time origin changes
        // floating-point event/interpolation arithmetic and can break a TAS
        // even when the snapshot is never restored. Retain absolute timestamps.
        TraceSnapshotMindists(manager, "capture-begin");
        // Collision topology, caches and pending events are checkpoint state,
        // regardless of whether the native or x87 solver is active.
        {
            char error[256];
            result->collisionCheckpoint.reset(IVP_Collision_Checkpoint::capture(
                manager->GetEnvironment(), error, sizeof(error), CaptureExternalController, context));
            if (!result->collisionCheckpoint)
            {
                context->OutputToConsoleEx("Collision checkpoint capture failed: %s", error);
                return CKERR_INVALIDPARAMETER;
            }
            result->collisionCheckpoint->write_audit(stderr);
            if (std::getenv("BALLANCE_PREPARE_COLLISION_CANDIDATES")) {
                // Capture-time staging audit: these identities are still live.
                // Actual rewind must map through the saved host entity identity.
                auto identity = [](const IVP_Real_Object *object, void *) {
                    return const_cast<IVP_Real_Object *>(object);
                };
                auto *stats = manager->GetEnvironment()->get_statistic_manager();
                const int count = stats->sum_of_mindists;
                const int generated = stats->mindists_generated;
                const int deleted = stats->mindists_deleted;
                for (int attempt = 0; attempt < 3; ++attempt) {
                    auto *prepared = result->collisionCheckpoint->prepare_candidates(
                        manager->GetEnvironment(), identity, NULL, error, sizeof(error), PrepareExternalController, context);
                    if (!prepared) {
                        context->OutputToConsoleEx("Candidate preparation failed: %s", error);
                        return CKERR_INVALIDPARAMETER;
                    }
                    IVP_Collision_Checkpoint::destroy_prepared_candidates(prepared);
                    if (stats->sum_of_mindists != count || stats->mindists_generated != generated || stats->mindists_deleted != deleted) {
                        context->OutputToConsoleEx("Candidate staging mutated live mindist statistics");
                        return CKERR_INVALIDPARAMETER;
                    }
                }
                std::fprintf(stderr,"COLLISION_GRAPH_PREPARATION cycles=3 completed\n");
            }

        }

        manager->CaptureSimulationClock(result->clock);
        manager->CaptureSimulationStep(result->step);
        manager->GetGravity(result->gravity);
        result->timeFactor = manager->m_PhysicsTimeFactor;
        result->randomSeed = ivp_srand_read();
        // The manager table is authoritative.  Enumerating CK entities and
        // asking GetPhysicsObject() happened to work for most levels, but it
        // silently omits a table entry if its owning CK object is temporarily
        // outside the class cache while a transformation script is running.
        // Those transition states are precisely the ones a practice snapshot
        // must preserve.
        result->objects.reserve(manager->GetPhysicsObjectCount());
        int objectsWithoutRecipe = 0;
        int objectsWithMultipleRecipes = 0;
        int orphanedTableEntries = 0;

        for (PhysicsObjectTable::Iterator it = manager->m_PhysicsObjects.Begin();
             it != manager->m_PhysicsObjects.End(); ++it)
        {
            CKObject *owner = context->GetObject(it.GetKey());
            if (!owner || !CKIsChildClassOf(owner, CKCID_3DENTITY))
            {
                ++orphanedTableEntries;
                continue;
            }
            CK3dEntity *entity = static_cast<CK3dEntity *>(owner);
            PhysicsObject *physics = &*it;
            IVP_Real_Object *object = physics ? physics->m_RealObject : NULL;
            IVP_Core *core = object ? object->get_core() : NULL;
            if (!object || !core)
                continue;

            PhysicsObjectState state;
            state.entityId = entity->GetID();
            state.identity = entity;
            state.physicsIdentity = object;
            state.physicsLifetime = manager->GetPhysicsObjectLifetime(state.entityId);
            if (!state.physicsLifetime)
                throw std::runtime_error("unregistered physics object lifetime");
            state.materialIdentity = object->l_default_material;
            state.frictionCount = physics->m_FrictionCount;
            state.frictionTime = physics->m_FrictionTime;
            CKBehavior *physicalize = NULL;
            int physicalizeRecipeCount = 0;
            const XObjectPointerArray &allBehaviors =
                context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
            for (XObjectPointerArray::ConstIterator behaviorIt = allBehaviors.Begin();
                 behaviorIt != allBehaviors.End(); ++behaviorIt)
            {
                CKBehavior *candidate = static_cast<CKBehavior *>(*behaviorIt);
                if (candidate && candidate->GetPrototypeGuid() == kPhysicalizeGuid &&
                    candidate->GetTarget() == entity)
                {
                    if (!physicalize)
                        physicalize = candidate;
                    ++physicalizeRecipeCount;
                }
            }
            if (physicalize)
            {
                state.physicalizeBehaviorId = physicalize->GetID();
                state.physicalizeIdentity = physicalize;
            }
            else
            {
                ++objectsWithoutRecipe;
            }
            if (physicalizeRecipeCount > 1)
                ++objectsWithMultipleRecipes;
            object->get_quat_world_f_object_AT(&state.rotation, &state.position);
            state.speed = core->speed;
            state.rotationSpeed = core->rot_speed;
            state.speedChange = core->speed_change;
            state.rotationSpeedChange = core->rot_speed_change;
            state.temporarilyUnmovable = core->temporarily_unmovable;
            state.impactsSinceLastPsi = core->impacts_since_last_PSI;
            state.coreTimeOfLastPsi = core->time_of_last_psi;
            state.inverseDeltaTime = core->i_delta_time;
            state.positionLastPsi = core->pos_world_f_core_last_psi;
            state.positionDeltaPerSecond = core->delta_world_f_core_psis;
            state.rotationLastPsi = core->q_world_f_core_last_psi;
            state.rotationNextPsi = core->q_world_f_core_next_psi;
            state.matrixLastPsi = core->m_world_f_core_last_psi;
            state.rotationAxisWorld = core->rotation_axis_world_space;
            state.currentSpeed = core->current_speed;
            state.absoluteOmega = core->abs_omega;
            state.maxSurfaceRotationSpeed = core->max_surface_rot_speed;
            for (int reference = 0; reference < 2; ++reference)
            {
                state.calmReferenceTime[reference] =
                    core->time_of_calm_reference[reference];
                state.calmReferenceRotation[reference] =
                    core->q_world_f_core_calm_reference[reference];
                state.calmReferencePosition[reference] =
                    core->position_world_f_core_calm_reference[reference];
            }
            state.mindistEventAlreadyDone = core->mindist_event_already_done;
            state.movement = object->get_movement_state();
            state.collisionEnabled = object->is_collision_detection_enabled();
            if (PhysicsContactData *data = physics->m_ContactData)
            {
                state.hasContactData = TRUE;
                state.contactBehaviorId = data->m_BehaviorID;
                state.contactBehaviorIdentity = data->GetBehavior();
                state.contactDelayStart = data->m_TimeDelayStart;
                state.contactDelayEnd = data->m_TimeDelayEnd;
                state.contactOutputs.reserve(data->m_GroupOutputCount);
                for (int i = 0; i < data->m_GroupOutputCount; ++i)
                {
                    PhysicsObjectState::ContactOutputState output;
                    output.active = data->m_GroupOutputs[i].active;
                    output.number = data->m_GroupOutputs[i].number;
                    state.contactOutputs.push_back(output);
                }

                PhysicsContactManager *contacts = manager->GetContactManager();
                const IVP_Time now = manager->GetSimulationTime();
                for (int i = 0; contacts && i < contacts->GetRecordCount(); ++i)
                {
                    PhysicsContactRecord *record = contacts->GetRecord(i);
                    if (record && record->m_PhysicsObject == physics)
                    {
                        PhysicsObjectState::ContactRecordState savedRecord;
                        savedRecord.index = record->m_Index;
                        savedRecord.age = now - record->m_Time;
                        state.contactRecords.push_back(savedRecord);
                    }
                }
            }
            result->objects.push_back(state);
            if (std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
            {
                int frictionContacts = 0;
                for (IVP_Synapse_Friction *synapse =
                         object->get_first_friction_synapse();
                     synapse; synapse = synapse->get_next())
                    ++frictionContacts;
                context->OutputToConsoleEx(
                    "  physics object=%s id=%u pos=%.2f,%.2f,%.2f speed=%.3f,%.3f,%.3f recipe=%s ivpContacts=%d.",
                    entity->GetName() ? entity->GetName() : "<unnamed>",
                    static_cast<unsigned>(state.entityId),
                    static_cast<float>(state.position.k[0]),
                    static_cast<float>(state.position.k[1]),
                    static_cast<float>(state.position.k[2]),
                    state.speed.k[0], state.speed.k[1], state.speed.k[2],
                    physicalize && physicalize->GetName()
                        ? physicalize->GetName() : "<none>", frictionContacts);
            }
        }

        if (std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
        {
            context->OutputToConsoleEx(
                "Snapshot physics audit: %d/%d live/table objects, %d orphaned table entries, %d without a Physicalize recipe, %d with multiple recipes.",
                static_cast<int>(result->objects.size()),
                manager->GetPhysicsObjectCount(), orphanedTableEntries,
                objectsWithoutRecipe,
                objectsWithMultipleRecipes);
        }

        if (result->collisionCheckpoint) {
            // Capture manager-level ordering once, independently of table order.
            // The legacy restore branch still uses per-object ages; the exact
            // graph transaction will use these absolute timestamps and order.
            std::unordered_map<PhysicsObject *,HostObjectIdentity> tableOwners;
            std::unordered_map<IVP_Real_Object *,HostObjectIdentity> realOwners;
            std::unordered_map<IVP_Material *,bool> materials;
            for (const auto &state : result->objects) {
                auto *object = manager->GetPhysicsObject(state.identity,FALSE);
                if (!object || !object->m_RealObject) throw std::runtime_error("host object disappeared during capture");
                HostObjectIdentity owner{state.entityId,state.identity};
                tableOwners.emplace(object,owner); realOwners.emplace(object->m_RealObject,owner);
                if (object->m_RealObject->l_default_material) materials.emplace(object->m_RealObject->l_default_material,true);
                if (std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
                    std::fprintf(stderr,"PHYSICS_HOST_CONTACT entity=%u count=%d time=%a\n",unsigned(state.entityId),state.frictionCount,state.frictionTime.get_time());
            }
            auto *contactManager = manager->GetContactManager();
            for (int i = 0; contactManager && i < contactManager->GetRecordCount(); ++i) {
                const auto *record = contactManager->GetRecord(i);
                auto owner = record ? tableOwners.find(record->m_PhysicsObject) : tableOwners.end();
                if (owner == tableOwners.end()) throw std::runtime_error("contact record lacks live captured owner");
                const auto *data = record->m_PhysicsObject->m_ContactData;
                if (!data || record->m_Index < 0 || record->m_Index >= data->m_GroupOutputCount)
                    throw std::runtime_error("contact record output index invalid");
                result->orderedContacts.push_back({owner->second.entityId,owner->second.identity,record->m_Index,record->m_Time});
            }
            for (int i = 0; i < manager->m_MovableObjects.len(); ++i) {
                auto owner = realOwners.find(manager->m_MovableObjects.element_at(i));
                if (owner == realOwners.end()) throw std::runtime_error("movable list object lacks captured owner");
                result->movableOrder.push_back(owner->second);
            }
            for (int i = 0; i < manager->m_MaterialOwners.len(); ++i) {
                const auto *owned = manager->m_MaterialOwners.element_at(i);
                auto *owner = owned ? CK3dEntity::Cast(context->GetObject(owned->m_Owner)) : NULL;
                if (!owner || !owned->m_Material || !materials.count(owned->m_Material))
                    throw std::runtime_error("host material owner needs uncaptured material codec");
                result->materialOwners.push_back({owned->m_Owner,owner,owned->m_Material});
            }
            if(result->environmentStaticMaterial)materials.emplace(result->environmentStaticMaterial,true);
            for(const auto &entry:materials) {
                const auto lifetime=IVP_Material::get_lifetime(entry.first);
                if(!lifetime)throw std::runtime_error("unregistered material lifetime");
                result->materials.push_back({entry.first,lifetime,manager->OwnsMaterial(entry.first)});
            }
            std::fprintf(stderr,"PHYSICS_HOST_STATE objects=%zu contacts=%zu movable=%zu material_owners=%zu\n",result->objects.size(),result->orderedContacts.size(),result->movableOrder.size(),result->materialOwners.size());
        }

        const XObjectPointerArray &behaviors =
            context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
        for (XObjectPointerArray::ConstIterator it = behaviors.Begin();
             it != behaviors.End(); ++it)
        {
            CKBehavior *behavior = static_cast<CKBehavior *>(*it);
            if (!behavior || !IsRebuildablePhysicsBehavior(
                                 behavior->GetPrototypeGuid()) ||
                behavior->GetPrototypeGuid() == kContinuousContactGuid ||
                !HasInstalledPhysicsRuntime(behavior))
                continue;
            result->activeBehaviors.push_back(
                {behavior->GetID(), behavior, behavior->GetPrototypeGuid()});
            if (behavior->GetPrototypeGuid() == kCollisionDetectionGuid &&
                !CapturePhysicsCollisionDetection(manager, behavior, result->activeBehaviors.back().collision))
                throw std::runtime_error("collision listener checkpoint capture failed");
            if (std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
            {
                CKBeObject *target = behavior->GetTarget();
                context->OutputToConsoleEx(
                    "  active physics runtime=%s guid=%08x:%08x target=%s.",
                    behavior->GetName() ? behavior->GetName() : "<unnamed>",
                    static_cast<unsigned>(behavior->GetPrototypeGuid().d1),
                    static_cast<unsigned>(behavior->GetPrototypeGuid().d2),
                    target && target->GetName() ? target->GetName() : "<none>");
            }
        }

        // Capture registration order, not behavior enumeration order. Unknown
        // callbacks need an explicit owner/lifetime codec before we can restore.
        std::unordered_map<IVP_Listener_Collision *,PhysicsObjectState::ListenerOwner> detectionOwners;
        for (const auto &state : result->activeBehaviors) if (state.guid == kCollisionDetectionGuid) {
            auto *listener = GetPhysicsCollisionDetectionListener(state.identity);
            if (!listener || !detectionOwners.emplace(listener,PhysicsObjectState::ListenerOwner{
                    false,state.behaviorId,state.identity}).second)
                throw std::runtime_error("ambiguous collision detection listener owner");
        }
        auto *cluster = manager->GetEnvironment()->get_cluster_manager();
        size_t callbackCount = 0;
        for (auto &state : result->objects) {
            auto *host = manager->GetPhysicsObject(state.identity,FALSE);
            const int count = cluster->get_collision_listener_count(state.physicsIdentity);
            for (int i = 0; i < count; ++i) {
                auto *listener = cluster->get_collision_listener(state.physicsIdentity,i);
                auto owner = detectionOwners.find(listener);
                if (owner != detectionOwners.end()) state.listenerOrder.push_back(owner->second);
                else if (host && host->m_ContactData && host->m_ContactData->m_Listener == listener)
                    state.listenerOrder.push_back({true,0,nullptr});
                else throw std::runtime_error("collision callback lacks typed snapshot owner");
            }
            callbackCount += count;
        }
        std::fprintf(stderr,"PHYSICS_LISTENERS_CAPTURE ordered=%zu detection=%zu\n",callbackCount,detectionOwners.size());

        // A physics building block can have consumed its CK input already but
        // still be waiting for the next safe IVP boundary.  CK behavior/IO
        // state alone therefore cannot describe this queue.  Store semantic
        // operation types, owner identities, phases and vector order. Clone
        // dormant typed callbacks; never replay the BB to recreate a queue.
        CapturePendingCallbacks(manager->m_PreSimulateCallbacks, result.get(),0);
        CapturePendingCallbacks(manager->m_PostSimulateCallbacks, result.get(),1);
        if (std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
            context->OutputToConsoleEx(
                "Snapshot physics audit: %d installed runtimes, %d pending callbacks.",
                static_cast<int>(result->activeBehaviors.size()),
                static_cast<int>(result->pendingBehaviors.size()));

        TraceSnapshotMindists(manager, "capture-end");
        *snapshot = result.release();
        return CK_OK;
    }
    catch (const std::bad_alloc &)
    {
        return CKERR_OUTOFMEMORY;
    }
    catch (const std::exception &error)
    {
        context->OutputToConsoleEx("Physics snapshot capture failed: %s",error.what());
        return CKERR_INVALIDPARAMETER;
    }
    catch (...)
    {
        return CKERR_INVALIDPARAMETER;
    }
}

int TT_PhysicsRestoreSnapshot(CKContext *context, TT_PhysicsSnapshotHandle snapshot)
{
    CKIpionManager *manager = GetPhysicsManager(context);
    PhysicsSnapshot *saved = static_cast<PhysicsSnapshot *>(snapshot);
    if (!manager || !saved || !manager->GetEnvironment())
        return CKERR_INVALIDPARAMETER;

    // Reconcile physicalized bodies through their captured owners, then restore
    // the complete collision graph. Re-executing building blocks cannot recover
    // the saved contact/event queues and must never be a fallback for a rewind.
    return RestoreRecreatedGraph(context, manager, *saved);
}

void TT_PhysicsDestroySnapshot(TT_PhysicsSnapshotHandle snapshot)
{
    delete static_cast<PhysicsSnapshot *>(snapshot);
}

int TT_PhysicsAuditBeamEntity(CKContext *context, CK3dEntity *entity,
                              float x, float y, float z)
{
    CKIpionManager *manager = GetPhysicsManager(context);
    PhysicsObject *physics = manager && entity
        ? manager->GetPhysicsObject(entity, FALSE) : NULL;
    IVP_Real_Object *object = physics ? physics->m_RealObject : NULL;
    IVP_Core *core = object ? object->get_core() : NULL;
    if (!object || !core)
        return CKERR_INVALIDPARAMETER;

    IVP_U_Quat rotation;
    IVP_U_Point oldPosition;
    object->get_quat_world_f_object_AT(&rotation, &oldPosition);
    IVP_U_Point position(x, y, z);
    object->beam_object_to_new_position(&rotation, &position);
    core = object->get_core();
    core->speed.set(0.0f, 0.0f, 0.0f);
    core->rot_speed.set(0.0f, 0.0f, 0.0f);
    core->speed_change.set(0.0f, 0.0f, 0.0f);
    core->rot_speed_change.set(0.0f, 0.0f, 0.0f);
    CKIpionManager::UpdateObjectWorldMatrix(object);
    return CK_OK;
}
