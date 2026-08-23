cmake_minimum_required(VERSION 3.21)

project(lan-chat-source-launcher LANGUAGES CXX)

add_executable(LANChat-Launcher WIN32 launcher.cpp)
target_compile_features(LANChat-Launcher PRIVATE cxx_std_20)
target_compile_definitions(LANChat-Launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(LANChat-Launcher PRIVATE shell32)

if(MSVC)
    # The source launcher is distributed as one small, self-contained EXE.
    set_property(TARGET LANChat-Launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()
add_test(
    NAME source-launcher-dry-run
    COMMAND LANChat-Launcher --dry-run --root ${CMAKE_SOURCE_DIR}/../..
)
set_tests_properties(source-launcher-dry-run PROPERTIES PASS_REGULAR_EXPRESSION "bootstrap-github.ps1")
