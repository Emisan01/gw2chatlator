# GW2 Chat Bridge – the optional Nexus add-on (see docs/NEXUS_PLAN.md). Pinned, MIT-licensed third_party/ (Nexus API,
# Unofficial Extras API, Dear ImGui 1.80 = the version Nexus uses) and a few portable / proven files of ours.
add_library(gw2chat_bridge SHARED
    src/bridge_main.cpp
    src/extras_setup.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/src/core/bridge_protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/core/json.cpp
    ${PROJECT_SOURCE_DIR}/src/core/text.cpp
    ${PROJECT_SOURCE_DIR}/src/core/i18n.cpp
    ${PROJECT_SOURCE_DIR}/src/core/i18n_de.cpp
    ${PROJECT_SOURCE_DIR}/src/core/i18n_ar.cpp
    ${PROJECT_SOURCE_DIR}/src/win/http.cpp
    ${PROJECT_SOURCE_DIR}/src/win/extras_install.cpp
    ${PROJECT_SOURCE_DIR}/src/win/hash.cpp)
target_include_directories(gw2chat_bridge PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
target_compile_definitions(gw2chat_bridge PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX
                           WIN32_LEAN_AND_MEAN GCT_BRIDGE_DLL)
target_link_libraries(gw2chat_bridge PRIVATE winhttp bcrypt shell32)
# Static CRT: the add-on must not depend on which VC++ runtime the game process happens to have loaded.
target_compile_options(gw2chat_bridge PRIVATE /utf-8 /W4 /permissive-)
set_source_files_properties(third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp PROPERTIES COMPILE_OPTIONS "/W0")
set_target_properties(gw2chat_bridge PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    PREFIX "")

if(GCT_BUILD_TESTS)
    # The whole chain without the game: the DLL with a stand-in Nexus API → the real pipe of our programs.
    add_executable(bridge_selftest ${PROJECT_SOURCE_DIR}/tests/tools/bridge_selftest.cpp)
    target_include_directories(bridge_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(bridge_selftest PRIVATE gct_win)
    target_compile_definitions(bridge_selftest PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
    target_compile_options(bridge_selftest PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET bridge_selftest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(bridge_selftest gw2chat_bridge)
endif()
