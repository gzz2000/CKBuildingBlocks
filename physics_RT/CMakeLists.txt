# physics_RT

add_subdirectory(ivp)

set(PHYSICS_RT_IVP_LIBRARIES ivp_controller ivp_compact_builder)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    set(PHYSICS_RT_IVP_LIBRARIES
            "$<LINK_GROUP:RESCAN,ivp_controller,ivp_compact_builder,ivp_physics,ivp_surface_manager,ivp_utility,ivp_collision>"
    )
endif ()



aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/Behaviors BEHAVIORS_SRCS)
ckbb_add_module(physics_RT
        SOURCES physics_RT.cpp
                CKIpionManager.cpp
                CKIpionManager.h
                PhysicsCallback.cpp
                PhysicsCallback.h
                PhysicsContact.cpp
                PhysicsContact.h
                PhysicsObjectApi.cpp
                PhysicsRuntimeApi.cpp
                PhysicsRuntimeApi.h
                PhysicsPreload.cpp
                PhysicsSnapshot.cpp
                PhysicsSnapshot.h
                ${BEHAVIORS_SRCS}
        LINKS ${PHYSICS_RT_IVP_LIBRARIES}
)

if (NOT CKBB_USE_EXTERNAL_VIRTOOLS_SDK)
    foreach (_target IN ITEMS physics_RT physics_RTStatic)
        if (TARGET ${_target})
            target_compile_definitions(${_target} PRIVATE CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND=1)
        endif ()
    endforeach ()
endif ()

# Keep a linker map for deterministic-physics comparison builds.  The retail
# module is stripped, so the map from the reconstructed x86 build is the most
# reliable way to carry source-level IVP names into binary-level comparisons.
if (WIN32 AND BALLANCE_PHYSICS_LINK_MAP)
    target_link_options(physics_RT PRIVATE "/MAP:${CMAKE_BINARY_DIR}/physics_RT.map")
endif ()

if (CKBB_BUILD_TESTS AND BUILD_TESTING AND TARGET CK2_3DStatic)
    add_executable(physics_rt_regression_tests
            tests/test_physics_regressions.cpp
            CKIpionManager.cpp
            PhysicsCallback.cpp
            PhysicsContact.cpp
    )
    target_compile_features(physics_rt_regression_tests PRIVATE cxx_std_17)
    target_compile_definitions(physics_rt_regression_tests PRIVATE
            CKBB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    target_include_directories(physics_rt_regression_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(physics_rt_regression_tests PRIVATE
            CK2_3DStatic
            ${PHYSICS_RT_IVP_LIBRARIES}
    )
    set_target_properties(physics_rt_regression_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME physics_rt_regression_tests
            COMMAND $<TARGET_FILE:physics_rt_regression_tests>)
    set_tests_properties(physics_rt_regression_tests PROPERTIES TIMEOUT 60)
endif ()

# Exercise the exported preload operation against a live CK/IVP world. This
# target also works in the shared-library configuration used by native Player.
if (CKBB_BUILD_TESTS AND BUILD_TESTING AND TARGET physics_RT AND TARGET CK2_3D AND
    NOT CKBB_USE_EXTERNAL_VIRTOOLS_SDK AND NOT WIN32)
    add_executable(physics_preload_api_tests tests/test_physics_preload.cpp)
    target_compile_features(physics_preload_api_tests PRIVATE cxx_std_17)
    target_include_directories(physics_preload_api_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(physics_preload_api_tests PRIVATE
            physics_RT CK2 VxMath ${PHYSICS_RT_IVP_LIBRARIES})
    add_dependencies(physics_preload_api_tests CK2_3D)
    add_test(NAME physics_preload_api_tests
            COMMAND physics_preload_api_tests $<TARGET_FILE:CK2_3D>)
    set_tests_properties(physics_preload_api_tests PROPERTIES TIMEOUT 60)
    add_executable(physics_listener_snapshot_tests tests/test_listener_snapshot.cpp)
    target_compile_features(physics_listener_snapshot_tests PRIVATE cxx_std_17)
    target_include_directories(physics_listener_snapshot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(physics_listener_snapshot_tests PRIVATE
            physics_RT CK2 VxMath ${PHYSICS_RT_IVP_LIBRARIES})
    add_dependencies(physics_listener_snapshot_tests CK2_3D)
    add_test(NAME physics_listener_snapshot_tests
            COMMAND physics_listener_snapshot_tests $<TARGET_FILE:CK2_3D> $<TARGET_FILE:physics_RT>)
    set_tests_properties(physics_listener_snapshot_tests PROPERTIES TIMEOUT 60)
    add_executable(physics_cache_snapshot_tests tests/test_cache_snapshot.cpp)
    target_compile_features(physics_cache_snapshot_tests PRIVATE cxx_std_17)
    target_include_directories(physics_cache_snapshot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(physics_cache_snapshot_tests PRIVATE
            physics_RT CK2 VxMath ${PHYSICS_RT_IVP_LIBRARIES})
    add_dependencies(physics_cache_snapshot_tests CK2_3D)
    add_test(NAME physics_cache_snapshot_tests
            COMMAND physics_cache_snapshot_tests $<TARGET_FILE:CK2_3D> $<TARGET_FILE:physics_RT>)
    set_tests_properties(physics_cache_snapshot_tests PROPERTIES TIMEOUT 60)
    add_executable(physics_material_snapshot_tests tests/test_material_snapshot.cpp)
    target_compile_features(physics_material_snapshot_tests PRIVATE cxx_std_17)
    target_include_directories(physics_material_snapshot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(physics_material_snapshot_tests PRIVATE
            physics_RT CK2 VxMath ${PHYSICS_RT_IVP_LIBRARIES})
    add_dependencies(physics_material_snapshot_tests CK2_3D)
    add_test(NAME physics_material_snapshot_tests
            COMMAND physics_material_snapshot_tests $<TARGET_FILE:CK2_3D> $<TARGET_FILE:physics_RT>)
    set_tests_properties(physics_material_snapshot_tests PROPERTIES TIMEOUT 60)


endif ()
