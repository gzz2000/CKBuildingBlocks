/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		           Physics Force
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include "CKIpionManager.h"
#include "PhysicsForceCheckpoint.h"
#include "ivp_collision_checkpoint.hxx"
#ifdef IVP_EMULATE_X87
#include "ivu_x87_mode.hxx"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

CKObjectDeclaration *FillBehaviorPhysicsForceDecl();
CKERROR CreatePhysicsForceProto(CKBehaviorPrototype **pproto);
int PhysicsForce(const CKBehaviorContext &behcontext);
CKERROR PhysicsForceCallBack(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorPhysicsForceDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("SetPhysicsForce");
    od->SetDescription("Physics Force");
    od->SetCategory("Physics");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x56e20c57, 0xb926068));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010000);
    od->SetCreationFunction(CreatePhysicsForceProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}
CKERROR CreatePhysicsForceProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("SetPhysicsForce");
    if (!proto)
        return CKERR_OUTOFMEMORY;

    proto->DeclareInput("Create");
    proto->DeclareInput("Shutdown");

    proto->DeclareOutput("Out1");
    proto->DeclareOutput("Out2");

    proto->DeclareInParameter("Position", CKPGUID_VECTOR, "0,0,0");
    proto->DeclareInParameter("Pos Referential", CKPGUID_3DENTITY);
    proto->DeclareInParameter("Direction", CKPGUID_VECTOR, "0,0,1");
    proto->DeclareInParameter("Direction Ref", CKPGUID_3DENTITY);
    proto->DeclareInParameter("Force Value", CKPGUID_FLOAT, "10");

    proto->DeclareLocalParameter("IVP_Handle", CKPGUID_POINTER);

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(PhysicsForce);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);
    proto->SetBehaviorCallbackFct(PhysicsForceCallBack);

    *pproto = proto;
    return CK_OK;
}

#define POSITION 0
#define REFERENTIAL 1
#define DIRECTION 2
#define DIRECTION_REF 3
#define FORCE_VALUE 4

class PhysicsControllerForce : public IVP_Controller_Independent
{
public:
    struct Detached {};
    explicit PhysicsControllerForce(Detached) : m_Core(NULL) {}
    PhysicsControllerForce(IVP_Real_Object *obj, const IVP_U_Point &pos, const IVP_U_Point &force)
    {
        m_Core = obj->get_core();
        m_Position = pos;
        m_Force = force;

        if (m_Core)
            IVP_Controller_Manager::add_controller_to_core(this, m_Core);
    }

    ~PhysicsControllerForce()
    {
        if (m_Core)
            IVP_Controller_Manager::remove_controller_from_core(this, m_Core);
    }

    void core_is_going_to_be_deleted_event(IVP_Core *my_core)
    {
        if (my_core == m_Core)
        {
            my_core->rem_core_controller(this);
            m_Core = NULL;
        }
    }

    void do_simulation_controller(IVP_Event_Sim *es, IVP_U_Vector<IVP_Core> *core_list)
    {
        if (core_list && core_list->len() != 0)
        {
            IVP_U_Matrix mat;
            m_Core->calc_at_matrix(m_Core->get_environment()->get_current_time(), &mat);

            const IVP_U_Point *worldForce = &m_Force;
#ifdef IVP_EMULATE_X87
            IVP_U_Point preciseForce;
            if (ivp_x87_is_enabled())
            {
                // Original 0x10004B0E stores the controller force as floats;
                // 0x100046F1 widens those stored values before inverse rotation.
                // Retain our member/layout so live solver switches and snapshots
                // keep the fast-mode value, but reproduce the store at use time.
                volatile float stored[3] = {
                    static_cast<float>(m_Force.k[0]),
                    static_cast<float>(m_Force.k[1]),
                    static_cast<float>(m_Force.k[2])};
                preciseForce.set(stored[0], stored[1], stored[2]);
                worldForce = &preciseForce;
            }
#endif
            IVP_U_Point out;
            mat.vimult3(worldForce, &out);

            if (std::getenv("BALLANCE_TRACE_FORCE_INPUT"))
            {
                const char *name = std::getenv("BALLANCE_TRACE_CORE_OBJECT");
                double begin = 0.0, end = 2.0;
                if (const char *range = std::getenv("BALLANCE_TRACE_DYNAMICS_RANGE"))
                    std::sscanf(range, "%lf,%lf", &begin, &end);
                const double time = m_Core->get_environment()->get_current_time().get_time();
                bool selected = false;
                for (int i = 0; name && i < m_Core->objects.len(); ++i)
                {
                    const char *object_name = m_Core->objects.element_at(i)->get_name();
                    if (object_name && !std::strcmp(object_name, name)) selected = true;
                }
                if (selected && time >= begin && time <= end)
                {
                    std::fprintf(stderr, "FORCE_INPUT time=%a", time);
                    for (int i = 0; i < 3; ++i)
                        std::fprintf(stderr, " p%d=%a f%d=%a", i, m_Position.k[i], i, m_Force.k[i]);
                    std::fprintf(stderr, "\nFORCE_TRANSFORM time=%a", time);
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            std::fprintf(stderr, " m%d%d=%a", r, c, mat.get_elem(r, c));
                    for (int i = 0; i < 3; ++i)
                        std::fprintf(stderr, " v%d=%a out%d=%a", i, worldForce->k[i], i, out.k[i]);
                    std::fputc('\n', stderr);
                }
            }

            IVP_U_Float_Point pos(m_Position.k[0], m_Position.k[1], m_Position.k[2]);
            IVP_U_Float_Point fcs(out.k[0], out.k[1], out.k[2]);
            IVP_U_Float_Point fws(m_Force.k[0], m_Force.k[1], m_Force.k[2]);
            m_Core->async_push_core(&pos, &fcs, &fws);
        }
    }

    IVP_CONTROLLER_PRIORITY get_controller_priority() { return IVP_CP_ACTUATOR; };

    IVP_Core *m_Core;
    IVP_U_Point m_Position;
    IVP_U_Point m_Force;
};

bool TT_TransferPhysicsForceControllers(IVP_Controller *oldController, IVP_Controller *newController,
                                      const IVP_Checkpoint_Controller_Payload &payload, bool commit)
{
    auto *oldForce = dynamic_cast<PhysicsControllerForce *>(oldController);
    auto *newForce = dynamic_cast<PhysicsControllerForce *>(newController);
    if ((!oldController && !newController) || oldController==newController ||
        (oldController && !oldForce) || (newController && !newForce) ||
        payload.schema!="CKBB.PhysicsForce/v1" || payload.cores.size()!=1 || !payload.cores[0] ||
        payload.scalar_bytes.size()!=6*sizeof(IVP_DOUBLE) || (newForce && newForce->m_Core)) return false;
    if (commit) {
        if (oldForce) oldForce->m_Core=NULL;
        if (newForce) newForce->m_Core=payload.cores[0];
    }
    return true;
}

bool TT_CapturePhysicsForceController(IVP_Controller *controller,
                                     IVP_Checkpoint_Controller_Payload &payload)
{
    auto *force = dynamic_cast<PhysicsControllerForce *>(controller);
    if (!force) return false;
    payload = IVP_Checkpoint_Controller_Payload();
    payload.schema = "CKBB.PhysicsForce/v1";
    // Six native IVP_DOUBLE scalars, position then force. Keep the exact stored
    // representation; precise-mode float spills happen at simulation use time.
    payload.scalar_bytes.resize(sizeof(force->m_Position.k) + sizeof(force->m_Force.k));
    std::memcpy(payload.scalar_bytes.data(), force->m_Position.k, sizeof(force->m_Position.k));
    std::memcpy(payload.scalar_bytes.data() + sizeof(force->m_Position.k),
                force->m_Force.k, sizeof(force->m_Force.k));
    payload.cores.push_back(force->m_Core);
    return true;
}

IVP_Controller *TT_PreparePhysicsForceController(const IVP_Checkpoint_Controller_Payload &payload)
{
    if (payload.schema != "CKBB.PhysicsForce/v1" || payload.cores.size() != 1 ||
        !payload.cores[0] || payload.scalar_bytes.size() != 6 * sizeof(IVP_DOUBLE))
        return NULL;
    auto *force = new PhysicsControllerForce(PhysicsControllerForce::Detached());
    std::memcpy(force->m_Position.k, payload.scalar_bytes.data(), sizeof(force->m_Position.k));
    std::memcpy(force->m_Force.k, payload.scalar_bytes.data() + sizeof(force->m_Position.k), sizeof(force->m_Force.k));
    // The plan retains the proposed core separately. Destructor cannot remove
    // a live controller while this null-core object remains unpublished.
    return force;
}

class PhysicsForceCallback : public PhysicsCallback
{
public:
    PhysicsForceCallback(CKIpionManager *man, CKBehavior *beh) : PhysicsCallback(man, beh, 2) {}

    PhysicsCallback *CloneForSnapshot() const override
    {
        // This callback has no payload beyond manager/type/behavior identity.
        return new PhysicsForceCallback(*this);
    }

    virtual int Execute()
    {
        CKBehavior *beh = GetBehavior();
        if (!beh)
            return 1;

        CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
        if (!ent)
            return 1;

        VxVector position;
        beh->GetInputParameterValue(POSITION, &position);

        CK3dEntity *referential = (CK3dEntity *)beh->GetInputParameterObject(REFERENTIAL);

        VxVector direction;
        beh->GetInputParameterValue(DIRECTION, &direction);

        CK3dEntity *directionRef = (CK3dEntity *)beh->GetInputParameterObject(DIRECTION_REF);

        float forceValue = 10.0f;
        beh->GetInputParameterValue(FORCE_VALUE, &forceValue);

        PhysicsObject *po = m_IpionManager->GetPhysicsObject(ent);
        if (!po)
            return 0;

        IVP_Real_Object *obj = po->m_RealObject;

        VxVector vec;
        if (directionRef)
            directionRef->TransformVector(&vec, &direction);
        else
            vec = direction;

        IVP_U_Point force(vec.x, vec.y, vec.z);
        if (force.quad_length() <= 0.0001)
            force.set(1.0, 0.0, 0.0);
        else
            force.normize();
        if (std::getenv("BALLANCE_TRACE_FORCE_BUILD"))
        {
            const char *selected = std::getenv("BALLANCE_TRACE_FORCE_BUILD_OBJECT");
            if (!selected) selected = std::getenv("BALLANCE_TRACE_CORE_OBJECT");
            double begin = 0.0, end = 2.0;
            if (const char *range = std::getenv("BALLANCE_TRACE_DYNAMICS_RANGE"))
                std::sscanf(range, "%lf,%lf", &begin, &end);
            const double time = obj->get_core()->get_environment()->get_current_time().get_time();
            if (selected && ent->GetName() && (!std::strcmp(selected, "*") || !std::strcmp(selected, ent->GetName())) && time >= begin && time <= end)
            {
                std::fprintf(stderr, "FORCE_DIRECTION time=%a target=%s input=(%a %a %a) value=%a ref=%s\n",
                             time, ent->GetName(), direction.x, direction.y, direction.z, forceValue,
                             directionRef && directionRef->GetName() ? directionRef->GetName() : "<none>");
                if (directionRef) {
                    const VxMatrix &matrix = directionRef->GetWorldMatrix();
                    std::fprintf(stderr, "FORCE_REFERENCE time=%a", time);
                    for (int r=0;r<4;++r) for(int c=0;c<4;++c)
                        std::fprintf(stderr, " m%d%d=%a",r,c,matrix[r][c]);
                    std::fputc('\n',stderr);
                }
                std::fprintf(stderr, "FORCE_NORM time=%a in=(%a %a %a) out=(%a %a %a) value=%a\n",
                             time, vec.x, vec.y, vec.z, force.k[0], force.k[1], force.k[2], forceValue);
            }
        }
        force.mult(forceValue);

        IVP_U_Point pos(position.x, position.y, position.z);
        if (referential != ent)
        {
            VxVector p;
            if (referential)
                referential->Transform(&p, &position);
            else
                p = position;

            IVP_U_Point p1(p.x, p.y, p.z);
            IVP_U_Matrix mat;
            obj->get_core()->calc_at_matrix(obj->get_environment()->get_current_time(), &mat);
            mat.vimult4(&p1, &pos);
        }

        PhysicsControllerForce *controller = new PhysicsControllerForce(obj, pos, force);
        beh->SetLocalParameterValue(0, &controller);

        return 1;
    }
};

int PhysicsForce(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    PhysicsControllerForce *controller = NULL;
    beh->GetLocalParameterValue(0, &controller);

    if (beh->IsInputActive(0))
    {
        if (!controller)
        {
            CK3dEntity *ent = (CK3dEntity *)beh->GetTarget();
            if (!ent)
                return CKBR_OWNERERROR;

            CKIpionManager *man = CKIpionManager::GetManager(context);
            if (!man || !man->GetEnvironment() || !man->m_PreSimulateCallbacks)
                return CKBR_GENERICERROR;

            PhysicsForceCallback *cb = new PhysicsForceCallback(man, beh);
            man->m_PreSimulateCallbacks->Process(cb);
        }

        beh->ActivateInput(0, FALSE);
        beh->ActivateOutput(0, TRUE);
    }
    else
    {
        if (controller)
        {
            delete controller;
            controller = NULL;
            beh->SetLocalParameterValue(0, &controller);
        }

        beh->ActivateInput(1, FALSE);
        beh->ActivateOutput(1, TRUE);
    }

    return CKBR_OK;
}

CKERROR PhysicsForceCallBack(const CKBehaviorContext &behcontext)
{
    if (behcontext.CallbackMessage == CKM_BEHAVIORRESET ||
        behcontext.CallbackMessage == CKM_BEHAVIORDELETE ||
        behcontext.CallbackMessage == CKM_BEHAVIORDETACH)
    {
        CKBehavior *beh = behcontext.Behavior;
        PhysicsControllerForce *controller = NULL;
        beh->GetLocalParameterValue(0, &controller);

        CKIpionManager *man = CKIpionManager::GetManager(behcontext.Context);
        if (controller && man && man->GetEnvironment())
            delete controller;

        controller = NULL;
        beh->SetLocalParameterValue(0, &controller);
    }

    return CKBR_OK;
}
