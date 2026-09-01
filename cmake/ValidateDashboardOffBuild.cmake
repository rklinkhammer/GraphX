if(NOT DEFINED SOURCE_DIR OR NOT DEFINED PARENT_BUILD_DIR)
  message(FATAL_ERROR "SOURCE_DIR and PARENT_BUILD_DIR are required")
endif()
set(off_build "${PARENT_BUILD_DIR}/dashboard-off-validation")
set(build_jobs "")
if(DEFINED ENV{GRAPHX_BUILD_JOBS})
  if(NOT "$ENV{GRAPHX_BUILD_JOBS}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "GRAPHX_BUILD_JOBS must be a positive integer")
  endif()
  set(build_jobs "$ENV{GRAPHX_BUILD_JOBS}")
endif()
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
  if(NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
      "CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer")
  endif()
  if(build_jobs STREQUAL "")
    set(build_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
  endif()
endif()
set(build_command "${CMAKE_COMMAND}" --build "${off_build}")
if(NOT build_jobs STREQUAL "")
  list(APPEND build_command --parallel "${build_jobs}")
endif()
list(APPEND build_command --target dsp_fhss_demo)

if(DEFINED ENV{GRAPHX_BUILD_JOBS} AND
   DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND
   NOT "$ENV{GRAPHX_BUILD_JOBS}" STREQUAL
       "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
  message(FATAL_ERROR
    "GRAPHX_BUILD_JOBS and CMAKE_BUILD_PARALLEL_LEVEL must agree")
endif()
file(REMOVE_RECURSE "${off_build}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${off_build}" -G Ninja
                        -DGRAPHX_BUILD_WEB_DASHBOARD=OFF -DBUILD_TESTING=OFF
                RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "dashboard-off configure failed: ${configure_result}")
endif()
execute_process(COMMAND ${build_command}
                RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "dashboard-off build failed: ${build_result}")
endif()
