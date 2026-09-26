cmake_minimum_required(VERSION 3.13)
project(baro C)

set(CMAKE_C_STANDARD 99)

enable_testing()

add_executable(baro_test baro.c baro_test.c)
target_compile_definitions(baro_test PRIVATE BARO_ENABLE BARO_SELF_TEST)
if(MSVC)
    # TODO remove static linking once ASan is in the PATH
    target_compile_options(baro_test PRIVATE /MTd /fsanitize=address)
    target_link_options(baro_test PRIVATE /fsanitize=address)
else()
    target_compile_options(baro_test PRIVATE -fsanitize=address,undefined)
    target_link_options(baro_test PRIVATE -fsanitize=address,undefined)
endif()

add_test(self_tests baro_test)

macro(AddExampleTest test expected_status)
    add_executable(
        "example_${test}"
        "examples/${test}.c" baro.c)
    target_compile_definitions("example_${test}" PRIVATE BARO_ENABLE)
    target_include_directories("example_${test}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        # TODO remove static linking once ASan is in the PATH
        target_compile_options("example_${test}" PRIVATE /MTd /fsanitize=address)
        target_link_options("example_${test}" PRIVATE /fsanitize=address)
    else()
        target_compile_options("example_${test}" PRIVATE -fsanitize=address,undefined)
        target_link_options("example_${test}" PRIVATE -fsanitize=address,undefined)
    endif()

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

add_executable(baro_regressions tests/regressions.c baro.c)
target_include_directories(baro_regressions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(baro_regressions PRIVATE BARO_ENABLE)
if(MSVC)
    target_compile_options(baro_regressions PRIVATE /MTd /fsanitize=address)
    target_link_options(baro_regressions PRIVATE /fsanitize=address)
else()
    target_compile_options(baro_regressions PRIVATE -fsanitize=address,undefined)
    target_link_options(baro_regressions PRIVATE -fsanitize=address,undefined)
endif()
foreach(case numeric float_failure require output suppression descriptors many
             abort cleanup null_strings null_failures capture4095 capture4096 capture4097
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
