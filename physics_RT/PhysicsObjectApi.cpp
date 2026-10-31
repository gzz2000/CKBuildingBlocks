#include "CKIpionManager.h"

#include "VxMatrix.h"
#include "VxQuaternion.h"

namespace {
void ConvertMatrix(const IVP_U_Matrix &source, VxMatrix &target)
{
    target[0][0] = static_cast<float>(source.get_elem(0, 0));
    target[0][1] = static_cast<float>(source.get_elem(1, 0));
    target[0][2] = static_cast<float>(source.get_elem(2, 0));
    target[0][3] = 0.0f;
    target[1][0] = static_cast<float>(source.get_elem(0, 1));
    target[1][1] = static_cast<float>(source.get_elem(1, 1));
    target[1][2] = static_cast<float>(source.get_elem(2, 1));
    target[1][3] = 0.0f;
    target[2][0] = static_cast<float>(source.get_elem(0, 2));
    target[2][1] = static_cast<float>(source.get_elem(1, 2));
    target[2][2] = static_cast<float>(source.get_elem(2, 2));
    target[2][3] = 0.0f;
    target[3][0] = static_cast<float>(source.vv.k[0]);
    target[3][1] = static_cast<float>(source.vv.k[1]);
    target[3][2] = static_cast<float>(source.vv.k[2]);
    target[3][3] = 1.0f;
}
}

const char *PhysicsObject::GetName() const
{
    return m_RealObject ? m_RealObject->get_name() : nullptr;
}

CK3dEntity *PhysicsObject::GetEntity() const
{
    return m_RealObject ? static_cast<CK3dEntity *>(m_RealObject->client_data) : nullptr;
}

void PhysicsObject::Wake()
{
    if (m_RealObject)
        m_RealObject->ensure_in_simulation();
}

bool PhysicsObject::IsStatic() const
{
    return m_RealObject && m_RealObject->get_core()->physical_unmoveable;
}

void PhysicsObject::EnableCollisions(bool enable)
{
    if (m_RealObject)
        m_RealObject->enable_collision_detection(enable ? IVP_TRUE : IVP_FALSE);
}

float PhysicsObject::GetMass() const
{
    return m_RealObject ? m_RealObject->get_core()->get_mass() : 0.0f;
}

float PhysicsObject::GetInvMass() const
{
    return m_RealObject ? m_RealObject->get_core()->get_inv_mass() : 0.0f;
}

void PhysicsObject::GetInertia(VxVector &inertia) const
{
    inertia.Set(0.0f, 0.0f, 0.0f);
    if (m_RealObject) {
        const IVP_U_Float_Point *value = m_RealObject->get_core()->get_rot_inertia();
        inertia.Set(value->k[0], value->k[1], value->k[2]);
    }
}

void PhysicsObject::GetInvInertia(VxVector &inertia) const
{
    inertia.Set(0.0f, 0.0f, 0.0f);
    if (m_RealObject) {
        const IVP_U_Float_Point *value = m_RealObject->get_core()->get_inv_rot_inertia();
        inertia.Set(value->k[0], value->k[1], value->k[2]);
    }
}

void PhysicsObject::GetDamping(float *speed, float *rotation) const
{
    if (!m_RealObject) {
        if (speed) *speed = 0.0f;
        if (rotation) *rotation = 0.0f;
        return;
    }
    IVP_Core *core = m_RealObject->get_core();
    if (speed) *speed = core->speed_damp_factor;
    if (rotation) *rotation = core->rot_speed_damp_factor.k[0];
}

void PhysicsObject::GetPosition(VxVector *worldPosition, VxVector *angles) const
{
    if (!m_RealObject) {
        if (worldPosition) worldPosition->Set(0.0f, 0.0f, 0.0f);
        if (angles) angles->Set(0.0f, 0.0f, 0.0f);
        return;
    }
    IVP_U_Matrix source;
    m_RealObject->get_m_world_f_object_AT(&source);
    if (worldPosition)
        worldPosition->Set(source.vv.k[0], source.vv.k[1], source.vv.k[2]);
    if (angles) {
        VxMatrix matrix;
        ConvertMatrix(source, matrix);
        VxQuaternion quaternion;
        quaternion.FromMatrix(matrix);
        quaternion.ToEulerAngles(&angles->x, &angles->y, &angles->z);
    }
}

void PhysicsObject::GetPositionMatrix(VxMatrix &positionMatrix) const
{
    if (!m_RealObject) {
        positionMatrix.Identity();
        return;
    }
    IVP_U_Matrix source;
    m_RealObject->get_m_world_f_object_AT(&source);
    ConvertMatrix(source, positionMatrix);
}

void PhysicsObject::GetVelocity(VxVector *velocity, VxVector *angularVelocity) const
{
    if (!m_RealObject) {
        if (velocity) velocity->Set(0.0f, 0.0f, 0.0f);
        if (angularVelocity) angularVelocity->Set(0.0f, 0.0f, 0.0f);
        return;
    }
    IVP_Core *core = m_RealObject->get_core();
    if (velocity)
        velocity->Set(core->speed.k[0] + core->speed_change.k[0],
                      core->speed.k[1] + core->speed_change.k[1],
                      core->speed.k[2] + core->speed_change.k[2]);
    if (angularVelocity)
        angularVelocity->Set(core->rot_speed.k[0] + core->rot_speed_change.k[0],
                             core->rot_speed.k[1] + core->rot_speed_change.k[1],
                             core->rot_speed.k[2] + core->rot_speed_change.k[2]);
}

void PhysicsObject::SetVelocity(const VxVector *velocity, const VxVector *angularVelocity)
{
    if (!m_RealObject)
        return;
    m_RealObject->ensure_in_simulation();
    IVP_Core *core = m_RealObject->get_core();
    if (velocity) {
        core->speed.set_to_zero();
        core->speed_change.set(velocity->x, velocity->y, velocity->z);
    }
    if (angularVelocity) {
        core->rot_speed.set_to_zero();
        core->rot_speed_change.set(angularVelocity->x, angularVelocity->y, angularVelocity->z);
    }
}
