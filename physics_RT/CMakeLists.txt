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
                PhysicsSnapshot.cpp
                PhysicsSnapshot.h
                ${BEHAVIORS_SRCS}
        LINKS ${PHYSICS_RT_IVP_LIBRARIES}
)

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
