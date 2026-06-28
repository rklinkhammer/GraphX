if(NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED EXPECTED_SUITE)
    message(FATAL_ERROR "TEST_EXECUTABLE and EXPECTED_SUITE are required")
endif()

execute_process(
    COMMAND "${TEST_EXECUTABLE}" --gtest_list_tests
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Failed to discover tests from ${TEST_EXECUTABLE}: ${_error}")
endif()

string(FIND "${_output}" "${EXPECTED_SUITE}." _suite_position)
if(_suite_position EQUAL -1)
    message(FATAL_ERROR
        "${TEST_EXECUTABLE} does not own expected suite ${EXPECTED_SUITE}")
endif()
