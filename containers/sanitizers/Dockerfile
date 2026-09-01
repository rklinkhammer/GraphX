# syntax=docker/dockerfile:1.7

FROM node:26.5.0-bookworm-slim AS node_runtime

FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

COPY --from=node_runtime /usr/local/ /usr/local/

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        ccache \
        cmake \
        curl \
        g++-14 \
        gcc-14 \
        git \
        gpg \
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
    && curl --fail --show-error --silent \
        https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor --output /usr/share/keyrings/kitware-archive-keyring.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' \
        > /etc/apt/sources.list.d/kitware.list \
    && apt-get update \
    && apt-get install --yes --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/* \
    && git clone --depth 1 --branch v3.14.0 \
        https://github.com/catchorg/Catch2.git /opt/Catch2 \
    && cmake -S /opt/Catch2 -B /opt/Catch2/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DCATCH_ENABLE_ALL_WARNINGS=OFF \
    && cmake --build /opt/Catch2/build --parallel 2 \
    && cmake --install /opt/Catch2/build --prefix /usr/local \
    && rm -rf /opt/Catch2

COPY examples/DSP/dashboard/api/requirements-contracts.lock \
    /opt/graphx/requirements-contracts.lock
RUN python3 -m venv /opt/graphx/contracts \
    && /opt/graphx/contracts/bin/python -m pip install --no-cache-dir \
        --requirement /opt/graphx/requirements-contracts.lock \
    && /opt/graphx/contracts/bin/python -m pip check

COPY containers/sanitizers/requirements-sar.lock \
    /opt/graphx/requirements-sar.lock
COPY containers/sanitizers/verify-python-lock.py \
    /opt/graphx/verify-python-lock.py
RUN python3 -m pip install --no-cache-dir --break-system-packages \
        --target /opt/graphx/sar-packages \
        --requirement /opt/graphx/requirements-sar.lock \
    && PYTHONPATH=/opt/graphx/sar-packages python3 \
        /opt/graphx/verify-python-lock.py \
        /opt/graphx/requirements-sar.lock /opt/graphx/sar-packages \
    && PYTHONPATH=/opt/graphx/sar-packages python3 -m pip check \
    && PYTHONPATH=/opt/graphx/sar-packages python3 -c \
        'import h5py, matplotlib, numpy, sarpy, scipy; print(numpy.__version__, sarpy.__version__)'

WORKDIR /workspace/GraphX
COPY . .

ENV CC=gcc-14 \
    CXX=g++-14 \
    PYTHONPATH=/opt/graphx/sar-packages \
    GRAPHX_DASHBOARD_CONTRACT_PYTHON=/opt/graphx/contracts/bin/python \
    GRAPHX_SAR_PYTHON=/usr/bin/python3 \
    LOG4CXX_CONFIGURATION=/workspace/GraphX/containers/sanitizers/log4cxx.properties \
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
    LSAN_OPTIONS=exitcode=23:print_suppressions=0:report_objects=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

COPY containers/sanitizers/run-sanitizers.sh /usr/local/bin/graphx-sanitizers
COPY containers/sanitizers/verify-instrumentation.py /usr/local/bin/verify-instrumentation.py
RUN chmod 0755 /usr/local/bin/graphx-sanitizers /usr/local/bin/verify-instrumentation.py \
    && node --version \
    && npm --version \
    && cmake --version \
    && g++-14 --version

ENTRYPOINT ["/usr/local/bin/graphx-sanitizers"]
CMD ["focused"]
