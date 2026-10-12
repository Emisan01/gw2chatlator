cmake_minimum_required(VERSION 3.20)
project(GW2ChatTranslator VERSION 1.3.1 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The version lives here only: version.h for the code and the exe resource.
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)
include_directories(${CMAKE_BINARY_DIR}/generated)

# ---- core: portable logic, no Win32 (text, JSON, chat parsing, glossary, protocols)
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
    src/core/corrections.cpp
    src/core/second_look.cpp
    src/core/typo_memory.cpp
    src/core/my_words.cpp
    src/core/rapid_rec.cpp
    src/core/rapid_models.cpp
    src/core/deepl_protocol.cpp
    src/core/mymemory_protocol.cpp
    src/core/llm_protocol.cpp
    src/core/chat_line.cpp
    src/core/chat_stream.cpp
    src/core/chat_tabs.cpp
    src/core/chat_geometry.cpp
    src/core/names.cpp
    src/core/image.cpp
    src/core/gw2_text.cpp
    src/core/mumble.cpp
    src/core/i18n.cpp
    src/core/i18n_de.cpp
    src/core/i18n_ar.cpp
    src/core/word_model.cpp
    src/core/tesseract_tsv.cpp
    src/core/languagetool_protocol.cpp
    src/core/gw2_install.cpp
    src/core/housekeeping.cpp
    src/core/voice.cpp
    src/core/voice_learn.cpp
    src/core/send_guard.cpp)
target_include_directories(gct_core PUBLIC src)

option(GCT_BUILD_TESTS "Build the core unit tests and the Windows test doubles" ON)
if(GCT_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE gct_core)
    add_test(NAME core_tests COMMAND core_tests)
endif()

if(WIN32)
    # ---- win: Windows services without UI. spellcheck.cpp and ocr.cpp are
    # linked per executable so the test build can swap in deterministic fakes.
    add_library(gct_win STATIC
        src/win/files.cpp
        src/win/http.cpp
        src/win/deepl_translator.cpp
        src/win/online_translators.cpp
        src/win/els.cpp
        src/win/gw2_api.cpp
        src/win/gw2_sender.cpp
        src/win/mumble_link.cpp
        src/win/screen_capture.cpp
        src/win/tesseract_ocr.cpp
        src/win/gw2_locate.cpp
        src/win/folder_cleanup.cpp
        src/win/keyboard_layout.cpp
        src/win/secret.cpp
        src/win/rapid_ocr.cpp
        src/win/extension.cpp
        src/win/hash.cpp
        src/win/audio_capture.cpp
        src/win/voice_models.cpp
        src/win/text_prediction.cpp)
    target_link_libraries(gct_win PUBLIC gct_core winhttp ole32 user32 gdi32 d3d11 dxgi dwmapi shell32 advapi32 version uuid crypt32 bcrypt runtimeobject)

    # ---- RapidOCR (optional, open source): PaddleOCR recognition models on ONNX Runtime (MIT, Microsoft).
    # The runtime is downloaded at configure time (checksum checked) and loaded only when RapidOCR is used
    # (delay load): without onnxruntime.dll next to the exe everything else works as before.
    option(GCT_RAPIDOCR "Build the RapidOCR text recognition (downloads ONNX Runtime)" ON)
    if(GCT_RAPIDOCR AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        include(FetchContent)
        FetchContent_Declare(onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-win-x64-1.30.0.zip
            URL_HASH SHA256=c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(onnxruntime)
        set(GCT_ORT_DIR ${onnxruntime_SOURCE_DIR})
        target_include_directories(gct_win PRIVATE ${GCT_ORT_DIR}/include)
        target_compile_definitions(gct_win PRIVATE GCT_HAVE_RAPIDOCR=1)
        target_link_libraries(gct_win PUBLIC ${GCT_ORT_DIR}/lib/onnxruntime.lib delayimp)
        set(GCT_ORT_DLL ${GCT_ORT_DIR}/lib/onnxruntime.dll)
        # The Latin model ships with the program (no install step): English, German, French, Spanish, Turkish …
        # Other script groups are downloaded in the settings when one of your languages needs them.
        set(GCT_RAPID_BASE https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2)
        set(GCT_RAPID_DIR ${CMAKE_BINARY_DIR}/rapid)
        if(NOT EXISTS ${GCT_RAPID_DIR}/latin_rec.onnx)
            file(DOWNLOAD ${GCT_RAPID_BASE}/onnx/PP-OCRv5/rec/latin_PP-OCRv5_rec_mobile.onnx ${GCT_RAPID_DIR}/latin_rec.onnx
                 EXPECTED_HASH SHA256=b20bd37c168a570f583afbc8cd7925603890efbcdc000a59e22c269d160b5f5a)
        endif()
        if(NOT EXISTS ${GCT_RAPID_DIR}/latin_dict.txt)
            file(DOWNLOAD ${GCT_RAPID_BASE}/paddle/PP-OCRv5/rec/latin_PP-OCRv5_rec_mobile/ppocrv5_latin_dict.txt
                 ${GCT_RAPID_DIR}/latin_dict.txt)
        endif()
    endif()

    # ---- Voice input of GW2 Keyboard (optional, open source): whisper.cpp (MIT) on the CPU. Built as DLLs that go to
    # <exe>\voice and are delay-loaded: without that folder GW2 Keyboard runs as before and voice is not offered.
    # One CPU backend DLL per instruction set (ggml-cpu-*), the best is chosen at runtime (CPUs without AVX2 too).
    # Static CRT and no OpenMP, so the DLLs need no VC++ redistributable.
    option(GCT_VOICE "Build the voice input of GW2 Keyboard (downloads whisper.cpp)" ON)
    if(GCT_VOICE AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
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
            EXCLUDE_FROM_ALL)  # only what GW2 Keyboard needs is built (not the bundled parakeet library)
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
    endif()

    # ---- app: the window
    set(GCT_APP_SOURCES
        src/app/main.cpp
        src/app/main_window.cpp
        src/app/input_box.cpp
        src/app/chat_log_view.cpp
        src/app/preview_view.cpp
        src/app/chat_reader.cpp
        src/app/region_picker.cpp
        src/app/spell_service.cpp
        src/app/suggestion_bar.cpp
        src/app/settings_dialog.cpp
        src/app/config.cpp
        res/app.rc)
    add_executable(GW2ChatTranslator WIN32 ${GCT_APP_SOURCES} src/win/spellcheck.cpp src/win/ocr.cpp)
    target_link_libraries(GW2ChatTranslator PRIVATE gct_win shell32 comctl32 runtimeobject)
    set(GCT_APP_TARGETS GW2ChatTranslator)

    # GW2 Keyboard: only the typing help (no chat reading, no OCR) – the same settings and learned words.
    add_executable(GW2Keyboard WIN32 src/kbd/kbd_main.cpp src/kbd/keyboard_window.cpp src/app/input_box.cpp
                   src/app/preview_view.cpp src/app/suggestion_bar.cpp src/app/spell_service.cpp
                   src/app/settings_dialog.cpp src/app/config.cpp src/win/spellcheck.cpp
                   src/kbd/voice_session.cpp src/kbd/whisper_engine.cpp res/app.rc)
    target_link_libraries(GW2Keyboard PRIVATE gct_win shell32 comctl32 runtimeobject)
    # Built without everything that only reading the chat needs (OCR, RapidOCR/onnxruntime, Tesseract, capture).
    target_compile_definitions(GW2Keyboard PRIVATE GCT_KEYBOARD_APP=1)
    list(APPEND GCT_APP_TARGETS GW2Keyboard)

    if(GCT_BUILD_TESTS)
        # Stand-in game: GW2 window class, chat panel fed from fake_chat.txt,
        # MumbleLink block, chat line that logs to received.txt.
        add_executable(fake_gw2 WIN32 tests/tools/fake_gw2.cpp)
        target_compile_definitions(fake_gw2 PRIVATE UNICODE _UNICODE)
        if(NOT MSVC)
            target_link_options(fake_gw2 PRIVATE -municode -static)
        endif()
        # Same app with a deterministic spell checker and an OCR that reads
        # fake_chat.txt instead of pixels (for Wine / UI tests).
        add_executable(GW2ChatTranslator_fakes WIN32 ${GCT_APP_SOURCES} tests/tools/fake_spellcheck.cpp
                       tests/tools/fake_ocr.cpp)
        target_link_libraries(GW2ChatTranslator_fakes PRIVATE gct_win shell32 comctl32)
        list(APPEND GCT_APP_TARGETS GW2ChatTranslator_fakes)
        # Measures text recognition on real chat pictures (local\bench, never committed).
        add_executable(ocr_bench tests/tools/ocr_bench.cpp src/win/ocr.cpp src/app/chat_reader.cpp src/win/spellcheck.cpp)
        target_link_libraries(ocr_bench PRIVATE gct_win windowscodecs runtimeobject)
        if(NOT MSVC)
            target_link_options(ocr_bench PRIVATE -municode)
        endif()
        list(APPEND GCT_APP_TARGETS ocr_bench)
        # Measures the typing help with a simulated player (invented messages, the real spell checker).
        add_executable(typing_bench tests/tools/typing_bench.cpp src/app/spell_service.cpp src/win/spellcheck.cpp)
        target_link_libraries(typing_bench PRIVATE gct_win)
        if(NOT MSVC)
            target_link_options(typing_bench PRIVATE -municode)
        endif()
        list(APPEND GCT_APP_TARGETS typing_bench)
        if(GCT_WHISPER)
            # Measures the voice recognizer on 16 kHz WAV recordings (local\voice, never committed).
            add_executable(voice_bench tests/tools/voice_bench.cpp src/kbd/whisper_engine.cpp)
            target_link_libraries(voice_bench PRIVATE gct_win)
            list(APPEND GCT_APP_TARGETS voice_bench)
        endif()
    endif()

    foreach(t gct_core gct_win ${GCT_APP_TARGETS})
        target_compile_definitions(${t} PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
    endforeach()

    if(MSVC)
        # Sources are UTF-8; static CRT so the exe runs without the VC++ redistributable.
        foreach(t gct_core gct_win ${GCT_APP_TARGETS})
            target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W4 /permissive->)
        endforeach()
        foreach(t ${GCT_APP_TARGETS})
            target_link_options(${t} PRIVATE /MANIFEST:NO)
        endforeach()
        set_property(TARGET gct_core gct_win ${GCT_APP_TARGETS} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        if(GCT_BUILD_TESTS)
            target_compile_options(core_tests PRIVATE /utf-8)
            set_property(TARGET core_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    else()
        foreach(t gct_core gct_win ${GCT_APP_TARGETS})
            target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
        endforeach()
        # wWinMain entry point; static runtime so no MinGW DLLs are needed.
        foreach(t ${GCT_APP_TARGETS})
            target_link_options(${t} PRIVATE -municode -static -static-libgcc -static-libstdc++)
        endforeach()
        if(GCT_BUILD_TESTS)
            target_link_options(core_tests PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()

    if(GCT_ORT_DLL)
        foreach(t ${GCT_APP_TARGETS})
            target_link_options(${t} PRIVATE /DELAYLOAD:onnxruntime.dll)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GCT_ORT_DLL} $<TARGET_FILE_DIR:${t}>
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${t}>/rapid
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GCT_RAPID_DIR}/latin_rec.onnx ${GCT_RAPID_DIR}/latin_dict.txt
                        $<TARGET_FILE_DIR:${t}>/rapid)
        endforeach()
    endif()

    if(GCT_WHISPER)
        # Voice input: whisper/ggml delay-loaded (GW2 Keyboard starts without them), the DLLs go to <exe>\voice.
        set(GCT_VOICE_DLLS $<TARGET_FILE:whisper> $<TARGET_FILE:ggml> $<TARGET_FILE:ggml-base>)
        foreach(c ${GCT_GGML_CPU_TARGETS})
            list(APPEND GCT_VOICE_DLLS $<TARGET_FILE:${c}>)
        endforeach()
        foreach(t GW2Keyboard voice_bench)
            if(TARGET ${t})
                target_link_libraries(${t} PRIVATE whisper ggml delayimp)
                target_compile_definitions(${t} PRIVATE GCT_HAVE_WHISPER=1)
                target_link_options(${t} PRIVATE /DELAYLOAD:whisper.dll /DELAYLOAD:ggml.dll)
                add_dependencies(${t} ${GCT_GGML_CPU_TARGETS})  # loaded at runtime only, no link dependency
                add_custom_command(TARGET ${t} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${t}>/voice
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GCT_VOICE_DLLS} $<TARGET_FILE_DIR:${t}>/voice)
            endif()
        endforeach()
    endif()
endif()
