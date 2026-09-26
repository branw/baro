# Developer-only coverage. Keep normal and sanitizer builds in separate trees.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BARO_GCOVR gcovr REQUIRED)
set(BARO_GCOV_EXECUTABLE "" CACHE STRING "Matching gcov command (or llvm-cov gcov)")
set(BARO_COVERAGE_MIN_LINE 85 CACHE STRING "Minimum measured line coverage percentage")
set(BARO_COVERAGE_MIN_BRANCH 65 CACHE STRING "Minimum measured branch coverage percentage")
if(NOT BARO_GCOV_EXECUTABLE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-prog-name=gcov
            OUTPUT_VARIABLE gcov OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    else()
        find_program(BARO_LLVM_COV llvm-cov)
        if(NOT BARO_LLVM_COV AND APPLE)
            execute_process(COMMAND xcrun --find llvm-cov OUTPUT_VARIABLE BARO_LLVM_COV
                OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        endif()
        if(NOT BARO_LLVM_COV)
            message(FATAL_ERROR "Coverage needs llvm-cov; set BARO_GCOV_EXECUTABLE")
        endif()
        set(gcov "\"${BARO_LLVM_COV}\" gcov")
    endif()
    set(BARO_GCOV_EXECUTABLE "${gcov}" CACHE STRING "Matching gcov command" FORCE)
endif()
get_property(coverage_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(coverage
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/coverage.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}"
        --ctest "${CMAKE_CTEST_COMMAND}" --config "$<CONFIG>"
        --gcovr "${BARO_GCOVR}" --gcov "${BARO_GCOV_EXECUTABLE}"
        --min-line "${BARO_COVERAGE_MIN_LINE}" --min-branch "${BARO_COVERAGE_MIN_BRANCH}"
    USES_TERMINAL VERBATIM)
foreach(target IN LISTS coverage_targets)
    get_target_property(type "${target}" TYPE)
    if(type STREQUAL "EXECUTABLE")
        add_dependencies(coverage "${target}")
    endif()
endforeach()
