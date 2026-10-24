cmake_minimum_required(VERSION 3.20)
project(GW2KeyboardNexus VERSION 2.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The version lives here only: version.h for the code (the add-on's definition in Nexus, the options window).
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)
include_directories(${CMAKE_BINARY_DIR}/generated)

# ---- core: portable logic, no Win32 (text, JSON, chat lines and codes, word model, protocols, i18n)
add_library(gct_core STATIC
    src/core/text.cpp
    src/core/json.cpp
    src/core/hotkey.cpp
    src/core/langs.cpp
    src/core/languages.cpp
    src/core/glossary.cpp
    src/core/protect.cpp
    src/core/slang.cpp
    src/core/cloud_mt_protocol.cpp
    src/core/local_mt_protocol.cpp
    src/core/typo_memory.cpp
    src/core/my_words.cpp
    src/core/snippets.cpp
    src/core/send_route.cpp
    src/core/translate_policy.cpp
    src/core/translation_cache.cpp
    src/core/deepl_protocol.cpp
    src/core/mymemory_protocol.cpp
    src/core/llm_protocol.cpp
    src/core/chat_line.cpp
    src/core/chat_code.cpp
    src/core/names.cpp
    src/core/gw2_text.cpp
    src/core/mumble.cpp
    src/core/i18n.cpp
    src/core/i18n_de.cpp
    src/core/i18n_fr.cpp
    src/core/i18n_es.cpp
    src/core/i18n_it.cpp
    src/core/i18n_pl.cpp
    src/core/i18n_pt.cpp
    src/core/i18n_tr.cpp
    src/core/i18n_ru.cpp
    src/core/word_model.cpp
    src/core/languagetool_protocol.cpp
    src/core/voice.cpp
    src/core/voice_learn.cpp
    src/core/send_guard.cpp
    src/core/contrast.cpp
    src/core/chat_source.cpp
    src/core/window_layout.cpp
    src/core/soft_wrap.cpp
    src/core/translation_orchestrator.cpp
)
target_include_directories(gct_core PUBLIC src)

option(GCT_BUILD_TESTS "Build the core unit tests and the add-on's self-test" ON)
if(GCT_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE gct_core)
    add_test(NAME core_tests COMMAND core_tests)
endif()

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "GW2 Keyboard for Nexus needs MSVC x64 – only the portable core and its tests are built.")
    return()
endif()

# ---- win: Windows services without UI (translators, sending, MumbleLink, microphone, word prediction, downloads).
# spellcheck.cpp is linked per target.
add_library(gct_win STATIC
    src/win/files.cpp
    src/win/extras_install.cpp
    src/win/local_mt_packages.cpp
    src/win/local_mt_translator.cpp
    src/win/http.cpp
    src/win/deepl_translator.cpp
    src/win/online_translators.cpp
    src/win/els.cpp
    src/win/gw2_sender.cpp
    src/win/mumble_link.cpp
    src/win/keyboard_layout.cpp
    src/win/secret.cpp
    src/win/hash.cpp
    src/win/cpu_cores.cpp
    src/win/mic_sessions.cpp
    src/win/audio_capture.cpp
    src/win/voice_models.cpp
    src/win/text_prediction.cpp)
target_link_libraries(gct_win PUBLIC gct_core winhttp ole32 user32 shell32 advapi32 version uuid crypt32 bcrypt runtimeobject)

# ---- Voice input (open source): whisper.cpp (MIT) on the CPU, in the helper gw2kb_voice.exe. Built as DLLs that go
# to <addons>\GW2Keyboard\voice and are delay-loaded. One CPU backend DLL per instruction set (ggml-cpu-*), the best is
# chosen at runtime. Static CRT and no OpenMP, so the DLLs need no VC++ redistributable.
option(GCT_VOICE "Build the voice input (downloads whisper.cpp)" ON)
if(GCT_VOICE)
    enable_language(C)
    include(FetchContent)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)  # our settings below win over whisper's option() defaults
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)  # CMAKE_MSVC_RUNTIME_LIBRARY is honoured inside whisper
    set(GCT_SAVED_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS ON)  # GGML_BACKEND_DL needs DLLs; our own libraries are STATIC explicitly
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(WHISPER_BUILD_TESTS OFF)
    set(WHISPER_BUILD_EXAMPLES OFF)
    set(WHISPER_BUILD_SERVER OFF)
    set(WHISPER_BUILD_IS_DEV OFF)
    set(GGML_NATIVE OFF)
    set(GGML_OPENMP OFF)
    set(GGML_CCACHE OFF)
    set(GGML_BACKEND_DL ON)
    set(GGML_CPU_ALL_VARIANTS ON)
    FetchContent_Declare(whisper
        URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.5.tar.gz
        URL_HASH SHA256=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL)  # only what the voice helper needs is built (not the bundled parakeet library)
    FetchContent_MakeAvailable(whisper)
    set(BUILD_SHARED_LIBS ${GCT_SAVED_SHARED_LIBS})
    unset(CMAKE_MSVC_RUNTIME_LIBRARY)
    # Every CPU backend variant that exists for this compiler (MODULE libraries, loaded by ggml at runtime).
    set(GCT_GGML_CPU_TARGETS)
    foreach(v x64 sse42 sandybridge haswell skylakex cannonlake cascadelake icelake cooperlake zen4 alderlake
              sapphirerapids)
        if(TARGET ggml-cpu-${v})
            list(APPEND GCT_GGML_CPU_TARGETS ggml-cpu-${v})
        endif()
    endforeach()
    if(NOT GCT_GGML_CPU_TARGETS)
        message(FATAL_ERROR "whisper.cpp: no ggml-cpu-* backend target found")
    endif()
    set(GCT_WHISPER ON)
    set(GCT_VOICE_DLLS $<TARGET_FILE:whisper> $<TARGET_FILE:ggml> $<TARGET_FILE:ggml-base>)
    foreach(c ${GCT_GGML_CPU_TARGETS})
        list(APPEND GCT_VOICE_DLLS $<TARGET_FILE:${c}>)
    endforeach()
endif()

# ---- tools that measure the typing help and the recognizer on this PC
set(GCT_TOOL_TARGETS)
if(GCT_BUILD_TESTS)
    # A simulated player types invented chat messages with the real spell checker (key presses saved, surprises).
    add_executable(typing_bench tests/tools/typing_bench.cpp src/app/spell_service.cpp src/win/spellcheck.cpp)
    target_link_libraries(typing_bench PRIVATE gct_win)
    list(APPEND GCT_TOOL_TARGETS typing_bench)
    if(GCT_WHISPER)
        # The voice recognizer on 16 kHz WAV recordings (local\voice, never committed).
        add_executable(voice_bench tests/tools/voice_bench.cpp src/kbd/whisper_engine.cpp)
        target_link_libraries(voice_bench PRIVATE gct_win whisper ggml delayimp)
        target_compile_definitions(voice_bench PRIVATE GCT_HAVE_WHISPER=1)
        target_link_options(voice_bench PRIVATE /DELAYLOAD:whisper.dll /DELAYLOAD:ggml.dll)
        add_dependencies(voice_bench ${GCT_GGML_CPU_TARGETS})
        add_custom_command(TARGET voice_bench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:voice_bench>/voice
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GCT_VOICE_DLLS} $<TARGET_FILE_DIR:voice_bench>/voice)
        list(APPEND GCT_TOOL_TARGETS voice_bench)
    endif()
endif()

# Sources are UTF-8; static CRT (no VC++ redistributable needed).
foreach(t gct_core gct_win ${GCT_TOOL_TARGETS})
    target_compile_definitions(${t} PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
    target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W4 /permissive->)
endforeach()
set_property(TARGET gct_core gct_win ${GCT_TOOL_TARGETS} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(GCT_BUILD_TESTS)
    target_compile_options(core_tests PRIVATE /utf-8)
    set_property(TARGET core_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- GW2 Keyboard for Nexus: the add-on, its voice helper, its self-test
add_subdirectory(addons/nexus-keyboard)

if(GCT_BUILD_TESTS)
    add_executable(local_mt_fake_helper tests/tools/local_mt_fake_helper.cpp)
    target_include_directories(local_mt_fake_helper PRIVATE src)
    target_link_libraries(local_mt_fake_helper PRIVATE gct_core)
    target_compile_definitions(local_mt_fake_helper PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
                               NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(local_mt_fake_helper PRIVATE /utf-8 /W4 /permissive- /EHsc)
    set_property(TARGET local_mt_fake_helper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(local_mt_translator_selftest tests/tools/local_mt_translator_selftest.cpp)
    target_include_directories(local_mt_translator_selftest PRIVATE src)
    target_link_libraries(local_mt_translator_selftest PRIVATE gct_win)
    target_compile_definitions(local_mt_translator_selftest PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
                               NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(local_mt_translator_selftest PRIVATE /utf-8 /W4 /permissive- /EHsc)
    set_property(TARGET local_mt_translator_selftest PROPERTY MSVC_RUNTIME_LIBRARY
                 "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(local_mt_translator_selftest local_mt_fake_helper)
    add_test(NAME local_mt_translator_selftest
             COMMAND local_mt_translator_selftest $<TARGET_FILE:local_mt_fake_helper>)
endif()

# The native local translation helper is optional: normal add-on builds don't fetch its much larger ML toolchain.
# Release builders enable GCT_LOCAL_MT and package the resulting gw2kb_translate.exe separately for click-to-install.
option(GCT_LOCAL_MT "Build the optional native CTranslate2 + OPUS-MT helper" OFF)
if(GCT_LOCAL_MT)
    include(FetchContent)
    # Keep the self-contained helper and every static inference dependency on the same CRT.
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_MKL OFF CACHE BOOL "" FORCE)
    set(WITH_DNNL ON CACHE BOOL "" FORCE)
    set(OPENMP_RUNTIME NONE CACHE STRING "" FORCE)
    set(ONEDNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ONEDNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ONEDNN_BUILD_GRAPH OFF CACHE BOOL "" FORCE)
    set(DNNL_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(DNNL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(DNNL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DNNL_BUILD_FOR_CI OFF CACHE BOOL "" FORCE)
    set(DNNL_ENABLE_WORKLOAD INFERENCE CACHE STRING "" FORCE)
    set(DNNL_ENABLE_PRIMITIVE_GPU_ISA "" CACHE STRING "" FORCE)
    set(ONEDNN_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    set(ONEDNN_CPU_RUNTIME SEQ CACHE STRING "" FORCE)
    FetchContent_Declare(onednn
        GIT_REPOSITORY https://github.com/uxlfoundation/oneDNN.git
        GIT_TAG v3.9.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(onednn)
    set(DNNL_INCLUDE_DIR "${onednn_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(DNNL_LIBRARY dnnl CACHE STRING "" FORCE)
    FetchContent_Declare(ctranslate2
        GIT_REPOSITORY https://github.com/OpenNMT/CTranslate2.git
        GIT_TAG v4.8.3 GIT_SHALLOW TRUE GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(ctranslate2)
    FetchContent_Declare(sentencepiece
        GIT_REPOSITORY https://github.com/google/sentencepiece.git
        GIT_TAG v0.2.0 GIT_SHALLOW TRUE)
    set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(SPM_BUILD_TRAINING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sentencepiece)

    if(MSVC)
        foreach(target dnnl dnnl_common dnnl_cpu dnnl_cpu_x64 ctranslate2 sentencepiece-static)
            if(TARGET ${target})
                set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
            endif()
        endforeach()
    endif()

    add_executable(gw2kb_translate addons/nexus-keyboard/src/translate_helper_main.cpp)
    target_link_libraries(gw2kb_translate PRIVATE gct_core gct_win ctranslate2 dnnl sentencepiece-static)
    target_include_directories(gw2kb_translate PRIVATE src "${sentencepiece_SOURCE_DIR}/src")
    target_compile_definitions(gw2kb_translate PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
                               NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(gw2kb_translate PRIVATE /utf-8 /W4 /permissive- /EHsc)
    set_target_properties(gw2kb_translate PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
                          RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/addons/nexus-keyboard/$<CONFIG>/GW2Keyboard)
endif()
