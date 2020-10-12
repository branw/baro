cmake_minimum_required(VERSION 3.12)
project(canyon C)

set(CMAKE_C_STANDARD 99)

add_executable(baro-tests baro.c baro_test.c)
target_compile_definitions(baro-tests PRIVATE BARO_ENABLE BARO_SELF_TEST)

enable_testing()
add_test(baro baro-tests)
