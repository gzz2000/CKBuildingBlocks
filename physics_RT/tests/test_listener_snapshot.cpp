#include "CKIpionManager.h"
#include "CK3dEntity.h"
#include "CKGlobals.h"
#include "CKPluginManager.h"
#include "PhysicsCollisionDetectionCheckpoint.h"
#include "PhysicsRuntimeApi.h"
#include "PhysicsSnapshot.h"
#include "ivp_physic_private.hxx"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    CKContext *context = nullptr;
    CKIpionManager *manager = nullptr;
    IVP_Material_Simple material{0.4f, 0.5f};
    std::array<CK3dEntity *, 2> bodies{};
    CKBehavior *listener = nullptr;
    TT_PhysicsSnapshotHandle saved = nullptr;
    PhysicsCollisionDetectionState expected{};
    TT_PhysicsCoreState core{};
    IVP_Real_Object *originalBody = nullptr;

    Fixture() {
        Require(CKCreateContext(&context, nullptr, 0, 0) == CK_OK, "create context");
        manager = CKIpionManager::GetManager(context);
        Require(manager != nullptr, "physics plugin manager");
        manager->CreateEnvironment();
        for (auto &body : bodies) {
            body = static_cast<CK3dEntity *>(context->CreateObject(CKCID_3DOBJECT));
            Require(body != nullptr, "create entity");
            Require(manager->CreatePhysicsObjectOnParameters(body, 0, nullptr,
                0, nullptr, nullptr, 0, nullptr, 1.0f, nullptr, nullptr, FALSE,
                &material, 1.0f, const_cast<char *>("test"), FALSE, FALSE, TRUE,
                0.1f, 0.1f) == CK_OK, "create body");
        }
        manager->GetEnvironment()->simulate_dtime(manager->GetEnvironment()->get_delta_PSI_time());
        listener = NewListener();
        expected.body = bodies[0];
        expected.bodyId = bodies[0]->GetID();
        expected.lastCollisionTime = 0.125;
        expected.sleepAfterwards = 0.25f;
        expected.minSpeed = 0.5f;
        expected.maxSpeed = 8.0f;
        expected.collisionId = 3;
        Require(RestorePhysicsCollisionDetection(manager, listener, expected, true), "install listener");
        originalBody = manager->GetPhysicsObject(bodies[0])->m_RealObject;
        Require(TT_PhysicsGetCoreState(originalBody, &core) != 0, "capture core");
        Require(TT_PhysicsCaptureSnapshot(context, &saved) == CK_OK, "capture snapshot");
    }
    ~Fixture() {
        TT_PhysicsDestroySnapshot(saved);
        if (context) CKCloseContext(context);
    }
    CKBehavior *NewListener() {
        auto *behavior = static_cast<CKBehavior *>(context->CreateObject(CKCID_BEHAVIOR));
        Require(behavior && behavior->InitFromGuid(CKGUID(0x7435488d, 0x201d1188)) == CK_OK,
                "create collision behavior");
        return behavior;
    }
    void ResetListener() { listener->CallCallbackFunction(CKM_BEHAVIORRESET); }
    void Restore() {
        Require(TT_PhysicsRestoreSnapshot(context, saved) == CK_OK, "restore host listener lifecycle");
        Require(manager->GetPhysicsObject(bodies[0])->m_RealObject == originalBody,
                "unnecessarily replaced surviving body");
        TT_PhysicsCoreState actualCore{};
        Require(TT_PhysicsGetCoreState(originalBody, &actualCore) != 0 &&
                std::memcmp(&core, &actualCore, sizeof(core)) == 0, "core state changed");
        PhysicsCollisionDetectionState actual{};
        Require(CapturePhysicsCollisionDetection(manager, listener, actual), "capture restored listener");
        Require(actual.body == expected.body && actual.bodyId == expected.bodyId &&
                actual.lastCollisionTime == expected.lastCollisionTime &&
                actual.sleepAfterwards == expected.sleepAfterwards &&
                actual.minSpeed == expected.minSpeed && actual.maxSpeed == expected.maxSpeed &&
                actual.collisionId == expected.collisionId, "listener state differs");
        auto *cluster = manager->GetEnvironment()->get_cluster_manager();
        Require(cluster->get_collision_listener_count(originalBody) == 1 &&
                cluster->get_collision_listener(originalBody, 0) == GetPhysicsCollisionDetectionListener(listener),
                "listener registration differs");
    }
};
void LifecycleRoundTrips() {
    Fixture f;
    f.manager->GetEnvironment()->simulate_dtime(0.05f);
    f.ResetListener();
    f.Restore();
    f.Restore();
    f.ResetListener();
    auto moved = f.expected;
    moved.body = f.bodies[1];
    moved.bodyId = moved.body->GetID();
    Require(RestorePhysicsCollisionDetection(f.manager, f.listener, moved, true), "reattach listener elsewhere");
    f.Restore();
    auto *extra = f.NewListener();
    Require(RestorePhysicsCollisionDetection(f.manager, extra, f.expected, true), "add extra listener");
    f.Restore();
    Require(GetPhysicsCollisionDetectionListener(extra) == nullptr, "extra listener survived rewind");
    f.Restore();
}
}
int main(int argc, char **argv) {
    try {
        Require(CKStartUp() == CK_OK, "startup");
        Require(argc == 3 && CKGetPluginManager()->RegisterPlugin(argv[1]) == CK_OK &&
                CKGetPluginManager()->RegisterPlugin(argv[2]) == CK_OK, "register 3D/physics plugins");
        for (int mode : {0, 1}) {
#ifndef IVP_EMULATE_X87
            if (mode == 1) continue;
#endif
            Require(TT_PhysicsSetSolverMode(mode) != 0, "set solver mode");
            LifecycleRoundTrips();
            std::printf("Listener snapshot lifecycle passed in solver mode %d\n", mode);
        }
        CKShutdown();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Listener snapshot test failed: %s\n", error.what());
        return 1;
    }
}
