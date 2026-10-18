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
FetchContent_MakeAvailable(SDL imgui json)
add_executable(touch_model_test touch_model_test.cpp)
add_executable(config_test config_test.c ../port/src/foxhollow_config.c)
target_compile_definitions(config_test PRIVATE TARGET_PC)
target_include_directories(config_test PRIVATE ../port/include ../extern/aurora/include)
target_link_libraries(config_test PRIVATE m)
add_executable(network_disabled_test network_disabled_test.cpp ../port/src/net/websocket_disabled.cpp)
add_executable(touch_input_test touch_input_test.cpp ../port/src/foxhollow_touch.cpp)
target_compile_definitions(touch_input_test PRIVATE TARGET_PC)
target_include_directories(touch_input_test PRIVATE
    ../extern/aurora/include ${sdl_SOURCE_DIR}/include ${imgui_SOURCE_DIR} ${json_SOURCE_DIR}/include)
option(FOXHOLLOW_TEST_SANITIZERS "FOXHOLLOW_TEST_SANITIZERS" ON)
foreach(target IN ITEMS touch_model_test touch_input_test config_test network_disabled_test)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    if (FOXHOLLOW_TEST_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif ()
    if (NOT target STREQUAL "config_test")
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
