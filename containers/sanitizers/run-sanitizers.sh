#!/usr/bin/env bash
set -euo pipefail

readonly mode="${1:-focused}"
readonly preset="ninja-debug-linux-sanitized"
readonly build_dir="/workspace/GraphX/build-ninja/${preset}"
readonly jobs="${GRAPHX_BUILD_JOBS:-1}"
readonly focused_tests='^(libgraph_unit|dsp_example_unit|phase2a_configuration|fhss_dashboard_api_contracts|fhss_phase2_candidate_characterization_smoke|fhss_fixture_topology_generator|graph_cli_help|graph_dashboard_help|graph_cli_node_count|graph_tools_executable_contract)$'

if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "GRAPHX_BUILD_JOBS must be a positive integer" >&2
    exit 64
fi
export CMAKE_BUILD_PARALLEL_LEVEL="${jobs}"

configure() {
    cmake --fresh --preset "${preset}" \
        -DGRAPHX_DASHBOARD_CONTRACT_PYTHON="${GRAPHX_DASHBOARD_CONTRACT_PYTHON}"
}

build_all() {
    cmake --build --preset build-debug-linux-sanitized --parallel "${jobs}"
    verify-instrumentation.py "${build_dir}/compile_commands.json"
}

check_frontend() {
    npm ci --ignore-scripts --prefix libgraph/web
    npm run typecheck --prefix libgraph/web
    npm test --prefix libgraph/web
    npm run check:assets --prefix libgraph/web
}

run_focused() {
    ctest --test-dir "${build_dir}" \
        --tests-regex "${focused_tests}" \
        --output-on-failure \
        --parallel 1
}

run_full() {
    ctest --test-dir "${build_dir}" \
        --output-on-failure \
        --parallel 1
}

case "${mode}" in
    focused)
        check_frontend
        configure
        build_all
        run_focused
        ;;
    full)
        check_frontend
        configure
        build_all
        run_full
        ;;
    frontend)
        check_frontend
        ;;
    shell)
        exec /bin/bash
        ;;
    *)
        echo "usage: graphx-sanitizers {focused|full|frontend|shell}" >&2
        exit 64
        ;;
esac
