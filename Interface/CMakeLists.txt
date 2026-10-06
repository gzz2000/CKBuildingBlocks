# Interface
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/Behaviors BEHAVIORS_SRCS)
ckbb_add_module(Interface
        SOURCES
        CKBitmapFont.cpp
        CKBitmapFont.h
        CKFontManager.cpp
        CKFontManager.h
        CKTextureFont.cpp
        CKTextureFont.h
        CompiledData.cpp
        CompiledData.h
        DrawingPrimitives.cpp
        DrawingPrimitives.h
        Layout.h
        Interface.cpp
        Interface.rc
        ${BEHAVIORS_SRCS}
)

if (NOT WIN32)
    if (TARGET Interface)
        target_sources(Interface PRIVATE CKStbFontBackend.cpp CKStbFontBackend.h)
        target_link_libraries(Interface PRIVATE stb)
    endif ()
    if (TARGET InterfaceStatic)
        target_sources(InterfaceStatic PRIVATE CKStbFontBackend.cpp CKStbFontBackend.h)
        target_link_libraries(InterfaceStatic PRIVATE stb)
    endif ()
endif ()

if (CKBB_BUILD_TESTS AND BUILD_TESTING AND NOT WIN32)
    include(CTest)
    enable_testing()
    add_executable(test_interface_stb_font_backend
            tests/test_stb_font_backend.cpp
            CKStbFontBackend.cpp
            CKStbFontBackend.h
    )
    target_include_directories(test_interface_stb_font_backend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_interface_stb_font_backend PRIVATE stb VxMath)
    target_compile_features(test_interface_stb_font_backend PRIVATE cxx_std_17)
    target_compile_definitions(test_interface_stb_font_backend PRIVATE
            TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts"
    )
    add_test(NAME test_interface_stb_font_backend COMMAND test_interface_stb_font_backend)
endif ()
