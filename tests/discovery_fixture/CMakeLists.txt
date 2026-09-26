cmake_minimum_required(VERSION 3.20)
project(baro_discovery_consumer C)
set(CMAKE_C_STANDARD 99)
set(BARO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BARO_SANITIZERS OFF CACHE BOOL "" FORCE)
add_subdirectory("${BARO_SOURCE}" baro)
enable_testing()
add_executable(fixture cases.c extra.c)
add_executable(descendant "${BARO_SOURCE}/tests/descendant.c")
set_target_properties(fixture PROPERTIES OUTPUT_NAME "fixture with spaces")
target_link_libraries(fixture PRIVATE baro::main)
target_compile_definitions(fixture PRIVATE "EXPECTED_CONFIG=\"$<CONFIG>\"")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/working directory")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/working directory/token" "present")
baro_discover_tests(fixture
    TEST_PREFIX "case::" TEST_SUFFIX "::suite"
    WORKING_DIRECTORY "working directory"
    ENVIRONMENT "BARO_VALUE=alpha;beta" [=[BARO_DOLLAR=${literal}]=] "BARO_CONFIG=$<CONFIG>"
        "BARO_DESCENDANT_EXE=$<TARGET_FILE:descendant>" "BARO_DESCENDANT_MARKER=${CMAKE_CURRENT_BINARY_DIR}/descendant-marker"
    LABELS "explicit;label" "config-$<CONFIG>"
    TIMEOUT 0.3 DISCOVERY_TIMEOUT 10
    PROPERTIES RESOURCE_LOCK "database" PROCESSORS 2
    EXTRA_ARGS -o)
