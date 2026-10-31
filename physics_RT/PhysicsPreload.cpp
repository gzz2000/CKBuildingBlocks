#include "PhysicsRuntimeApi.h"
#include "CKIpionManager.h"
#include <ivp_controller.hxx>
#include <ivp_sim_unit.hxx>
#include <ivp_collision_filter.hxx>
#include <cmath>
#include <vector>

int TT_PhysicsCompleteFreefallPreloadV1(CKBaseManager *manager,
    CK3dEntity *const *owners, unsigned int count, TT_PhysicsPreloadResultV1 *result)
{
    if (!result || result->size != sizeof(*result)) return 0;
    *result = {sizeof(*result), 1};
    if (!manager || manager->GetGuid() != TT_PHYSICS_MANAGER_GUID ||
        !owners || !count || count > 4096) return 0;
    auto *physics = static_cast<CKIpionManager *>(manager);
    auto *env = physics->GetEnvironment();
    if (!env) return 0;
    result->objects = count;
    result->timeBefore = result->timeAfter = env->get_current_time().get_time();
    result->seedBefore = result->seedAfter = ivp_srand_read();
    result->countdownBefore = result->countdownAfter = physics->GetNextMovementCheck();
    auto fail = [&](int status) { result->status = status; return 0; };
    if (physics->GetPhysicsObjectCount() != int(count) ||
        physics->m_MovableObjects.len() != int(count)) return fail(2);
    std::vector<IVP_Real_Object *> objects;
    objects.reserve(count);
    // Resolve owners from the manager's live table without dereferencing a
    // caller-supplied owner until its identity has been established there.
    for (unsigned i = 0; i < count; ++i) {
        if (!owners[i]) return fail(2);
        for (unsigned j = 0; j < i; ++j) if (owners[j] == owners[i]) return fail(2);
        IVP_Real_Object *object = nullptr;
        for (auto it = physics->m_PhysicsObjects.Begin(); it != physics->m_PhysicsObjects.End(); ++it) {
            auto *candidate = (*it).m_RealObject;
            if (candidate && candidate->client_data == owners[i]) { object = candidate; break; }
        }
        if (!object || physics->m_MovableObjects.index_of(object) < 0) return fail(2);
        objects.push_back(object);
        auto *core = object->get_core();
        auto *unit = core->sim_unit_of_core;
        if (!unit || core->objects.len() != 1 || unit->sim_unit_cores.len() != 1 ||
            unit->controller_cores.len() != 1 ||
            unit->controller_cores.element_at(0)->l_controller != env->get_gravity_controller() ||
            object->get_first_friction_synapse()) return fail(3);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(core->speed.k[axis]) || !std::isfinite(core->rot_speed.k[axis]) ||
                core->speed_change.k[axis] != 0 || core->rot_speed_change.k[axis] != 0)
                return fail(3);
        }
        if (!std::isfinite(core->speed_damp_factor) || core->speed_damp_factor < 0) return fail(3);
    }
    const double psi = env->get_delta_PSI_time();
    const auto *gravity = env->get_gravity();
    const double g2 = gravity->k[0]*gravity->k[0] + gravity->k[1]*gravity->k[1] + gravity->k[2]*gravity->k[2];
    if (!std::isfinite(psi) || psi <= 0 || !std::isfinite(g2) || g2 <= 0) return fail(3);
    // Do not advance an unrelated world or allow contacts to merge these units.
    auto *filter = env->get_collision_filter();
    if (!filter) return fail(3);
    for (unsigned i = 0; i < count; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (filter->check_objects_for_collision_detection(objects[i], objects[j])) return fail(3);

    // State-based completion, with a failure bound rather than a fitted number
    // of warm-up steps. The original preload's freefall units cease consuming
    // movement-check RNG when the existing engine fast-object flag becomes set.
    for (;;) {
        bool complete = true;
        if (physics->GetPhysicsObjectCount() != int(count) || physics->m_MovableObjects.len() != int(count)) return fail(4);
        for (auto *object : objects) {
            if (physics->m_MovableObjects.index_of(object) < 0) return fail(4);
            auto *unit = object->get_core()->sim_unit_of_core;
            if (!unit || unit->sim_unit_cores.len() != 1) return fail(4);
            complete = complete && unit->sim_unit_has_fast_objects;
        }
        if (complete) { result->status = 0; return 1; }
        if (result->steps == 1024) return fail(5);
        env->simulate_dtime(psi);
        ++result->steps;
        result->timeAfter = env->get_current_time().get_time();
        result->seedAfter = ivp_srand_read();
        result->countdownAfter = physics->GetNextMovementCheck();
    }
}
