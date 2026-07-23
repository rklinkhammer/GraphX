#!/usr/bin/env bash

set -euo pipefail

SOURCE_ROOT=/workspace/GraphX
BUILD_ROOT="$SOURCE_ROOT/build-dashboard-operator"
EVIDENCE_ROOT=/workspace/evidence
CAPTURE_ROOT="$EVIDENCE_ROOT/captures"
DASHBOARD_PORT=18080
PROXY_PORT=8080

mkdir -p "$CAPTURE_ROOT"
cd "$EVIDENCE_ROOT"

"$BUILD_ROOT/examples/DSP/graphx-dsp-fhss-iq-generator" \
  --message-json "$SOURCE_ROOT/examples/DSP/fixtures/fhss_demo_messages.json" \
  --iq-output "$CAPTURE_ROOT/fhss_input.cf32" \
  --truth-output "$CAPTURE_ROOT/fhss_input.truth.json" \
  --sigmf-meta "$CAPTURE_ROOT/fhss_input.sigmf-meta" \
  --sample-format cf32_le \
  --force

test -s "$CAPTURE_ROOT/fhss_input.cf32"
test -s "$CAPTURE_ROOT/fhss_input.truth.json"
test -s "$CAPTURE_ROOT/fhss_input.sigmf-meta"

dashboard_pid=
proxy_pid=

cleanup() {
  if [[ -n "$proxy_pid" ]]; then
    kill -TERM "$proxy_pid" 2>/dev/null || true
    wait "$proxy_pid" 2>/dev/null || true
  fi
  if [[ -n "$dashboard_pid" ]]; then
    kill -INT "$dashboard_pid" 2>/dev/null || true
    wait "$dashboard_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

"$BUILD_ROOT/examples/DSP/graphx-dsp-fhss-demo" \
  --graph-config "$SOURCE_ROOT/libdsp/config/fhss_phase2_binary_iq_receiver.json" \
  --plugin-dir "$BUILD_ROOT/plugins" \
  --dashboard \
  --dashboard-host 127.0.0.1 \
  --dashboard-port "$DASHBOARD_PORT" &
dashboard_pid=$!

for _ in $(seq 1 100); do
  if curl --fail --silent \
      "http://127.0.0.1:${DASHBOARD_PORT}/healthz" \
      >/dev/null; then
    break
  fi
  if ! kill -0 "$dashboard_pid" 2>/dev/null; then
    wait "$dashboard_pid"
  fi
  sleep 0.1
done

curl --fail --silent \
  "http://127.0.0.1:${DASHBOARD_PORT}/healthz" >/dev/null

socat \
  "TCP-LISTEN:${PROXY_PORT},fork,reuseaddr,bind=0.0.0.0" \
  "TCP:127.0.0.1:${DASHBOARD_PORT}" &
proxy_pid=$!

echo "GraphX dashboard operator image revision: $(cat /opt/graphx/GRAPHX_REVISION)"
echo "Open http://127.0.0.1:8080/ on the Docker host."

wait "$dashboard_pid"
