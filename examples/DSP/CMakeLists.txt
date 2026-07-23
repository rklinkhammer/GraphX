cmake_minimum_required(VERSION 3.23)

project(graphx_dsp_example LANGUAGES CXX)

find_package(log4cxx REQUIRED)

add_library(dsp_fhss_iq_artifacts STATIC
    src/FHSSHash.cpp
    src/FHSSIqArtifactGenerator.cpp)
target_include_directories(dsp_fhss_iq_artifacts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dsp_fhss_iq_artifacts PUBLIC dsp)

if(GRAPHX_BUILD_WEB_DASHBOARD)
    # Remove obsolete Phase 1 exact-toolchain cache entries so an existing build
    # cannot continue resolving Node or npm from an ephemeral path.
    unset(GRAPHX_DASHBOARD_NODE_EXECUTABLE CACHE)
    unset(GRAPHX_DASHBOARD_NPM_CLI CACHE)
    unset(GRAPHX_DASHBOARD_ALLOW_UNPINNED_TOOLCHAIN CACHE)
    find_program(GRAPHX_DASHBOARD_HOST_NODE NAMES node NO_CACHE)
    find_program(GRAPHX_DASHBOARD_HOST_NPM NAMES npm NO_CACHE)
    if(NOT GRAPHX_DASHBOARD_HOST_NODE)
        message(FATAL_ERROR
            "Node.js was not found on the host PATH. Install a supported "
            "Node.js release on the host, then reconfigure. GraphX does not "
            "download or provision Node.js in a temporary directory.")
    endif()
    if(NOT GRAPHX_DASHBOARD_HOST_NPM)
        message(FATAL_ERROR
            "npm was not found on the host PATH. Install supported npm on the "
            "host, then reconfigure. GraphX does not download or provision npm "
            "in a temporary directory.")
    endif()
    get_filename_component(GRAPHX_DASHBOARD_NODE_DIRECTORY
        "${GRAPHX_DASHBOARD_HOST_NODE}" DIRECTORY)
    get_filename_component(GRAPHX_DASHBOARD_NPM_DIRECTORY
        "${GRAPHX_DASHBOARD_HOST_NPM}" DIRECTORY)
    set(GRAPHX_DASHBOARD_HOST_ENV_COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${GRAPHX_DASHBOARD_NPM_DIRECTORY}:${GRAPHX_DASHBOARD_NODE_DIRECTORY}:$ENV{PATH}")
    set(GRAPHX_DASHBOARD_NPM_COMMAND
        ${GRAPHX_DASHBOARD_HOST_ENV_COMMAND} "${GRAPHX_DASHBOARD_HOST_NPM}")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/toolchain.json"
        GRAPHX_DASHBOARD_TOOLCHAIN_JSON)
    string(JSON GRAPHX_DASHBOARD_NODE_MINIMUM GET
        "${GRAPHX_DASHBOARD_TOOLCHAIN_JSON}" node_minimum)
    string(JSON GRAPHX_DASHBOARD_NODE_MAXIMUM GET
        "${GRAPHX_DASHBOARD_TOOLCHAIN_JSON}" node_maximum_exclusive)
    string(JSON GRAPHX_DASHBOARD_NPM_MINIMUM GET
        "${GRAPHX_DASHBOARD_TOOLCHAIN_JSON}" npm_minimum)
    string(JSON GRAPHX_DASHBOARD_NPM_MAXIMUM GET
        "${GRAPHX_DASHBOARD_TOOLCHAIN_JSON}" npm_maximum_exclusive)
    execute_process(COMMAND "${GRAPHX_DASHBOARD_HOST_NODE}" --version
        OUTPUT_VARIABLE GRAPHX_DASHBOARD_ACTUAL_NODE OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GRAPHX_DASHBOARD_NODE_RESULT)
    execute_process(COMMAND ${GRAPHX_DASHBOARD_NPM_COMMAND} --version
        OUTPUT_VARIABLE GRAPHX_DASHBOARD_ACTUAL_NPM OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GRAPHX_DASHBOARD_NPM_RESULT)
    string(REGEX REPLACE "^v" "" GRAPHX_DASHBOARD_ACTUAL_NODE
        "${GRAPHX_DASHBOARD_ACTUAL_NODE}")
    if(NOT GRAPHX_DASHBOARD_NODE_RESULT EQUAL 0 OR
       GRAPHX_DASHBOARD_ACTUAL_NODE VERSION_LESS GRAPHX_DASHBOARD_NODE_MINIMUM OR
       NOT GRAPHX_DASHBOARD_ACTUAL_NODE VERSION_LESS GRAPHX_DASHBOARD_NODE_MAXIMUM)
        message(FATAL_ERROR
            "The host Node.js installation is missing or unsupported. "
            "Install Node >=${GRAPHX_DASHBOARD_NODE_MINIMUM} and "
            "<${GRAPHX_DASHBOARD_NODE_MAXIMUM} on the host, then reconfigure. "
            "Found '${GRAPHX_DASHBOARD_ACTUAL_NODE}'. GraphX does not download "
            "or provision Node.js in a temporary directory.")
    endif()
    if(NOT GRAPHX_DASHBOARD_NPM_RESULT EQUAL 0 OR
       GRAPHX_DASHBOARD_ACTUAL_NPM VERSION_LESS GRAPHX_DASHBOARD_NPM_MINIMUM OR
       NOT GRAPHX_DASHBOARD_ACTUAL_NPM VERSION_LESS GRAPHX_DASHBOARD_NPM_MAXIMUM)
        message(FATAL_ERROR
            "The host npm installation is missing or unsupported. "
            "Install npm >=${GRAPHX_DASHBOARD_NPM_MINIMUM} and "
            "<${GRAPHX_DASHBOARD_NPM_MAXIMUM} on the host, then reconfigure. "
            "Found '${GRAPHX_DASHBOARD_ACTUAL_NPM}'. GraphX does not download "
            "or provision npm in a temporary directory.")
    endif()
    execute_process(
        COMMAND ${GRAPHX_DASHBOARD_NPM_COMMAND} ls --depth=0 --offline
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend"
        RESULT_VARIABLE GRAPHX_DASHBOARD_DEPENDENCY_RESULT
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT GRAPHX_DASHBOARD_DEPENDENCY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Dashboard frontend dependencies are not installed in the source "
            "workspace. Using the supported host Node/npm installation, run "
            "'npm ci --ignore-scripts --offline' in "
            "examples/DSP/dashboard/frontend, then reconfigure. GraphX does "
            "not install packages or use temporary package prefixes.")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dashboard-toolchain.json"
        "{\"schema\":\"graphx.fhss.dashboard.host_toolchain.v1\","
        "\"node\":\"${GRAPHX_DASHBOARD_ACTUAL_NODE}\","
        "\"npm\":\"${GRAPHX_DASHBOARD_ACTUAL_NPM}\","
        "\"resolution\":\"host_PATH\",\"supported\":true}\n")
    file(GLOB_RECURSE GRAPHX_DASHBOARD_FRONTEND_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/src/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/test/*")
    set(GRAPHX_DASHBOARD_DIST_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/fhss-dashboard-dist")
    add_custom_command(
        OUTPUT "${GRAPHX_DASHBOARD_DIST_DIR}/index.html"
        COMMAND ${GRAPHX_DASHBOARD_NPM_COMMAND} run build --
            --outDir "${GRAPHX_DASHBOARD_DIST_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/package.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/package-lock.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/toolchain.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/vite.config.ts"
            "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/frontend/index.html"
            ${GRAPHX_DASHBOARD_FRONTEND_SOURCES}
        COMMENT "Building the single compiled GraphX FHSS dashboard"
        VERBATIM)
    add_custom_target(dsp_fhss_dashboard_frontend ALL
        DEPENDS "${GRAPHX_DASHBOARD_DIST_DIR}/index.html")
    file(READ "${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json"
        GRAPHX_FHSS_RECEIVER_TEMPLATE_JSON)
    configure_file(
        dashboard/FHSSReceiverTemplate.hpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/FHSSReceiverTemplate.hpp"
        @ONLY)
    set(GRAPHX_FHSS_PINNED_SCHEMA_FILES
        dashboard/sigmf/official-v1.2.6/sigmf-schema.json
        dashboard/api/schemas/fhss-investigation-manifest.schema.json
        dashboard/api/schemas/fhss-investigation-artifact-entry.schema.json
        dashboard/api/schemas/fhss-investigation-provenance.schema.json
        dashboard/api/schemas/fhss-receiver-observation.schema.json
        dashboard/api/schemas/fhss-comparison-result.schema.json
        dashboard/api/schemas/fhss-receiver-result.schema.json
        dashboard/api/schemas/fhss-operator-actions.schema.json
        dashboard/api/schemas/fhss-external-iq-reference.schema.json
        dashboard/api/schemas/fhss-iq-truth.schema.json
        dashboard/api/schemas/fhss-receiver-graph.schema.json
        dashboard/api/schemas/fhss-build-api-manifest.schema.json)
    set(GRAPHX_FHSS_PINNED_SCHEMA_INITIALIZERS "")
    set(GRAPHX_FHSS_PINNED_SCHEMA_DIGEST_INITIALIZERS "")
    foreach(schema_file IN LISTS GRAPHX_FHSS_PINNED_SCHEMA_FILES)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${schema_file}" schema_json)
        file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${schema_file}" schema_sha256)
        string(APPEND GRAPHX_FHSS_PINNED_SCHEMA_INITIALIZERS
            "R\"GRAPHX_SCHEMA(${schema_json})GRAPHX_SCHEMA\",\n")
        string(APPEND GRAPHX_FHSS_PINNED_SCHEMA_DIGEST_INITIALIZERS
            "std::pair<std::string_view,std::string_view>{\"${schema_file}\",\"${schema_sha256}\"},\n")
    endforeach()
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/api/openapi.json"
        GRAPHX_FHSS_OPENAPI_SHA256)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GRAPHX_FHSS_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GRAPHX_FHSS_SOURCE_DIRTY_FILES OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(GRAPHX_FHSS_SOURCE_DIRTY_FILES)
        set(GRAPHX_FHSS_SOURCE_DIRTY true)
    else()
        set(GRAPHX_FHSS_SOURCE_DIRTY false)
    endif()
    configure_file(
        dashboard/FHSSPinnedSchemas.hpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/FHSSPinnedSchemas.hpp"
        @ONLY)
    add_library(dsp_fhss_dashboard_api STATIC
        dashboard/FHSSDashboardApi.cpp
        dashboard/FHSSObservationService.cpp
        dashboard/FHSSGraphRuntimeOwner.cpp
        dashboard/FHSSJobController.cpp
        dashboard/FHSSInvestigationBundleService.cpp
        dashboard/FHSSDashboardConfigurationPolicy.cpp)
    target_include_directories(dsp_fhss_dashboard_api PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dashboard
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(dsp_fhss_dashboard_api PUBLIC dsp dsp_fhss_iq_artifacts)
    if(ENABLE_TSAN)
        target_compile_options(dsp_fhss_dashboard_api PRIVATE
            -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(dsp_fhss_dashboard_api PUBLIC -fsanitize=thread)
    endif()
    install(FILES
        "${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json"
        DESTINATION share/graphx/config)
endif()

add_executable(dsp_spectrum_demo
    src/main.cpp
)
set_target_properties(dsp_spectrum_demo PROPERTIES
    OUTPUT_NAME "graphx-dsp-spectrum-demo"
)

# The graph runtime currently resolves its GPU capability bootstrap symbol from libgpu;
# the DSP demo topology itself remains CPU-only.
target_link_libraries(dsp_spectrum_demo PRIVATE dsp log4cxx graph gpu)
target_compile_definitions(dsp_spectrum_demo PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    DSP_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_fft_spectrum_256.json"
    DSP_METAL_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(dsp_spectrum_demo graphx_dynamic_nodes)
endif()

add_executable(dsp_fhss_iq_generator
    src/fhss_iq_generator.cpp
)
set_target_properties(dsp_fhss_iq_generator PROPERTIES
    OUTPUT_NAME "graphx-dsp-fhss-iq-generator"
)
target_link_libraries(dsp_fhss_iq_generator PRIVATE dsp dsp_fhss_iq_artifacts)

add_executable(dsp_fhss_phase2_characterize
    src/fhss_phase2_characterize.cpp
)
set_target_properties(dsp_fhss_phase2_characterize PROPERTIES
    OUTPUT_NAME "graphx-dsp-fhss-phase2-characterize"
)
target_link_libraries(dsp_fhss_phase2_characterize PRIVATE dsp)

add_executable(dsp_fhss_demo
    src/fhss_demo.cpp
)
set_target_properties(dsp_fhss_demo PROPERTIES
    OUTPUT_NAME "graphx-dsp-fhss-demo"
)

# The FHSS fixture graph is CPU-only, but the graph runtime resolves its
# accelerator token bootstrap symbols from libgpu.
target_link_libraries(dsp_fhss_demo PRIVATE dsp log4cxx graph gpu)
target_compile_definitions(dsp_fhss_demo PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    DSP_FHSS_CHANNELIZED_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
)
if(GRAPHX_BUILD_WEB_DASHBOARD)
    add_dependencies(dsp_fhss_demo dsp_fhss_dashboard_frontend)
    target_link_libraries(dsp_fhss_demo PRIVATE dsp_fhss_dashboard_api)
    target_compile_definitions(dsp_fhss_demo PRIVATE
        GRAPHX_BUILD_WEB_DASHBOARD=1
        DSP_FHSS_DASHBOARD_ASSET_DIRECTORY="${GRAPHX_DASHBOARD_DIST_DIR}"
        DSP_FHSS_DASHBOARD_INSTALLED_ASSET_DIRECTORY="${CMAKE_INSTALL_FULL_DATADIR}/graphx/fhss-dashboard"
    )
endif()
if(TARGET graphx_dynamic_nodes)
    add_dependencies(dsp_fhss_demo graphx_dynamic_nodes)
endif()

install(TARGETS dsp_spectrum_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS dsp_fhss_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(GRAPHX_BUILD_WEB_DASHBOARD)
    install(DIRECTORY "${GRAPHX_DASHBOARD_DIST_DIR}/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard)
    install(DIRECTORY dashboard/api/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/api
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY dashboard/operator/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/operator
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY dashboard/sigmf/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/sigmf)
    install(DIRECTORY dashboard/baseline/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/baseline)
    install(FILES tools/fhss_phase3_independent.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/operator)
    install(FILES
        ${CMAKE_SOURCE_DIR}/docs/graphx_dashboard_user_manual.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss_dashboard_phase8_manual_operator_test.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss_dashboard_phase8_security_support.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss_dashboard_phase8_architecture_recommendation.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss-dashboard-v2.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss_dashboard_phase1_operator_test.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/fhss_dashboard_frontend_policy.md
        ${CMAKE_SOURCE_DIR}/docs/dsp/adr/0001-fhss-dashboard-react-flow-elk.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/docs)
    install(FILES
        ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
        ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/config)
endif()

install(TARGETS dsp_fhss_iq_generator
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS dsp_fhss_phase2_characterize
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
