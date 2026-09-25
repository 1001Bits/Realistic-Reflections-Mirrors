cmake_minimum_required(VERSION 3.21)
project(MirrorsOfSkyrim VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The SKSE dependency guard requires MSVC x64 on Windows")
endif()

add_library(MirrorsOfSkyrimDependencyGuard SHARED src/Guard.cpp src/Plugin.cpp)
target_compile_features(MirrorsOfSkyrimDependencyGuard PRIVATE cxx_std_23)
target_compile_definitions(MirrorsOfSkyrimDependencyGuard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(MirrorsOfSkyrimDependencyGuard PRIVATE /W4 /WX /permissive- /EHsc /MP1)
target_link_libraries(MirrorsOfSkyrimDependencyGuard PRIVATE user32)
set_target_properties(MirrorsOfSkyrimDependencyGuard PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

install(TARGETS MirrorsOfSkyrimDependencyGuard RUNTIME DESTINATION SKSE/Plugins)
install(DIRECTORY scripts/Source/ DESTINATION Scripts/Source FILES_MATCHING PATTERN "*.psc")

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(MirrorsOfSkyrimDependencyGuardHost tests/MirrorsOfSkyrimDependencyGuardHost.cpp)
    target_compile_features(MirrorsOfSkyrimDependencyGuardHost PRIVATE cxx_std_23)
    target_compile_definitions(MirrorsOfSkyrimDependencyGuardHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(MirrorsOfSkyrimDependencyGuardHost PRIVATE /W4 /WX /permissive- /EHsc /MP1)
    add_dependencies(MirrorsOfSkyrimDependencyGuardHost MirrorsOfSkyrimDependencyGuard)
    add_test(NAME DependencyGuard
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mirrors_of_skyrim_dependency_guard.py"
            "$<TARGET_FILE:MirrorsOfSkyrimDependencyGuardHost>"
            "$<TARGET_FILE:MirrorsOfSkyrimDependencyGuard>"
            "${CMAKE_BINARY_DIR}/offline-tests")
    add_test(NAME PluginSource
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_plugin_source.py")
endif()
