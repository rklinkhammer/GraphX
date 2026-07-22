if(NOT DEFINED CONTRACT_PYTHON OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "CONTRACT_PYTHON, SOURCE_DIR, and BUILD_DIR are required")
endif()
# Source and installed operator lanes intentionally use fixed install/evidence
# paths. RUN_SERIAL only serializes tests inside one CTest process, so take an
# OS-level lock to prevent independent CTest invocations or agents from
# deleting, rewriting, or resource-contending those paths concurrently.
file(LOCK "${BUILD_DIR}/fhss-dashboard-operator.lock"
     GUARD PROCESS TIMEOUT 1800 RESULT_VARIABLE operator_lock_result)
if(NOT operator_lock_result EQUAL 0)
  message(FATAL_ERROR
    "could not acquire exclusive dashboard operator lock: ${operator_lock_result}")
endif()
set(operator "${SOURCE_DIR}/examples/DSP/dashboard/operator/fhss_dashboard_operator.py")
if(NOT DEFINED PHASE)
  set(PHASE 1)
endif()
set(operator_build "${BUILD_DIR}")
set(output "${BUILD_DIR}/fhss-dashboard-phase${PHASE}-operator-ctest")
if(INSTALLED_TREE)
  set(prefix "${BUILD_DIR}/fhss-dashboard-installed-ctest")
  file(REMOVE_RECURSE "${prefix}")
  execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}"
                  RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "dashboard installed-tree install failed: ${result}")
  endif()
  set(operator "${prefix}/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py")
  set(operator_build "${prefix}")
  set(output "${BUILD_DIR}/fhss-dashboard-installed-phase${PHASE}-operator-ctest")
endif()
file(REMOVE_RECURSE "${output}")
execute_process(COMMAND "${CONTRACT_PYTHON}" "${operator}" exercise --phase "${PHASE}"
                        --build-dir "${operator_build}" --output-dir "${output}"
                RESULT_VARIABLE exercise_result)
if(NOT exercise_result EQUAL 0)
  message(FATAL_ERROR "dashboard operator exercise failed: ${exercise_result}")
endif()
if(PHASE EQUAL 4)
  foreach(case IN ITEMS clean impaired negative)
    execute_process(COMMAND "${CONTRACT_PYTHON}" "${operator}" serve --phase "${PHASE}"
                            --build-dir "${operator_build}" --output-dir "${output}"
                            --case "${case}" --exit-after-case
                    RESULT_VARIABLE serve_result TIMEOUT 90)
    if(NOT serve_result EQUAL 0)
      message(FATAL_ERROR "dashboard operator exercise-to-serve ${case} failed: ${serve_result}")
    endif()
  endforeach()
elseif(PHASE EQUAL 5)
  foreach(case IN ITEMS step continue cancelled)
    execute_process(COMMAND "${CONTRACT_PYTHON}" "${operator}" serve --phase "${PHASE}"
                            --build-dir "${operator_build}" --output-dir "${output}"
                            --case "${case}" --exit-after-case
                    RESULT_VARIABLE serve_result TIMEOUT 90)
    if(NOT serve_result EQUAL 0)
      message(FATAL_ERROR "dashboard operator exercise-to-serve ${case} failed: ${serve_result}")
    endif()
  endforeach()
endif()
execute_process(COMMAND "${CONTRACT_PYTHON}" "${operator}" verify --phase "${PHASE}" --output-dir "${output}"
                RESULT_VARIABLE verify_result)
if(NOT verify_result EQUAL 0)
  message(FATAL_ERROR "dashboard operator verify failed: ${verify_result}")
endif()
if(PHASE EQUAL 4)
  message(STATUS "Phase 4 automation evidence is PARTIAL/pre-browser; final PASS requires clean, impaired, and negative record-screenshot artifacts followed by verify --require-screenshots")
elseif(PHASE EQUAL 5)
  message(STATUS "Phase 5 operator workflow passed; browser screenshot evidence remains a manual verification artifact")
endif()
