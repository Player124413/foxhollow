cmake_minimum_required(VERSION 3.25)
project(foxhollow_input_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
include(FetchContent)
FetchContent_Declare(SDL
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.10
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _headers_only)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b-docking
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _headers_only)
FetchContent_Declare(json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _headers_only)
FetchContent_Declare(xxhash
    URL https://codeload.github.com/Cyan4973/xxHash/tar.gz/refs/tags/v0.8.3
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _headers_only)
FetchContent_MakeAvailable(SDL imgui json xxhash)
add_library(etc1_test_vendor STATIC ../extern/aurora/extern/rg_etc1/rg_etc1.cpp)
add_executable(etc2_test etc2_test.cpp ../extern/aurora/lib/gfx/etc2.cpp)
target_include_directories(etc2_test PRIVATE ${xxhash_SOURCE_DIR})
target_link_libraries(etc2_test PRIVATE etc1_test_vendor)
add_library(imgui_test STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp)
target_include_directories(imgui_test PUBLIC ${imgui_SOURCE_DIR})
target_compile_definitions(imgui_test PUBLIC IMGUI_ENABLE_TEST_ENGINE)
add_executable(touch_ui_test touch_ui_test.cpp ../port/src/foxhollow_touch.cpp
    ../port/src/foxhollow_cutscene.c ../port/src/foxhollow_quit.c
    ../extern/aurora/lib/input/router.cpp ../extern/aurora/lib/input/source_state.cpp)
target_compile_definitions(touch_ui_test PRIVATE TARGET_PC)
target_include_directories(touch_ui_test PRIVATE ../port/include
    ../extern/aurora/include ${sdl_SOURCE_DIR}/include ${json_SOURCE_DIR}/include)
target_link_libraries(touch_ui_test PRIVATE imgui_test)
add_executable(cutscene_playback_test cutscene_playback_test.c)
target_compile_definitions(cutscene_playback_test PRIVATE TARGET_PC)
target_compile_options(cutscene_playback_test PRIVATE -fno-strict-aliasing -include ${CMAKE_CURRENT_SOURCE_DIR}/../port/include/foxhollow_compat.h)
target_include_directories(cutscene_playback_test PRIVATE ../port/include ../extern/aurora/include ../game/include ../game)
add_executable(cutscene_test cutscene_test.c ../port/src/foxhollow_cutscene.c)
target_include_directories(cutscene_test PRIVATE ../port/include)
add_executable(config_android_test config_test.c ../port/src/foxhollow_config.c)
target_compile_definitions(config_android_test PRIVATE TARGET_PC __ANDROID__)
target_include_directories(config_android_test PRIVATE ../port/include ../extern/aurora/include)
target_link_libraries(config_android_test PRIVATE m)
add_executable(touch_model_test touch_model_test.cpp)
add_executable(config_test config_test.c ../port/src/foxhollow_config.c)
target_compile_definitions(config_test PRIVATE TARGET_PC)
target_include_directories(config_test PRIVATE ../port/include ../extern/aurora/include)
target_link_libraries(config_test PRIVATE m)
add_executable(network_disabled_test network_disabled_test.cpp ../port/src/net/websocket_disabled.cpp)
add_executable(touch_input_test touch_input_test.cpp ../port/src/foxhollow_touch.cpp)
target_compile_definitions(touch_input_test PRIVATE TARGET_PC)
target_include_directories(touch_input_test PRIVATE
    ../port/include ../extern/aurora/include ${sdl_SOURCE_DIR}/include ${imgui_SOURCE_DIR} ${json_SOURCE_DIR}/include)
option(FOXHOLLOW_TEST_SANITIZERS "FOXHOLLOW_TEST_SANITIZERS" ON)
foreach(target IN ITEMS etc2_test touch_model_test touch_input_test touch_ui_test cutscene_test cutscene_playback_test config_android_test config_test network_disabled_test imgui_test)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    if (FOXHOLLOW_TEST_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif ()
    if (NOT target MATCHES "^(config(_android)?_test|imgui_test)$")
        add_test(NAME ${target} COMMAND ${target})
    endif ()
endforeach()

add_test(NAME config_default COMMAND config_test 1)
foreach(pair IN ITEMS "0:0" "0.5:0.5" "1:1" "nan:1" "inf:1" "-1:1" "2:1" "0.5junk:1" ":1")
    string(REPLACE ":" ";" parts "${pair}")
    list(GET parts 0 value)
    list(GET parts 1 expected)
    string(MAKE_C_IDENTIFIER "config_${value}" name)
    add_test(NAME ${name} COMMAND config_test ${expected})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "FOXHOLLOW_AUDIO_GAIN=${value}")
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    add_test(NAME android_elf_validation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_verify_native.py)
endif ()

foreach(renderer IN ITEMS auto vulkan opengles null invalid)
    add_test(NAME android_renderer_${renderer} COMMAND config_android_test 1)
    set_tests_properties(android_renderer_${renderer} PROPERTIES ENVIRONMENT "FOXHOLLOW_RENDERER=${renderer}")
endforeach()
add_test(NAME android_renderer_default COMMAND config_android_test 1)

add_test(NAME cutscene_native_syntax COMMAND ${CMAKE_C_COMPILER}
    -fsyntax-only -w -Wno-error=incompatible-pointer-types -std=gnu11
    -DTARGET_PC -D__ANDROID__ -DAURORA -DBUILD_VERSION=0 -DVERSION_GSAE01
    -I${CMAKE_CURRENT_SOURCE_DIR}/../port/include
    -I${CMAKE_CURRENT_SOURCE_DIR}/../extern/aurora/include
    -I${CMAKE_CURRENT_SOURCE_DIR}/../game/include -I${CMAKE_CURRENT_SOURCE_DIR}/../game
    -I${CMAKE_CURRENT_SOURCE_DIR}/../extern/musyx/include -I${sdl_SOURCE_DIR}/include
    -include ${CMAKE_CURRENT_SOURCE_DIR}/../port/include/foxhollow_compat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/src/dlls/engine/52_n_attractmode/n_attractmode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/src/dlls/engine/2/2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../port/src/vi_shim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/src/dlls/objects/488_SB_Galleon/SB_Galleon.c)

add_test(NAME touch_ui_router_test COMMAND touch_ui_test routed)

add_test(NAME launcher_resources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_launcher_resources.py)
add_test(NAME android_signing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_android_signing.py)
