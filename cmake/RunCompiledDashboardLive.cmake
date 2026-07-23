if(NOT DEFINED CONTRACT_PYTHON OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "CONTRACT_PYTHON, SOURCE_DIR, and BUILD_DIR are required")
endif()
if(INSTALLED_TREE)
  set(RUN_ROOT "${BUILD_DIR}/fhss-dashboard-compiled-live-install")
  file(REMOVE_RECURSE "${RUN_ROOT}")
  execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${RUN_ROOT}"
    RESULT_VARIABLE install_result)
  if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "compiled dashboard live install failed")
  endif()
else()
  set(RUN_ROOT "${BUILD_DIR}")
endif()
set(OUTPUT_DIR "${BUILD_DIR}/fhss-dashboard-compiled-live-${INSTALLED_TREE}")
file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
  COMMAND "${CONTRACT_PYTHON}"
    "${SOURCE_DIR}/examples/DSP/dashboard/operator/compiled_dashboard_live.py"
    --build-dir "${RUN_ROOT}" --output-dir "${OUTPUT_DIR}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE live_result)
if(NOT live_result EQUAL 0)
  message(FATAL_ERROR "compiled dashboard live acceptance failed: ${live_result}")
endif()
