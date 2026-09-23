cmake_minimum_required(VERSION 3.21)
project(snapx LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "MSVC is required")
endif()

add_executable(snapx
    src/main.cpp
    src/args.cpp
    src/window_enum.cpp
    src/capture.cpp
    src/encode.cpp
    src/clipboard.cpp
    res/snapx.rc
)

target_include_directories(snapx PRIVATE src)

target_compile_options(snapx PRIVATE /W4 /WX /utf-8 /EHsc /permissive-)

target_link_libraries(snapx PRIVATE
    windowsapp
    d3d11
    dxgi
    dwmapi
    windowscodecs
    shlwapi
    shell32
    user32
    ole32
)

target_compile_definitions(snapx PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

enable_testing()
add_test(NAME top-help COMMAND snapx --help)
add_test(NAME scan-command COMMAND snapx scan)
add_test(NAME scan-help COMMAND snapx scan --help)
add_test(NAME capture-help COMMAND snapx capture --help)
add_test(NAME screen-help COMMAND snapx screen --help)
add_test(NAME no-args COMMAND snapx)
set_tests_properties(no-args PROPERTIES WILL_FAIL TRUE)
add_test(NAME missing-pid COMMAND snapx capture -o shot.png)
set_tests_properties(missing-pid PROPERTIES WILL_FAIL TRUE)
add_test(NAME screen-rejects-pid COMMAND snapx screen --pid 1)
set_tests_properties(screen-rejects-pid PROPERTIES WILL_FAIL TRUE)
add_test(NAME clipboard-with-output COMMAND snapx capture --pid 1 -c -o shot.png)
set_tests_properties(clipboard-with-output PROPERTIES WILL_FAIL TRUE)
add_test(NAME clipboard-before-command COMMAND snapx -c)
set_tests_properties(clipboard-before-command PROPERTIES WILL_FAIL TRUE)
add_test(NAME bad-pid COMMAND snapx capture --pid abc)
set_tests_properties(bad-pid PROPERTIES WILL_FAIL TRUE)
add_test(NAME bad-scale COMMAND snapx capture --pid 1 --scale 6)
set_tests_properties(bad-scale PROPERTIES WILL_FAIL TRUE)
add_test(NAME bad-quality COMMAND snapx capture --pid 1 -q 0)
set_tests_properties(bad-quality PROPERTIES WILL_FAIL TRUE)
add_test(NAME bad-extension COMMAND snapx capture --pid 1 -o shot.webp)
set_tests_properties(bad-extension PROPERTIES WILL_FAIL TRUE)
add_test(NAME unknown-option COMMAND snapx --bogus)
set_tests_properties(unknown-option PROPERTIES WILL_FAIL TRUE)
add_test(NAME removed-list-alias COMMAND snapx --list)
set_tests_properties(removed-list-alias PROPERTIES WILL_FAIL TRUE)
add_test(NAME scan-rejects-capture-option COMMAND snapx scan -o shot.png)
set_tests_properties(scan-rejects-capture-option PROPERTIES WILL_FAIL TRUE)
add_test(NAME option-before-command COMMAND snapx --pid 1)
set_tests_properties(option-before-command PROPERTIES WILL_FAIL TRUE)
add_test(NAME duplicate-command COMMAND snapx scan capture)
set_tests_properties(duplicate-command PROPERTIES WILL_FAIL TRUE)
add_test(NAME no-such-pid COMMAND snapx capture --pid 999999)
set_tests_properties(no-such-pid PROPERTIES WILL_FAIL TRUE)
