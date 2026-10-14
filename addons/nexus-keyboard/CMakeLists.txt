# GW2 Keyboard for Nexus – the pure DLL version (docs/NEXUS_KEYBOARD_PLAN.md). The same core and services as the exe;
# the UI is ImGui (Nexus' 1.80, headers shared with addons/nexus).
set(GCT_NEXUS_TP ${PROJECT_SOURCE_DIR}/addons/nexus/third_party)
add_library(gw2keyboard_nexus SHARED
    src/kb_main.cpp
    src/voice_module.cpp
    ${PROJECT_SOURCE_DIR}/src/app/config.cpp
    ${PROJECT_SOURCE_DIR}/src/app/spell_service.cpp
    ${PROJECT_SOURCE_DIR}/src/win/spellcheck.cpp
    ${GCT_NEXUS_TP}/imgui/imgui.cpp
    ${GCT_NEXUS_TP}/imgui/imgui_draw.cpp
    ${GCT_NEXUS_TP}/imgui/imgui_tables.cpp
    ${GCT_NEXUS_TP}/imgui/imgui_widgets.cpp)
target_include_directories(gw2keyboard_nexus PRIVATE ${PROJECT_SOURCE_DIR}/src ${GCT_NEXUS_TP} ${GCT_NEXUS_TP}/imgui)
target_link_libraries(gw2keyboard_nexus PRIVATE gct_win)
target_compile_definitions(gw2keyboard_nexus PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX
                           WIN32_LEAN_AND_MEAN GCT_NEXUS_KEYBOARD)
target_compile_options(gw2keyboard_nexus PRIVATE /utf-8 /W4 /permissive-)
set_source_files_properties(${GCT_NEXUS_TP}/imgui/imgui.cpp ${GCT_NEXUS_TP}/imgui/imgui_draw.cpp
    ${GCT_NEXUS_TP}/imgui/imgui_tables.cpp ${GCT_NEXUS_TP}/imgui/imgui_widgets.cpp PROPERTIES COMPILE_OPTIONS "/W0")
# Static CRT, like the bridge: independent of the runtime the game has loaded.
set_target_properties(gw2keyboard_nexus PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" PREFIX "")

if(GCT_BUILD_TESTS)
    # The add-on outside the game: a real ImGui context, a stand-in Nexus API, frames drawn, a chat line, unloaded.
    add_executable(nexus_kb_selftest ${PROJECT_SOURCE_DIR}/tests/tools/nexus_kb_selftest.cpp
        ${GCT_NEXUS_TP}/imgui/imgui.cpp ${GCT_NEXUS_TP}/imgui/imgui_draw.cpp ${GCT_NEXUS_TP}/imgui/imgui_tables.cpp
        ${GCT_NEXUS_TP}/imgui/imgui_widgets.cpp)
    target_include_directories(nexus_kb_selftest PRIVATE ${GCT_NEXUS_TP} ${GCT_NEXUS_TP}/imgui)
    target_compile_definitions(nexus_kb_selftest PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(nexus_kb_selftest PRIVATE /utf-8)
    set_property(TARGET nexus_kb_selftest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(nexus_kb_selftest gw2keyboard_nexus)
endif()

if(GCT_WHISPER)
    # The voice helper (gw2kb_voice.exe): whisper outside the game process – its own process affinity on the least busy
    # cores and below normal priority, as in the exe. Lies next to the add-on's data: <addons>\GW2Keyboard\, its whisper
    # DLLs in <addons>\GW2Keyboard\voice.
    add_executable(gw2kb_voice WIN32 src/voice_helper_main.cpp ${PROJECT_SOURCE_DIR}/src/kbd/voice_session.cpp
                   ${PROJECT_SOURCE_DIR}/src/kbd/whisper_engine.cpp ${PROJECT_SOURCE_DIR}/src/app/config.cpp)
    target_include_directories(gw2kb_voice PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(gw2kb_voice PRIVATE gct_win whisper ggml delayimp)
    target_compile_definitions(gw2kb_voice PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX
                               GCT_HAVE_WHISPER=1)
    target_compile_options(gw2kb_voice PRIVATE /utf-8 /W4 /permissive-)
    target_link_options(gw2kb_voice PRIVATE /DELAYLOAD:whisper.dll /DELAYLOAD:ggml.dll /MANIFEST:NO)
    set_target_properties(gw2kb_voice PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
                          RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/GW2Keyboard)
    add_dependencies(gw2kb_voice ${GCT_GGML_CPU_TARGETS})
    add_custom_command(TARGET gw2kb_voice POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gw2kb_voice>/voice
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GCT_VOICE_DLLS} $<TARGET_FILE_DIR:gw2kb_voice>/voice)
endif()
