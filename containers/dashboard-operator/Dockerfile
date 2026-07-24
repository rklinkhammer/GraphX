# syntax=docker/dockerfile:1.7

FROM node:26.5.0-bookworm-slim AS node_runtime

FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG GRAPHX_REVISION=unrecorded

COPY --from=node_runtime /usr/local/ /usr/local/

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        curl \
        gpg \
    && curl --fail --show-error --silent \
        https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor --output /usr/share/keyrings/kitware-archive-keyring.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' \
        > /etc/apt/sources.list.d/kitware.list \
    && apt-get update \
    && apt-get install --yes --no-install-recommends \
        ccache \
        cmake \
        g++-14 \
        gcc-14 \
        libboost-dev \
        libeigen3-dev \
        libgtest-dev \
        libhdf5-dev \
        liblog4cxx-dev \
        ninja-build \
        nlohmann-json3-dev \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        socat \
    && rm -rf /var/lib/apt/lists/*

COPY examples/DSP/dashboard/api/requirements-contracts.lock \
    /opt/graphx/requirements-contracts.lock
RUN python3 -m venv /opt/graphx/contracts \
    && /opt/graphx/contracts/bin/python -m pip install --no-cache-dir \
        --requirement /opt/graphx/requirements-contracts.lock \
    && /opt/graphx/contracts/bin/python -m pip check

WORKDIR /workspace/GraphX
COPY . .

ENV CC=gcc-14 \
    CXX=g++-14 \
    GRAPHX_DASHBOARD_CONTRACT_PYTHON=/opt/graphx/contracts/bin/python

RUN node --version \
    && npm --version \
    && npm ci --ignore-scripts \
        --prefix examples/DSP/dashboard/frontend \
    && npm run format:check --prefix examples/DSP/dashboard/frontend \
    && npm run typecheck --prefix examples/DSP/dashboard/frontend \
    && npm test --prefix examples/DSP/dashboard/frontend

RUN cmake -S . -B build-dashboard-operator -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=26 \
        -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
        -DGRAPHX_BUILD_EXAMPLES_SAR=OFF \
        -DENABLE_CUDA_GRAPH_NODES=OFF \
        -DENABLE_SYCL_GRAPH_NODES=OFF \
        -DENABLE_METAL_GRAPH_NODES=OFF \
        -DENABLE_METAL_NATIVE_RUNTIME=OFF \
        -DGRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=OFF \
        -DGRAPHX_FHSS_SOURCE_REVISION_OVERRIDE=${GRAPHX_REVISION} \
        -DGRAPHX_FHSS_SOURCE_DIRTY_OVERRIDE=true \
        -DGRAPHX_DASHBOARD_CONTRACT_PYTHON=/opt/graphx/contracts/bin/python \
    && cmake --build build-dashboard-operator --parallel 2 --target \
        dsp_fhss_demo dsp_fhss_iq_generator test_dsp_example_unit \
    && build-dashboard-operator/examples/DSP/test/test_dsp_example_unit \
        --gtest_filter='GraphRuntimeSessionIdentityTest.*:FhssDashboardServerContractTest.MetricsSchemasAreStableWithDefaultPayloads' \
    && ctest --test-dir build-dashboard-operator --output-on-failure -R \
        'fhss_dashboard_(api_contracts|phase0_baseline|phase1_frontend|v2_phase3_identity)'

RUN printf '%s\n' "${GRAPHX_REVISION}" > /opt/graphx/GRAPHX_REVISION

COPY containers/dashboard-operator/run-dashboard.sh \
    /usr/local/bin/graphx-dashboard-operator
RUN chmod 0755 /usr/local/bin/graphx-dashboard-operator

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/graphx-dashboard-operator"]
