# TT_Gravity_RT

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/Behaviors BEHAVIORS_SRCS)
ckbb_add_module(TT_Gravity_RT
        SOURCES ExtraSnapshot.h SimpleShadowSnapshot.h TT_Gravity_RT.cpp ${BEHAVIORS_SRCS}
)

# Include the behavior implementation in this fixture so allocator faults
# exercise its real list ownership without replacing the game's allocator.
if (CKBB_BUILD_TESTS AND BUILD_TESTING AND NOT WIN32 AND TARGET CK2_3D)
    add_executable(test_extra_snapshot tests/ExtraSnapshotTest.cpp)
    target_compile_features(test_extra_snapshot PRIVATE cxx_std_17)
    target_link_libraries(test_extra_snapshot PRIVATE CK2_3D CK2 VxMath)
    foreach (mode IN ITEMS normal missing allocation prepared stale empty)
        add_test(NAME test_extra_snapshot_${mode} COMMAND test_extra_snapshot ${mode})
    endforeach ()
endif ()

if (NOT CKBB_USE_EXTERNAL_VIRTOOLS_SDK)
    foreach (_target IN ITEMS TT_Gravity_RT TT_Gravity_RTStatic)
        if (TARGET ${_target})
            target_compile_definitions(${_target} PRIVATE CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND=1)
        endif ()
    endforeach ()
endif ()
