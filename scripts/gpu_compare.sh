!#/bin/bash
cmake --preset ninja-debug-metal-native
cmake --build --preset build-debug-metal-native --target dsp_spectrum_demo
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins
  
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins \
  --summary-json "$tmpdir/summary.json"

echo "Summary JSON:"
cat "$tmpdir/summary.json"

tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --warmup-iterations 1 \
  --measured-iterations 3 \
  --executor-timeout-s 8 \
  --report-json "$tmpdir/dsp_cpu_vs_metal_report.json"

echo "CPU vs Metal Report JSON:"
cat "$tmpdir/dsp_cpu_vs_metal_report.json"

GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1 \
GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO=1.10 \
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --warmup-iterations 1 \
  --measured-iterations 3 \
  --executor-timeout-s 8 \
  --report-json "$tmpdir/dsp_cpu_vs_metal_strict_report.json"

echo "CPU vs Metal Strict Report JSON:"
cat "$tmpdir/dsp_cpu_vs_metal_strict_report.json"
