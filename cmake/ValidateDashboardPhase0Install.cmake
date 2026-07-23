if(NOT DEFINED CONTRACT_PYTHON OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "CONTRACT_PYTHON, SOURCE_DIR, and BUILD_DIR are required")
endif()

set(prefix "${BUILD_DIR}/fhss-dashboard-phase0-installed")
file(LOCK "${BUILD_DIR}/fhss-dashboard-operator.lock"
     GUARD PROCESS TIMEOUT 1800 RESULT_VARIABLE lock_result)
if(NOT lock_result EQUAL 0)
  message(FATAL_ERROR "could not acquire dashboard install lock: ${lock_result}")
endif()
file(REMOVE_RECURSE "${prefix}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Phase 0 clean installation failed: ${install_result}")
endif()
execute_process(
  COMMAND "${CONTRACT_PYTHON}"
    "${SOURCE_DIR}/examples/DSP/dashboard/operator/phase0_baseline.py" verify
    --source-root "${SOURCE_DIR}"
    --installed-root "${prefix}"
  RESULT_VARIABLE verify_result)
if(NOT verify_result EQUAL 0)
  message(FATAL_ERROR
    "source and clean-installed frontend inventories differ: ${verify_result}")
endif()
execute_process(
  COMMAND "${CONTRACT_PYTHON}"
    "${prefix}/share/graphx/fhss-dashboard/operator/phase0_smoke.py"
    --executable "${prefix}/bin/graphx-dsp-fhss-demo"
    --assets "${prefix}/share/graphx/fhss-dashboard"
    --graph-config "${prefix}/share/graphx/config/fhss_phase2_binary_iq_receiver.json"
  RESULT_VARIABLE smoke_result TIMEOUT 30)
if(NOT smoke_result EQUAL 0)
  message(FATAL_ERROR "clean installed dashboard smoke failed: ${smoke_result}")
endif()
