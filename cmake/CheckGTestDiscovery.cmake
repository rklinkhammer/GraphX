if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

if(NOT DEFINED EXPECTED_SUITE AND NOT DEFINED EXPECTED_SUITES)
    message(FATAL_ERROR "EXPECTED_SUITE or EXPECTED_SUITES is required")
endif()

if(DEFINED EXPECTED_SUITE AND DEFINED EXPECTED_SUITES)
    message(FATAL_ERROR "Use either EXPECTED_SUITE or EXPECTED_SUITES, not both")
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

if(DEFINED EXPECTED_SUITE)
    set(_expected_suites "${EXPECTED_SUITE}")
else()
    set(_expected_suites ${EXPECTED_SUITES})
endif()

foreach(_expected_suite IN LISTS _expected_suites)
    string(FIND "${_output}" "${_expected_suite}." _suite_position)
    if(_suite_position EQUAL -1)
        message(FATAL_ERROR
            "${TEST_EXECUTABLE} does not own expected suite ${_expected_suite}")
    endif()
endforeach()

if(DEFINED UNEXPECTED_SUITES)
    foreach(_unexpected_suite IN LISTS UNEXPECTED_SUITES)
        string(FIND "${_output}" "${_unexpected_suite}." _suite_position)
        if(NOT _suite_position EQUAL -1)
            message(FATAL_ERROR
                "${TEST_EXECUTABLE} unexpectedly owns suite ${_unexpected_suite}")
        endif()
    endforeach()
endif()
