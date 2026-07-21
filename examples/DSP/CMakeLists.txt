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
    target_link_libraries(dsp_fhss_demo PRIVATE dsp_fhss_dashboard_api)
    target_compile_definitions(dsp_fhss_demo PRIVATE
        GRAPHX_BUILD_WEB_DASHBOARD=1
        DSP_FHSS_DASHBOARD_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/examples/DSP/dashboard"
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
    install(FILES dashboard/index.html dashboard/fhss_transport_state.js
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard)
    install(DIRECTORY dashboard/api/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/api
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY dashboard/operator/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/operator
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
    install(DIRECTORY dashboard/sigmf/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/sigmf)
    install(FILES tools/fhss_phase3_independent.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/fhss-dashboard/operator)
    install(FILES
        ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/config)
endif()

install(TARGETS dsp_fhss_iq_generator
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS dsp_fhss_phase2_characterize
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
