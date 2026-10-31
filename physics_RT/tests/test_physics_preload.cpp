#include "CKIpionManager.h"
#include "CK3dEntity.h"
#include "CKGlobals.h"
#include "CKPluginManager.h"
#include "PhysicsRuntimeApi.h"
#include "ivp_sim_unit.hxx"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

struct Fixture {
    CKContext *context = nullptr;
    CKIpionManager *manager = nullptr;
    IVP_Material_Simple material{0.4f, 0.5f};
    std::array<CK3dEntity *, 2> owners{};
    explicit Fixture(const char *group = "preload") {
        Require(CKCreateContext(&context, nullptr, 0, 0) == CK_OK, "create context");
        manager = new CKIpionManager(context);
        Require(manager->OnCKInit() == CK_OK, "initialize manager");
        manager->CreateEnvironment();
        for (unsigned i = 0; i < owners.size(); ++i) {
            owners[i] = static_cast<CK3dEntity *>(context->CreateObject(CKCID_3DOBJECT));
            Require(owners[i] != nullptr, "create owner");
            Require(manager->CreatePhysicsObjectOnParameters(owners[i], 0, nullptr,
                0, nullptr, nullptr, 0, nullptr, 1.0f, nullptr, nullptr, FALSE,
                &material, 1.0f, const_cast<char *>(group), FALSE, FALSE, TRUE,
                0.1f, 0.1f) == CK_OK, "create isolated freefall body");
        }
        // ensure_in_simulation queues revival for the next PSI. The API is
        // called on live units at retirement, after this normal engine step.
        auto *env = manager->GetEnvironment();
        env->simulate_dtime(env->get_delta_PSI_time());
    }
    ~Fixture() { if (context) CKCloseContext(context); }
    IVP_Real_Object *Object(unsigned i) { return manager->GetPhysicsObject(owners[i])->m_RealObject; }
    TT_PhysicsPreloadResultV1 Complete(bool expected, int status) {
        TT_PhysicsPreloadResultV1 result{};
        result.size = sizeof(result);
        const bool success = TT_PhysicsCompleteFreefallPreloadV1(manager, owners.data(), owners.size(), &result) != 0;
        if (success != expected || result.status != status)
            std::fprintf(stderr, "completion success=%d status=%d steps=%u expected=%d/%d bodies=%d movable=%d\n",
                success, result.status, result.steps, expected, status,
                manager->GetPhysicsObjectCount(), manager->m_MovableObjects.len());
        Require(success == expected,
                "unexpected completion result");
        Require(result.status == status, "unexpected completion status");
        return result;
    }
};

struct Observation {
    double time;
    int seed, countdown, objects;
    std::array<TT_PhysicsCoreState, 2> cores{};
    explicit Observation(Fixture &f)
        : time(f.manager->GetEnvironment()->get_current_time().get_time()),
          seed(TT_PhysicsGetRandomSeed()), countdown(f.manager->GetNextMovementCheck()),
          objects(f.manager->GetPhysicsObjectCount()) {
        for (unsigned i = 0; i < cores.size(); ++i)
            Require(TT_PhysicsGetCoreState(f.Object(i), &cores[i]) != 0, "observe core");
    }
    void Unchanged(Fixture &f) const {
        Observation after(f);
        Require(time == after.time && seed == after.seed && countdown == after.countdown && objects == after.objects,
                "preflight failure or completed preload changed timing/RNG/topology");
        Require(std::memcmp(cores.data(), after.cores.data(), sizeof(cores)) == 0,
                "preflight failure or completed preload changed core state");
    }
};

void RejectsInvalidOwners() {
    Fixture f;
    Observation before(f);
    Require(TT_PhysicsCompleteFreefallPreloadV1(f.manager, f.owners.data(), 2, nullptr) == 0,
            "accepted null result");
    TT_PhysicsPreloadResultV1 invalid{};
    Require(TT_PhysicsCompleteFreefallPreloadV1(f.manager, f.owners.data(), 2, &invalid) == 0,
            "accepted invalid result size");
    invalid.size = sizeof(invalid);
    Require(TT_PhysicsCompleteFreefallPreloadV1(f.manager, f.owners.data(), 0, &invalid) == 0 && invalid.status == 1,
            "accepted empty owner set");
    before.Unchanged(f);
    auto *second = f.owners[1];
    f.owners[1] = f.owners[0];
    Require(f.Complete(false, 2).steps == 0, "duplicate owner advanced world");
    f.owners[1] = second;
    before.Unchanged(f);
    // Arbitrary foreign identity must be rejected without dereferencing it.
    f.owners[1] = reinterpret_cast<CK3dEntity *>(uintptr_t(1));
    Require(f.Complete(false, 2).steps == 0, "foreign owner advanced world");
    f.owners[1] = second;
    before.Unchanged(f);
}

void RejectsUnsupportedDynamics() {
    Fixture f;
    auto *core = f.Object(0)->get_core();
    core->speed_change.k[0] = 1.0f;
    Observation force(f);
    Require(f.Complete(false, 3).steps == 0, "pending force advanced world");
    force.Unchanged(f);
    core->speed_change.k[0] = 0.0f;
    core->speed_damp_factor = std::numeric_limits<float>::quiet_NaN();
    Observation damping(f);
    Require(f.Complete(false, 3).steps == 0, "invalid damping advanced world");
    damping.Unchanged(f);
}

void RejectsPossibleContacts() {
    Fixture f("");
    Observation before(f);
    Require(f.Complete(false, 3).steps == 0, "collision-enabled pair advanced world");
    before.Unchanged(f);
}

void CompletesActualWorldAndIsIdempotent() {
    Fixture f;
    const auto result = f.Complete(true, 0);
    Require(result.steps > 0 && result.timeAfter > result.timeBefore, "did not advance actual physics");
    for (unsigned i = 0; i < f.owners.size(); ++i) {
        auto *core = f.Object(i)->get_core();
        Require(core->sim_unit_of_core->sim_unit_has_fast_objects, "unit did not complete freefall");
        Require(core->speed.k[1] < 0.0f, "gravity did not advance body");
    }
    Observation before(f);
    Require(f.Complete(true, 0).steps == 0, "completed world advanced again");
    before.Unchanged(f);
    // A second context must execute its own initialization, not inherit a
    // process-global 'already attempted' flag from the first one.
    Fixture other;
    Require(other.Complete(true, 0).steps > 0, "second context skipped initialization");
}
}

int main(int argc, char **argv) {
    try {
        Require(CKStartUp() == CK_OK, "engine startup");
        Require(argc == 2 && CKGetPluginManager()->RegisterPlugin(argv[1]) == CK_OK,
                "register engine 3D object classes");
        for (int mode : {0, 1}) {
#ifndef IVP_EMULATE_X87
            if (mode == 1) continue;
#endif
            Require(TT_PhysicsSetSolverMode(mode) != 0, "set solver mode");
            RejectsInvalidOwners();
            RejectsUnsupportedDynamics();
            RejectsPossibleContacts();
            CompletesActualWorldAndIsIdempotent();
            std::printf("Preload runtime API checks passed in solver mode %d\n", mode);
        }
        CKShutdown();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Preload runtime API test failed: %s\n", error.what());
        return 1;
    }
}
