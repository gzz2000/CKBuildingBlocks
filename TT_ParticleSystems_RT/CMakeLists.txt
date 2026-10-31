# TT_ParticleSystems_RT

ckbb_add_module(TT_ParticleSystems_RT
        SOURCES TT_ParticleSystems_RT.cpp
                ParticleGuids.h
                Particle.h
                Particle.cpp
                ParticleSnapshot.h
                ParticleSnapshot.cpp
                ParticleTools.h
                ParticleTools.cpp
                ParticleManager.h
                ParticleManager.cpp
                ParticleEmitter.h
                ParticleEmitter.cpp
                PointEmitter.h
                PointEmitter.cpp
                LineEmitter.h
                LineEmitter.cpp
                PlanarEmitter.h
                PlanarEmitter.cpp
                CubicEmitter.h
                CubicEmitter.cpp
                DiscEmitter.h
                DiscEmitter.cpp
                ObjectEmitter.h
                ObjectEmitter.cpp
                CurveEmitter.h
                CurveEmitter.cpp
                CylindricalEmitter.h
                CylindricalEmitter.cpp
                SphericalEmitter.h
                SphericalEmitter.cpp
                TimePointEmitter.h
                TimePointEmitter.cpp
                WaveEmitter.h
                WaveEmitter.cpp
                ParticleSystemRenderCallbacks.h
                ParticleSystemRenderCallbacks.cpp
                ParticleSystemDeflectors.cpp
                ParticleSystemInteractors.cpp
                ParticleSystemMeshes.cpp
                GeneralParticleSystem.h
                GeneralParticleSystem.cpp
                PointParticleSystem.cpp
                LinearParticleSystem.cpp
                PlanarParticleSystem.cpp
                CubicParticleSystem.cpp
                DiscParticleSystem.cpp
                ObjectParticleSystem.cpp
                CurveParticleSystem.cpp
                CylindricalParticleSystem.cpp
                SphericalParticleSystem.cpp
                TimeDependentPointParticleSystem.cpp
                WaveParticleSystem.cpp
)

# The fixture uses the real renderer entity class without creating a window.
if (CKBB_BUILD_TESTS AND BUILD_TESTING AND NOT WIN32
        AND TARGET TT_ParticleSystems_RT AND TARGET CK2_3D)
    add_executable(test_particle_snapshot tests/test_particle_snapshot.cpp)
    target_include_directories(test_particle_snapshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_snapshot PRIVATE cxx_std_17)
    target_link_libraries(test_particle_snapshot PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_snapshot COMMAND test_particle_snapshot)
    add_executable(test_particle_render_snapshot tests/test_particle_render_snapshot.cpp)
    target_include_directories(test_particle_render_snapshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_render_snapshot PRIVATE cxx_std_17)
    target_link_libraries(test_particle_render_snapshot PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_render_snapshot COMMAND test_particle_render_snapshot)
    add_executable(test_particle_cache_snapshot tests/test_particle_cache_snapshot.cpp)
    target_include_directories(test_particle_cache_snapshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_cache_snapshot PRIVATE cxx_std_17)
    target_link_libraries(test_particle_cache_snapshot PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_cache_snapshot COMMAND test_particle_cache_snapshot)
    add_executable(test_particle_resources tests/test_particle_resources.cpp)
    target_include_directories(test_particle_resources PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_resources PRIVATE cxx_std_17)
    target_link_libraries(test_particle_resources PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_resources COMMAND test_particle_resources)
    add_executable(test_particle_manager_lifetime tests/test_particle_manager_lifetime.cpp)
    target_include_directories(test_particle_manager_lifetime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_manager_lifetime PRIVATE cxx_std_17)
    target_link_libraries(test_particle_manager_lifetime PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_manager_lifetime COMMAND test_particle_manager_lifetime)
    add_executable(test_particle_pool tests/test_particle_pool.cpp)
    target_include_directories(test_particle_pool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(test_particle_pool PRIVATE cxx_std_17)
    target_link_libraries(test_particle_pool PRIVATE TT_ParticleSystems_RT CK2_3D CK2 VxMath)
    add_test(NAME test_particle_pool COMMAND test_particle_pool)
endif ()
