// SPDX-License-Identifier: MIT

#include "accelgraph/SpectrumGraphNodes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "config/ConfigError.hpp"

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
#include <cuda_runtime_api.h>
#include <cufft.h>
#endif

namespace accelgraph {

namespace {

constexpr const char* kStrictFallbackDiagnostic =
    "Strict fallback policy forbids backend fallback for SpectrumAnalysis.";

AcceleratorError MakeSpectrumError(AcceleratorErrorCategory category,
                                   const char* operation,
                                   std::string diagnostic,
                                   AcceleratorBackend backend = AcceleratorBackend::Cpu,
                                   std::optional<int> native_code = std::nullopt) {
    AcceleratorError error;
    error.category = category;
    error.backend = backend;
    error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
    error.provider_id = AcceleratorProviderId{"spectrum.analysis"};
    error.operation = operation;
    error.diagnostic = std::move(diagnostic);
    error.native_error_code = native_code;
    return error;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string BackendToString(AcceleratorBackend backend) {
    switch (backend) {
    case AcceleratorBackend::Cpu:
        return "cpu";
    case AcceleratorBackend::Metal:
        return "metal";
    case AcceleratorBackend::Cuda:
        return "cuda";
    }
    return "cpu";
}

SpectrumRequestedBackend ParseRequestedBackend(const graph::JsonView& cfg) {
    const auto& raw = cfg.Raw();
    std::string backend = "auto";
    if (raw.contains("backend")) {
        if (!raw["backend"].is_string()) {
            throw graph::ConfigError("SpectrumAnalysisNode backend must be a string");
        }
        backend = ToLower(raw["backend"].get<std::string>());
    }

    if (backend == "cpu") {
        return SpectrumRequestedBackend::Cpu;
    }
    if (backend == "metal") {
        return SpectrumRequestedBackend::Metal;
    }
    if (backend == "cuda") {
        return SpectrumRequestedBackend::Cuda;
    }
    if (backend == "auto") {
        return SpectrumRequestedBackend::Auto;
    }

    throw graph::ConfigError("SpectrumAnalysisNode backend must be one of: cpu, metal, cuda, auto");
}

SpectrumFallbackPolicy ParseFallbackPolicy(const graph::JsonView& cfg) {
    const auto& raw = cfg.Raw();
    std::string policy = "strict";
    if (raw.contains("fallback_policy")) {
        if (!raw["fallback_policy"].is_string()) {
            throw graph::ConfigError("SpectrumAnalysisNode fallback_policy must be a string");
        }
        policy = ToLower(raw["fallback_policy"].get<std::string>());
    }

    if (policy == "strict") {
        return SpectrumFallbackPolicy::Strict;
    }
    if (policy == "allow") {
        return SpectrumFallbackPolicy::Allow;
    }

    throw graph::ConfigError("SpectrumAnalysisNode fallback_policy must be one of: strict, allow");
}

std::expected<void, AcceleratorError>
ValidateSpectrumRequest(const SpectrumAnalysisRequest& request, AcceleratorBackend backend) {
    if (request.complex_sample_count == 0) {
        return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::InvalidArgument,
                                                 "SpectrumAnalysis::Analyze",
                                                 "complex_sample_count must be greater than zero",
                                                 backend));
    }
    if (request.interleaved_iq.size() < request.complex_sample_count * 2U) {
        return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::InvalidArgument,
                                                 "SpectrumAnalysis::Analyze",
                                                 "interleaved_iq must contain 2 * complex_sample_count values",
                                                 backend));
    }
    if (request.sample_rate_hz <= 0.0) {
        return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::InvalidArgument,
                                                 "SpectrumAnalysis::Analyze",
                                                 "sample_rate_hz must be greater than zero",
                                                 backend));
    }
    return {};
}

class CpuSpectrumAnalysisOperation final : public ISpectrumAnalysisOperation {
public:
    [[nodiscard]] std::expected<SpectrumAnalysisResult, AcceleratorError>
    Analyze(const SpectrumAnalysisRequest& request) override {
        auto valid = ValidateSpectrumRequest(request, AcceleratorBackend::Cpu);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        const std::size_t n = request.complex_sample_count;
        const std::size_t max_bins = n / 2 + 1;
        const std::size_t bins = request.output_bins == 0 ? max_bins : std::min(request.output_bins, max_bins);

        SpectrumAnalysisResult result;
        result.magnitudes.resize(bins, 0.0f);
        result.execution_backend = AcceleratorBackend::Cpu;

        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
        for (std::size_t k = 0; k < bins; ++k) {
            std::complex<double> sum{0.0, 0.0};
            for (std::size_t sample_index = 0; sample_index < n; ++sample_index) {
                const std::complex<double> sample{
                    static_cast<double>(request.interleaved_iq[2 * sample_index]),
                    static_cast<double>(request.interleaved_iq[2 * sample_index + 1])};
                const double angle = -kTwoPi * static_cast<double>(k) *
                                     static_cast<double>(sample_index) / static_cast<double>(n);
                const std::complex<double> twiddle{std::cos(angle), std::sin(angle)};
                sum += sample * twiddle;
            }
            result.magnitudes[k] = static_cast<float>(std::abs(sum));
        }

        auto peak_it = std::max_element(result.magnitudes.begin(), result.magnitudes.end());
        result.peak_bin = peak_it == result.magnitudes.end()
                              ? 0
                              : static_cast<std::size_t>(std::distance(result.magnitudes.begin(), peak_it));
        const double bin_hz = request.sample_rate_hz / static_cast<double>(n);
        result.peak_frequency_hz = static_cast<double>(result.peak_bin) * bin_hz;
        return result;
    }
};

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE

std::string BuildCudaDiagnostic(const char* prefix, cudaError_t code) {
    return std::string(prefix) + " " + cudaGetErrorName(code) + ": " + cudaGetErrorString(code);
}

class CudaSpectrumAnalysisOperation final : public ISpectrumAnalysisOperation {
public:
    static std::expected<std::unique_ptr<CudaSpectrumAnalysisOperation>, AcceleratorError>
    Create(int device_ordinal) {
        CudaAcceleratorProvider provider;
        AcceleratorSessionCreateRequest request;
        request.requested_device = AcceleratorDeviceId{"cuda:" + std::to_string(device_ordinal)};
        auto session = provider.CreateSession(request);
        if (!session.has_value()) {
            return std::unexpected(session.error());
        }

        return std::make_unique<CudaSpectrumAnalysisOperation>(device_ordinal);
    }

    explicit CudaSpectrumAnalysisOperation(int device_ordinal)
        : device_ordinal_(device_ordinal) {}

    [[nodiscard]] std::expected<SpectrumAnalysisResult, AcceleratorError>
    Analyze(const SpectrumAnalysisRequest& request) override {
        auto valid = ValidateSpectrumRequest(request, AcceleratorBackend::Cuda);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        const std::size_t n = request.complex_sample_count;
        const std::size_t max_bins = n / 2 + 1;
        const std::size_t bins = request.output_bins == 0 ? max_bins : std::min(request.output_bins, max_bins);

        std::vector<cufftComplex> host_iq(n);
        for (std::size_t sample_index = 0; sample_index < n; ++sample_index) {
            host_iq[sample_index] = cufftComplex{request.interleaved_iq[2 * sample_index],
                                                 request.interleaved_iq[2 * sample_index + 1]};
        }

        const auto set_device_error = cudaSetDevice(device_ordinal_);
        if (set_device_error != cudaSuccess) {
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::Unavailable,
                                                     "SpectrumAnalysis::Analyze",
                                                     BuildCudaDiagnostic("CUDA set-device failure.", set_device_error),
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(set_device_error)));
        }

        cufftComplex* device_iq = nullptr;
        const std::size_t iq_bytes = host_iq.size() * sizeof(cufftComplex);

        const auto malloc_iq_error = cudaMalloc(reinterpret_cast<void**>(&device_iq), iq_bytes);
        if (malloc_iq_error != cudaSuccess) {
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::AllocationFailed,
                                                     "SpectrumAnalysis::Analyze",
                                                     BuildCudaDiagnostic("CUDA IQ allocation failure.", malloc_iq_error),
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(malloc_iq_error)));
        }

        auto release_device_memory = [&]() {
            if (device_iq != nullptr) {
                cudaFree(device_iq);
            }
        };

        const auto memcpy_h2d_error = cudaMemcpy(device_iq,
                                                 host_iq.data(),
                                                 iq_bytes,
                                                 cudaMemcpyHostToDevice);
        if (memcpy_h2d_error != cudaSuccess) {
            release_device_memory();
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::TransferFailed,
                                                     "SpectrumAnalysis::Analyze",
                                                     BuildCudaDiagnostic("CUDA H2D copy failure.", memcpy_h2d_error),
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(memcpy_h2d_error)));
        }

        cufftHandle plan = 0;
        const auto plan_error = cufftPlan1d(&plan, static_cast<int>(n), CUFFT_C2C, 1);
        if (plan_error != CUFFT_SUCCESS) {
            release_device_memory();
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::BackendFailure,
                                                     "SpectrumAnalysis::Analyze",
                                                     "cuFFT plan creation failure.",
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(plan_error)));
        }

        const auto exec_error = cufftExecC2C(plan, device_iq, device_iq, CUFFT_FORWARD);
        if (exec_error != CUFFT_SUCCESS) {
            cufftDestroy(plan);
            release_device_memory();
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::BackendFailure,
                                                     "SpectrumAnalysis::Analyze",
                                                     "cuFFT execution failure.",
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(exec_error)));
        }

        const auto sync_error = cudaDeviceSynchronize();
        if (sync_error != cudaSuccess) {
            cufftDestroy(plan);
            release_device_memory();
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::BackendFailure,
                                                     "SpectrumAnalysis::Analyze",
                                                     BuildCudaDiagnostic("CUDA FFT sync failure.", sync_error),
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(sync_error)));
        }

        std::vector<cufftComplex> host_fft(n);
        const auto memcpy_d2h_error = cudaMemcpy(host_fft.data(),
                                                 device_iq,
                                                 iq_bytes,
                                                 cudaMemcpyDeviceToHost);
        if (memcpy_d2h_error != cudaSuccess) {
            cufftDestroy(plan);
            release_device_memory();
            return std::unexpected(MakeSpectrumError(AcceleratorErrorCategory::TransferFailed,
                                                     "SpectrumAnalysis::Analyze",
                                                     BuildCudaDiagnostic("CUDA D2H copy failure.", memcpy_d2h_error),
                                                     AcceleratorBackend::Cuda,
                                                     static_cast<int>(memcpy_d2h_error)));
        }

        cufftDestroy(plan);
        release_device_memory();

        SpectrumAnalysisResult result;
        result.magnitudes.resize(bins, 0.0f);
        for (std::size_t bin = 0; bin < bins; ++bin) {
            const auto real = static_cast<double>(host_fft[bin].x);
            const auto imag = static_cast<double>(host_fft[bin].y);
            result.magnitudes[bin] = static_cast<float>(std::sqrt(real * real + imag * imag));
        }

        auto peak_it = std::max_element(result.magnitudes.begin(), result.magnitudes.end());
        result.peak_bin = peak_it == result.magnitudes.end()
                              ? 0
                              : static_cast<std::size_t>(std::distance(result.magnitudes.begin(), peak_it));
        const double bin_hz = request.sample_rate_hz / static_cast<double>(n);
        result.peak_frequency_hz = static_cast<double>(result.peak_bin) * bin_hz;
        result.execution_backend = AcceleratorBackend::Cuda;
        return result;
    }

private:
    int device_ordinal_{0};
};

#endif

std::expected<std::unique_ptr<ISpectrumAnalysisOperation>, AcceleratorError>
CreateOperationForBackend(SpectrumRequestedBackend requested,
                          SpectrumFallbackPolicy fallback_policy,
                          int cuda_device_ordinal,
                          AcceleratorBackend& execution_backend,
                          bool& fallback_used,
                          std::string& fallback_reason) {
    auto use_cpu = [&]() -> std::expected<std::unique_ptr<ISpectrumAnalysisOperation>, AcceleratorError> {
        execution_backend = AcceleratorBackend::Cpu;
        return std::make_unique<CpuSpectrumAnalysisOperation>();
    };

    if (requested == SpectrumRequestedBackend::Cpu) {
        fallback_used = false;
        fallback_reason = "none";
        return use_cpu();
    }

    if (requested == SpectrumRequestedBackend::Metal) {
        const auto unavailable = MakeSpectrumError(AcceleratorErrorCategory::Unavailable,
                                                   "SpectrumAnalysisNode::Configure",
                                                   "Metal SpectrumAnalysis is unavailable on this host/build.",
                                                   AcceleratorBackend::Metal);
        if (fallback_policy == SpectrumFallbackPolicy::Allow) {
            fallback_used = true;
            fallback_reason = unavailable.diagnostic;
            return use_cpu();
        }
        return std::unexpected(unavailable);
    }

    auto try_cuda = [&]() -> std::expected<std::unique_ptr<ISpectrumAnalysisOperation>, AcceleratorError> {
#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
        auto cuda_op = CudaSpectrumAnalysisOperation::Create(cuda_device_ordinal);
        if (!cuda_op.has_value()) {
            return std::unexpected(cuda_op.error());
        }
        execution_backend = AcceleratorBackend::Cuda;
        return std::move(cuda_op.value());
#else
        return std::unexpected(MakeSpectrumError(
            AcceleratorErrorCategory::Unavailable,
            "SpectrumAnalysisNode::Configure",
            "CUDA SpectrumAnalysis unavailable: ACCELGRAPH_ENABLE_CUDA, ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE, "
            "or ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE is OFF.",
            AcceleratorBackend::Cuda));
#endif
    };

    if (requested == SpectrumRequestedBackend::Cuda) {
        auto cuda_op = try_cuda();
        if (cuda_op.has_value()) {
            fallback_used = false;
            fallback_reason = "none";
            return cuda_op;
        }
        if (fallback_policy == SpectrumFallbackPolicy::Allow) {
            fallback_used = true;
            fallback_reason = cuda_op.error().diagnostic;
            return use_cpu();
        }
        return std::unexpected(cuda_op.error());
    }

    // Auto selection prefers CUDA when available and falls back to CPU.
    auto cuda_op = try_cuda();
    if (cuda_op.has_value()) {
        fallback_used = false;
        fallback_reason = "none";
        return cuda_op;
    }

    fallback_used = false;
    fallback_reason = "none";
    return use_cpu();
}

}  // namespace

void SineWaveSourceNode::Configure(const graph::JsonView& cfg) {
    const auto& raw = cfg.Raw();

    if (raw.contains("sample_rate_hz")) {
        if (!raw["sample_rate_hz"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode sample_rate_hz must be a number");
        }
        sample_rate_hz_ = raw["sample_rate_hz"].get<double>();
    }
    if (raw.contains("tone_frequency_hz")) {
        if (!raw["tone_frequency_hz"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode tone_frequency_hz must be a number");
        }
        tone_frequency_hz_ = raw["tone_frequency_hz"].get<double>();
    }
    if (raw.contains("amplitude")) {
        if (!raw["amplitude"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode amplitude must be a number");
        }
        amplitude_ = raw["amplitude"].get<double>();
    }
    if (raw.contains("phase_radians")) {
        if (!raw["phase_radians"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode phase_radians must be a number");
        }
        phase_radians_ = raw["phase_radians"].get<double>();
    }
    if (raw.contains("complex_sample_count")) {
        if (!raw["complex_sample_count"].is_number_integer() || raw["complex_sample_count"].get<int>() <= 0) {
            throw graph::ConfigError("SineWaveSourceNode complex_sample_count must be a positive integer");
        }
        complex_sample_count_ = static_cast<std::size_t>(raw["complex_sample_count"].get<int>());
    }
    if (raw.contains("frame_count")) {
        if (!raw["frame_count"].is_number_integer() || raw["frame_count"].get<int>() <= 0) {
            throw graph::ConfigError("SineWaveSourceNode frame_count must be a positive integer");
        }
        frame_count_ = static_cast<std::size_t>(raw["frame_count"].get<int>());
    }

    if (sample_rate_hz_ <= 0.0) {
        throw graph::ConfigError("SineWaveSourceNode sample_rate_hz must be greater than zero");
    }

    produced_frames_ = 0;
}

std::optional<IqFrameToken> SineWaveSourceNode::Produce(std::integral_constant<std::size_t, 0>) {
    if (produced_frames_ >= frame_count_) {
        return std::nullopt;
    }

    IqFrameToken token;
    token.complex_sample_count = complex_sample_count_;
    token.sample_rate_hz = sample_rate_hz_;
    token.tone_frequency_hz = tone_frequency_hz_;
    token.frame_index = static_cast<std::uint64_t>(produced_frames_);
    token.interleaved_iq.resize(complex_sample_count_ * 2U, 0.0f);

    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    const double phase_step = kTwoPi * tone_frequency_hz_ / sample_rate_hz_;
    const double frame_phase_offset = static_cast<double>(produced_frames_) *
                                      static_cast<double>(complex_sample_count_) * phase_step;

    for (std::size_t sample_index = 0; sample_index < complex_sample_count_; ++sample_index) {
        const double phase = phase_radians_ + frame_phase_offset + static_cast<double>(sample_index) * phase_step;
        token.interleaved_iq[2 * sample_index] = static_cast<float>(amplitude_ * std::cos(phase));
        token.interleaved_iq[2 * sample_index + 1] = static_cast<float>(amplitude_ * std::sin(phase));
    }

    ++produced_frames_;
    return token;
}

void SpectrumAnalysisNode::Configure(const graph::JsonView& cfg) {
    const auto& raw = cfg.Raw();

    requested_backend_ = ParseRequestedBackend(cfg);
    fallback_policy_ = ParseFallbackPolicy(cfg);

    output_bins_ = 128;
    if (raw.contains("output_bins")) {
        if (!raw["output_bins"].is_number_integer() || raw["output_bins"].get<int>() <= 0) {
            throw graph::ConfigError("SpectrumAnalysisNode output_bins must be a positive integer");
        }
        output_bins_ = static_cast<std::size_t>(raw["output_bins"].get<int>());
    }

    cuda_device_ordinal_ = 0;
    if (raw.contains("cuda_device_ordinal")) {
        if (!raw["cuda_device_ordinal"].is_number_integer() || raw["cuda_device_ordinal"].get<int>() < 0) {
            throw graph::ConfigError("SpectrumAnalysisNode cuda_device_ordinal must be a non-negative integer");
        }
        cuda_device_ordinal_ = raw["cuda_device_ordinal"].get<int>();
    }

    auto op = CreateOperationForBackend(requested_backend_,
                                        fallback_policy_,
                                        cuda_device_ordinal_,
                                        execution_backend_,
                                        fallback_used_,
                                        fallback_reason_);
    if (!op.has_value()) {
        throw graph::ConfigError(op.error().diagnostic + " " + kStrictFallbackDiagnostic);
    }

    operation_ = std::move(op.value());
}

std::optional<SpectrumFrameToken> SpectrumAnalysisNode::Transfer(
    const IqFrameToken& iq,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!operation_) {
        return std::nullopt;
    }

    SpectrumAnalysisRequest request;
    request.interleaved_iq = iq.interleaved_iq;
    request.complex_sample_count = iq.complex_sample_count;
    request.sample_rate_hz = iq.sample_rate_hz;
    request.output_bins = output_bins_;
    request.requested_backend = requested_backend_;
    request.fallback_policy = fallback_policy_;

    auto analyzed = operation_->Analyze(request);
    if (!analyzed.has_value()) {
        throw graph::ConfigError(analyzed.error().diagnostic);
    }

    SpectrumFrameToken output;
    output.magnitudes = std::move(analyzed->magnitudes);
    output.fft_size = iq.complex_sample_count;
    output.peak_bin = analyzed->peak_bin;
    output.peak_frequency_hz = analyzed->peak_frequency_hz;
    output.requested_backend = (requested_backend_ == SpectrumRequestedBackend::Auto)
                                   ? "auto"
                                   : (requested_backend_ == SpectrumRequestedBackend::Cuda
                                          ? "cuda"
                                          : (requested_backend_ == SpectrumRequestedBackend::Metal ? "metal" : "cpu"));
    output.execution_backend = BackendToString(analyzed->execution_backend);
    output.fallback_used = fallback_used_ || analyzed->fallback_used;
    output.fallback_reason = fallback_used_ ? fallback_reason_ : analyzed->fallback_reason;
    return output;
}

void SpectrumSinkNode::Configure(const graph::JsonView& cfg) {
    if (!cfg.Raw().is_object()) {
        throw graph::ConfigError("SpectrumSinkNode configuration must be a JSON object");
    }
}

bool SpectrumSinkNode::Consume(const SpectrumFrameToken& spectrum,
                               std::integral_constant<std::size_t, 0>) {
    last_spectrum_ = spectrum;
    ++captured_count_;
    return true;
}

}  // namespace accelgraph
