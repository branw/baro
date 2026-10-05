# One side of the concurrent-adapter check in discovery.cmake. execute_process
# joins concurrent commands into a pipeline; this writes nothing to stdout, so
# it cannot be ended by a closed pipe when the other side finishes first.
execute_process(COMMAND "${ADAPTER}" "${EXECUTABLE}" --test-id 1 --ctest
    OUTPUT_QUIET RESULT_VARIABLE status)
if(NOT "${status}" STREQUAL "0")
    message(FATAL_ERROR "Adapter failed: ${status}")
endif()
