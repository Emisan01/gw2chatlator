cmake_minimum_required(VERSION 3.20)
project(GW2ChatTranslator VERSION 0.9.0 LANGUAGES CXX)
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
    src/core/glyph_reader.cpp
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
    src/core/housekeeping.cpp)
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
        src/win/rapid_ocr.cpp)
    target_link_libraries(gct_win PUBLIC gct_core winhttp ole32 user32 gdi32 d3d11 dxgi dwmapi shell32 advapi32 version uuid crypt32 bcrypt)

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
endif()
