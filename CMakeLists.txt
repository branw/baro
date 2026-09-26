cmake_minimum_required(VERSION 3.20)
project(baro VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(baro_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(baro_top_level ON)
endif()
option(BARO_BUILD_TESTS "Build Baro's own tests" ${baro_top_level})
option(BARO_COVERAGE "Build developer coverage instrumentation and reports" OFF)
option(BARO_SANITIZERS "Instrument Baro and its consumers with sanitizers" OFF)

add_library(baro STATIC baro.c)
add_library(baro::baro ALIAS baro)
target_compile_definitions(baro PUBLIC BARO_ENABLE)
target_include_directories(baro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
if(NOT WIN32)
    target_link_libraries(baro PUBLIC m)
endif()
add_library(baro_main STATIC baro_main.c)
set_target_properties(baro_main PROPERTIES EXPORT_NAME main)
add_library(baro::main ALIAS baro_main)
target_link_libraries(baro_main PUBLIC baro)
if(MSVC)
    target_compile_options(baro PRIVATE /W4)
else()
    target_compile_options(baro PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BARO_SANITIZERS)
    if(MSVC)
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        target_compile_options(baro PUBLIC /fsanitize=address)
        target_link_options(baro PUBLIC /INCREMENTAL:NO)
    else()
        target_compile_options(baro PUBLIC -fsanitize=address,undefined)
        target_link_options(baro PUBLIC -fsanitize=address,undefined)
    endif()
endif()
add_executable(baro_ctest baro_ctest.c)
add_executable(baro::ctest ALIAS baro_ctest)
set_target_properties(baro_ctest PROPERTIES EXPORT_NAME ctest)
if(BARO_COVERAGE)
    if(NOT baro_top_level OR NOT BARO_BUILD_TESTS OR BARO_SANITIZERS OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "BARO_COVERAGE requires a top-level GCC/Clang test build without sanitizers")
    endif()
    target_compile_options(baro PUBLIC --coverage -O0 -g)
    target_link_options(baro PUBLIC --coverage)
    target_compile_options(baro_ctest PRIVATE --coverage -O0 -g)
    target_link_options(baro_ctest PRIVATE --coverage)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Baro.cmake")
include(GNUInstallDirs)
install(TARGETS baro baro_main baro_ctest EXPORT baroTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES baro.h DESTINATION include)
install(FILES cmake/Baro.cmake cmake/BaroDiscoverTests.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/baro)
install(EXPORT baroTargets NAMESPACE baro:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/baro)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/baroConfig.cmake" "include(\"\${CMAKE_CURRENT_LIST_DIR}/baroTargets.cmake\")\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/Baro.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/baroConfig.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/baro)

if(NOT BARO_BUILD_TESTS)
    return()
endif()
enable_testing()
add_executable(baro_test baro_test.c)
target_link_libraries(baro_test PRIVATE baro::main)
target_compile_definitions(baro_test PRIVATE BARO_SELF_TEST)
add_test(NAME self_tests COMMAND baro_test)

macro(AddExampleTest test expected_status)
    add_executable(
        "example_${test}"
        "examples/${test}.c")
    target_link_libraries("example_${test}" PRIVATE baro::main)
    target_compile_definitions("example_${test}" PRIVATE BARO_ENABLE)
    target_include_directories("example_${test}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME "check_example_${test}"
        COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:example_${test}>"
            "-DARGS=${ARGN}"
            "-DEXPECTED_STATUS=${expected_status}"
            "-DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/${test}.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_example.cmake")
endmacro()

AddExampleTest(basic 1 -a)
AddExampleTest(subtests 1 -ao)
AddExampleTest(unicode_encoding 0 -a)
AddExampleTest(empty 1)
AddExampleTest(tag_filtering 1 -t foo,bar)
AddExampleTest(partitioning 1 -n 2 -p 5)

# This test causes a Visual C++ Runtime Library abort() when building in MSVC..?
if(NOT MSVC)
    AddExampleTest(assert 1 -er)
    target_compile_definitions(example_assert PRIVATE BARO_REPLACE_ASSERT)
endif()

# Excluded from the suite because it takes so long to build
#AddExampleTest(stress_tests 1)

add_executable(baro_regressions tests/regressions.c)
target_link_libraries(baro_regressions PRIVATE baro::main)
target_include_directories(baro_regressions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(baro_regressions PRIVATE BARO_ENABLE)
foreach(case numeric float_failure require output suppression descriptors many
             abort cleanup cleanup_failure null_strings null_failures capture4095 capture4096 capture4097
             capture9000 capture_isolation)
    add_test(NAME regression_${case}
        COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:baro_regressions>"
            "-DCASE=${case}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/regression_${case}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_regression.cmake")
endforeach()
add_executable(baro_disabled tests/disabled.c)
target_include_directories(baro_disabled PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME disabled_build COMMAND baro_disabled)

foreach(partition RANGE 1 6)
    add_test(NAME regression_partition_${partition}
        COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:example_partitioning>"
            "-DCASE=partition"
            "-DPARTITION=${partition}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/regression_partition_${partition}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_regression.cmake")
endforeach()

add_test(NAME consumer_integration COMMAND ${CMAKE_COMMAND}
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}"
    "-DCONFIG=$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer.cmake")

add_executable(example_readme examples/readme.c)
target_link_libraries(example_readme PRIVATE baro::main)
add_test(NAME readme_example COMMAND example_readme)
add_test(NAME automation COMMAND ${CMAKE_COMMAND}
    "-DEMPTY_PROGRAM=$<TARGET_FILE:example_empty>"
    "-DPROGRAM=$<TARGET_FILE:baro_regressions>"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/automation.cmake")
add_test(NAME documentation COMMAND ${CMAKE_COMMAND}
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/documentation.cmake")
add_executable(baro_descendant tests/descendant.c)
add_test(NAME isolation COMMAND ${CMAKE_COMMAND}
    "-DDESCENDANT=$<TARGET_FILE:baro_descendant>"
    "-DPROGRAM=$<TARGET_FILE:baro_regressions>"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/isolation.cmake")

get_property(baro_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
add_test(NAME discovery_integration COMMAND ${CMAKE_COMMAND}
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/discovery-integration"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DMULTI=${baro_multi}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery.cmake")
if(NOT baro_multi)
    find_program(BARO_NINJA ninja)
    find_program(BARO_XCODEBUILD xcodebuild)
    if(BARO_NINJA)
        set(baro_multi_generator "Ninja Multi-Config")
    elseif(APPLE AND BARO_XCODEBUILD)
        set(baro_multi_generator "Xcode")
    endif()
    if(baro_multi_generator)
        add_test(NAME discovery_multiconfig COMMAND ${CMAKE_COMMAND}
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/discovery-multiconfig"
            "-DGENERATOR=${baro_multi_generator}"
            -DMULTI=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery.cmake")
    endif()
endif()

add_executable(baro_discovery_probe tests/discovery_probe.c)
add_test(NAME discovery_hardening COMMAND ${CMAKE_COMMAND}
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/discovery-hardening"
    "-DPROBE=$<TARGET_FILE:baro_discovery_probe>"
    "-DRUNNER=$<TARGET_FILE:baro_ctest>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery_hardening.cmake")

add_test(NAME adapter_contract COMMAND ${CMAKE_COMMAND}
    "-DPROGRAM=$<TARGET_FILE:baro_regressions>" "-DADAPTER=$<TARGET_FILE:baro_ctest>"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/adapter.cmake")

if(BARO_COVERAGE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BaroCoverage.cmake")
endif()
