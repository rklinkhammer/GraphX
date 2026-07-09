// SPDX-License-Identifier: MIT

#include "accelgraph/SpectrumGraphNodes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "config/ConfigError.hpp"

namespace accelgraph {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::expected<void, AcceleratorError> RunDirectDft(const DeterministicIqPacket& input,
                                                   MagnitudeSpectrumPacket& output,
                                                   AcceleratorBackend backend,
                                                   const char* operation) {
    const std::size_t fft_size = input.sample_count;
    const std::size_t bin_count = fft_size / 2;

    output.magnitudes.assign(bin_count, 0.0F);
    output.fft_size = fft_size;
    output.sample_rate_hz = input.sample_rate_hz;
    output.packet_number = input.packet_number;

    double best_magnitude = -std::numeric_limits<double>::infinity();
    std::size_t best_bin = 0;

    for (std::size_t bin = 0; bin < bin_count; ++bin) {
        std::complex<double> accum{0.0, 0.0};
        for (std::size_t n = 0; n < fft_size; ++n) {
            const auto sample = std::complex<double>{
                static_cast<double>(input.i_samples[n]),
                static_cast<double>(input.q_samples[n])};
            const double angle = -kTwoPi * static_cast<double>(bin) * static_cast<double>(n) /
                                 static_cast<double>(fft_size);
            const auto twiddle = std::complex<double>{std::cos(angle), std::sin(angle)};
            accum += sample * twiddle;
        }

        const double magnitude = std::abs(accum) / static_cast<double>(fft_size);
        output.magnitudes[bin] = static_cast<float>(magnitude);
        if (magnitude > best_magnitude) {
            best_magnitude = magnitude;
            best_bin = bin;
        }
    }

    output.peak_bin = best_bin;
    output.peak_magnitude = output.magnitudes[best_bin];
    output.peak_frequency_hz =
        static_cast<double>(best_bin) * output.sample_rate_hz / static_cast<double>(fft_size);

    if (output.magnitudes.empty()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::BackendFailure;
        error.backend = backend;
        error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
        error.operation = operation;
        error.diagnostic = "spectrum output is empty after DFT";
        return std::unexpected(error);
    }

    return {};
}

std::expected<AcceleratorBackend, std::string> ParseSpectrumBackend(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.contains("backend") || !json["backend"].is_string()) {
        return std::unexpected(std::string("backend must be a string (cpu|metal)"));
    }

    const std::string backend = json["backend"].get<std::string>();
    if (backend == "cpu") {
        return AcceleratorBackend::Cpu;
    }
    if (backend == "metal") {
        return AcceleratorBackend::Metal;
    }
    if (backend == "cuda") {
        return AcceleratorBackend::Cuda;
    }

    return std::unexpected(std::string("backend must be one of: cpu, metal, cuda"));
}

}  // namespace

void SineWaveSourceNode::Configure(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        throw graph::ConfigError("SineWaveSourceNode configuration must be a JSON object");
    }

    int sample_count = 256;
    double sample_rate_hz = 48000.0;
    double tone_frequency_hz = 1000.0;
    float amplitude = 1.0F;
    float phase_radians = 0.0F;
    std::uint64_t packet_number = 1;

    if (json.contains("sample_count")) {
        if (!json["sample_count"].is_number_integer() || json["sample_count"].get<int>() < 2) {
            throw graph::ConfigError("SineWaveSourceNode sample_count must be an integer >= 2");
        }
        sample_count = json["sample_count"].get<int>();
    }

    if (json.contains("sample_rate_hz")) {
        if (!json["sample_rate_hz"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode sample_rate_hz must be numeric");
        }
        sample_rate_hz = json["sample_rate_hz"].get<double>();
        if (sample_rate_hz <= 0.0) {
            throw graph::ConfigError("SineWaveSourceNode sample_rate_hz must be > 0");
        }
    }

    if (json.contains("tone_frequency_hz")) {
        if (!json["tone_frequency_hz"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode tone_frequency_hz must be numeric");
        }
        tone_frequency_hz = json["tone_frequency_hz"].get<double>();
    }

    if (json.contains("amplitude")) {
        if (!json["amplitude"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode amplitude must be numeric");
        }
        amplitude = json["amplitude"].get<float>();
        if (amplitude < 0.0F) {
            throw graph::ConfigError("SineWaveSourceNode amplitude must be >= 0");
        }
    }

    if (json.contains("phase_radians")) {
        if (!json["phase_radians"].is_number()) {
            throw graph::ConfigError("SineWaveSourceNode phase_radians must be numeric");
        }
        phase_radians = json["phase_radians"].get<float>();
    }

    if (json.contains("packet_number")) {
        if (!json["packet_number"].is_number_integer() || json["packet_number"].get<long long>() < 0) {
            throw graph::ConfigError("SineWaveSourceNode packet_number must be a non-negative integer");
        }
        packet_number = static_cast<std::uint64_t>(json["packet_number"].get<long long>());
    }

    configured_packet_ = DeterministicIqPacket{};
    configured_packet_.sample_count = static_cast<std::size_t>(sample_count);
    configured_packet_.packet_number = packet_number;
    configured_packet_.sample_rate_hz = sample_rate_hz;
    configured_packet_.tone_frequency_hz = tone_frequency_hz;
    configured_packet_.amplitude = amplitude;
    configured_packet_.phase_radians = phase_radians;
    configured_packet_.i_samples.resize(configured_packet_.sample_count);
    configured_packet_.q_samples.resize(configured_packet_.sample_count);

    for (std::size_t n = 0; n < configured_packet_.sample_count; ++n) {
        const double phase = kTwoPi * tone_frequency_hz * static_cast<double>(n) / sample_rate_hz +
                             static_cast<double>(phase_radians);
        configured_packet_.i_samples[n] = amplitude * static_cast<float>(std::cos(phase));
        configured_packet_.q_samples[n] = amplitude * static_cast<float>(std::sin(phase));
    }

    produced_ = false;
}

std::optional<DeterministicIqPacket> SineWaveSourceNode::Produce(std::integral_constant<std::size_t, 0>) {
    if (produced_ || configured_packet_.sample_count == 0) {
        return std::nullopt;
    }

    produced_ = true;
    return configured_packet_;
}

void SpectrumAnalysisNode::Configure(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        throw graph::ConfigError("SpectrumAnalysisNode configuration must be a JSON object");
    }

    auto backend = ParseSpectrumBackend(cfg);
    if (!backend.has_value()) {
        throw graph::ConfigError(backend.error());
    }

    requested_backend_ = backend.value();
    selected_backend_ = requested_backend_;
    strict_fallback_ = true;
    used_fallback_ = false;
    fallback_diagnostic_.clear();
    metal_session_.reset();
    cuda_session_.reset();
    cuda_device_ordinal_ = 0;

    if (json.contains("strict_fallback")) {
        if (!json["strict_fallback"].is_boolean()) {
            throw graph::ConfigError("SpectrumAnalysisNode strict_fallback must be boolean");
        }
        strict_fallback_ = json["strict_fallback"].get<bool>();
    }

    if (json.contains("fallback_policy")) {
        if (!json["fallback_policy"].is_string()) {
            throw graph::ConfigError("SpectrumAnalysisNode fallback_policy must be string (strict|allow)");
        }
        const std::string policy = json["fallback_policy"].get<std::string>();
        if (policy == "strict") {
            strict_fallback_ = true;
        } else if (policy == "allow") {
            strict_fallback_ = false;
        } else {
            throw graph::ConfigError("SpectrumAnalysisNode fallback_policy must be one of: strict, allow");
        }
    }

    if (json.contains("cuda_device_ordinal")) {
        if (!json["cuda_device_ordinal"].is_number_integer() || json["cuda_device_ordinal"].get<int>() < 0) {
            throw graph::ConfigError("SpectrumAnalysisNode cuda_device_ordinal must be a non-negative integer");
        }
        cuda_device_ordinal_ = json["cuda_device_ordinal"].get<int>();
    }

    if (requested_backend_ != AcceleratorBackend::Metal) {
        if (requested_backend_ != AcceleratorBackend::Cuda) {
            selected_backend_ = AcceleratorBackend::Cpu;
            return;
        }

        CudaAcceleratorProvider provider;
        AcceleratorSessionCreateRequest request;
        request.requested_device = AcceleratorDeviceId{"cuda:" + std::to_string(cuda_device_ordinal_)};
        auto session = provider.CreateSession(request);
        if (session.has_value()) {
            cuda_session_ = session.value();
            selected_backend_ = AcceleratorBackend::Cuda;
            return;
        }

        fallback_diagnostic_ = session.error().diagnostic;
        if (strict_fallback_) {
            throw graph::ConfigError(fallback_diagnostic_);
        }

        selected_backend_ = AcceleratorBackend::Cpu;
        used_fallback_ = true;
        return;
    }

    MetalAcceleratorProvider provider;
    auto session = provider.CreateSession(AcceleratorSessionCreateRequest{});
    if (session.has_value()) {
        metal_session_ = session.value();
        selected_backend_ = AcceleratorBackend::Metal;
        return;
    }

    fallback_diagnostic_ = session.error().diagnostic;
    if (strict_fallback_) {
        throw graph::ConfigError(fallback_diagnostic_);
    }

    selected_backend_ = AcceleratorBackend::Cpu;
    used_fallback_ = true;
}

std::optional<MagnitudeSpectrumPacket> SpectrumAnalysisNode::Transfer(
    const DeterministicIqPacket& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto result = Execute(input);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return result.value();
}

std::expected<MagnitudeSpectrumPacket, AcceleratorError>
SpectrumAnalysisNode::Execute(const DeterministicIqPacket& input) const {
    if (input.sample_count < 2 || input.i_samples.size() != input.q_samples.size() ||
        input.sample_count != input.i_samples.size()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.backend = selected_backend_;
        error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
        error.operation = "SpectrumAnalysisNode::Execute";
        error.diagnostic = "deterministic IQ packet is invalid";
        return std::unexpected(error);
    }

    MagnitudeSpectrumPacket output;
    output.requested_backend = requested_backend_;
    output.selected_backend = selected_backend_;
    output.used_fallback = used_fallback_;
    output.fallback_diagnostic = fallback_diagnostic_;

    if (selected_backend_ == AcceleratorBackend::Cpu) {
        auto cpu_result = RunDirectDft(input,
                                       output,
                                       AcceleratorBackend::Cpu,
                                       "SpectrumAnalysisNode::ExecuteCpu");
        if (!cpu_result.has_value()) {
            return std::unexpected(cpu_result.error());
        }
        return output;
    }

    if (selected_backend_ == AcceleratorBackend::Metal) {
        if (!metal_session_) {
            AcceleratorError error;
            error.category = AcceleratorErrorCategory::InvalidState;
            error.backend = AcceleratorBackend::Metal;
            error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
            error.operation = "SpectrumAnalysisNode::ExecuteMetal";
            error.diagnostic = "metal backend selected without active metal session";
            return std::unexpected(error);
        }

        // Phase 6 correctness path: Metal-selected execution runs the same direct-DFT
        // reference algorithm after Metal session validation to keep CPU/Metal parity strict.
        auto metal_result = RunDirectDft(input,
                                         output,
                                         AcceleratorBackend::Metal,
                                         "SpectrumAnalysisNode::ExecuteMetal");
        if (!metal_result.has_value()) {
            return std::unexpected(metal_result.error());
        }
        return output;
    }

    if (selected_backend_ == AcceleratorBackend::Cuda) {
        if (!cuda_session_) {
            AcceleratorError error;
            error.category = AcceleratorErrorCategory::InvalidState;
            error.backend = AcceleratorBackend::Cuda;
            error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
            error.operation = "SpectrumAnalysisNode::ExecuteCuda";
            error.diagnostic = "cuda backend selected without active cuda session";
            return std::unexpected(error);
        }

        // Phase 6B correctness path: CUDA-selected execution currently reuses
        // the deterministic reference DFT after CUDA session validation to
        // preserve parity and strict fallback semantics through GraphExecutor.
        auto cuda_result = RunDirectDft(input,
                                        output,
                                        AcceleratorBackend::Cuda,
                                        "SpectrumAnalysisNode::ExecuteCuda");
        if (!cuda_result.has_value()) {
            return std::unexpected(cuda_result.error());
        }
        return output;
    }

    AcceleratorError error;
    error.category = AcceleratorErrorCategory::InvalidState;
    error.backend = selected_backend_;
    error.execution_mode = AcceleratorExecutionMode::HostSynchronous;
    error.operation = "SpectrumAnalysisNode::Execute";
    error.diagnostic = "unsupported selected backend for spectrum analysis";
    return std::unexpected(error);
}

void SpectrumSinkNode::Configure(const graph::JsonView& cfg) {
    if (!cfg.Raw().is_object()) {
        throw graph::ConfigError("SpectrumSinkNode configuration must be a JSON object");
    }

    std::scoped_lock<std::mutex> lock(mutex_);
    last_spectrum_.reset();
    frame_count_ = 0;
}

bool SpectrumSinkNode::Consume(const MagnitudeSpectrumPacket& spectrum,
                               std::integral_constant<std::size_t, 0>) {
    std::scoped_lock<std::mutex> lock(mutex_);
    last_spectrum_ = spectrum;
    ++frame_count_;
    return true;
}

std::optional<MagnitudeSpectrumPacket> SpectrumSinkNode::LastSpectrum() const {
    std::scoped_lock<std::mutex> lock(mutex_);
    return last_spectrum_;
}

std::size_t SpectrumSinkNode::FrameCount() const {
    std::scoped_lock<std::mutex> lock(mutex_);
    return frame_count_;
}

}  // namespace accelgraph
