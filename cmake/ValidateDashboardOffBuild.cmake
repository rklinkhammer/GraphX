if(NOT DEFINED SOURCE_DIR OR NOT DEFINED PARENT_BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and PARENT_BUILD_DIR are required")
endif()
set(off_build "${PARENT_BUILD_DIR}/dashboard-off-validation")
file(REMOVE_RECURSE "${off_build}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${off_build}" -G Ninja
                        -DGRAPHX_BUILD_WEB_DASHBOARD=OFF -DBUILD_TESTING=OFF
                RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "dashboard-off configure failed: ${configure_result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${off_build}" --target dsp_fhss_demo
                RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "dashboard-off build failed: ${build_result}")
endif()
