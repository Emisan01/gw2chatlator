# GW2 Chat Bridge – the optional Nexus add-on (see docs/NEXUS_PLAN.md). Only the pinned, MIT-licensed headers in
# third_party/ and two portable core files: no ImGui, no other dependency.
add_library(gw2chat_bridge SHARED
    src/bridge_main.cpp
    ${PROJECT_SOURCE_DIR}/src/core/bridge_protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/core/json.cpp)
target_include_directories(gw2chat_bridge PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_definitions(gw2chat_bridge PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX
                           WIN32_LEAN_AND_MEAN)
# Static CRT: the add-on must not depend on which VC++ runtime the game process happens to have loaded.
target_compile_options(gw2chat_bridge PRIVATE /utf-8 /W4 /permissive-)
set_target_properties(gw2chat_bridge PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    PREFIX "")
