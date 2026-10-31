# TT_Toolbox_RT

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/Behaviors BEHAVIORS_SRCS)
ckbb_add_module(TT_Toolbox_RT
        SOURCES TT_Toolbox_RT.cpp
                ToolboxGuids.h
                TimeManager.h
                TimeManager.cpp
                DebugManager.h
                DebugManager.cpp
                DXManager.h
                DXManager.cpp
                ${BEHAVIORS_SRCS}
)

if (NOT CKBB_USE_EXTERNAL_VIRTOOLS_SDK)
    foreach (_target IN ITEMS TT_Toolbox_RT TT_Toolbox_RTStatic)
        if (TARGET ${_target})
            target_compile_definitions(${_target} PRIVATE CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND=1)
        endif ()
    endforeach ()
endif ()

if (CKBB_BUILD_TESTS AND BUILD_TESTING AND NOT WIN32 AND NOT CKBB_USE_EXTERNAL_VIRTOOLS_SDK)
    add_executable(test_toolbox_clock tests/TimeManagerClockTest.cpp)
    target_compile_features(test_toolbox_clock PRIVATE cxx_std_17)
    target_link_libraries(test_toolbox_clock PRIVATE CK2 VxMath)
    add_test(NAME test_toolbox_clock COMMAND test_toolbox_clock)
endif ()
