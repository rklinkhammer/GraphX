/**
 * @file SDRTestNodes.hpp
 * @brief Dynamically loadable SDR test nodes.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <mutex>
#include <optional>
#include <tuple>
#include <vector>

#include "graph/ICompletionCallback.hpp"
#include "graph/Message.hpp"
#include "graph/NamedNodes.hpp"
#include "graph/PortSpec.hpp"
#include "graph/PortTypes.hpp"

namespace test::sdr {

struct IQPacket {
    double sample_rate_hz{0.0};
    double tone_frequency_hz{0.0};
    uint64_t packet_number{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::vector<std::complex<double>> samples;
};

struct PowerSpectrum {
    double sample_rate_hz{0.0};
    uint64_t packet_number{0};
    std::vector<double> bins;
};

class SineIQSourceNode : public graph::NamedSourceNode<SineIQSourceNode, graph::message::Message> {
public:
    static constexpr char kIQOutputPort[] = "IQ";

    using Ports = std::tuple<
        graph::PortSpec<0, graph::message::Message, graph::PortDirection::Output, kIQOutputPort,
                        graph::PayloadList<IQPacket>>>;

    SineIQSourceNode(size_t sample_count = 64, double sample_rate_hz = 64000.0, double tone_bin = 5.0)
        : sample_count_(sample_count),
          sample_rate_hz_(sample_rate_hz),
          tone_bin_(tone_bin) {
        SetName("SineIQSourceNode");
    }

    std::optional<graph::message::Message> Produce(std::integral_constant<std::size_t, 0>) override {
        if (produced_) {
            return std::nullopt;
        }
        produced_ = true;

        IQPacket packet;
        packet.sample_rate_hz = sample_rate_hz_;
        packet.tone_frequency_hz = tone_bin_ * sample_rate_hz_ / static_cast<double>(sample_count_);
        packet.packet_number = 1;
        packet.samples.reserve(sample_count_);

        constexpr double kTwoPi = 6.28318530717958647692;
        for (size_t n = 0; n < sample_count_; ++n) {
            const double phase = kTwoPi * tone_bin_ * static_cast<double>(n) / static_cast<double>(sample_count_);
            packet.samples.emplace_back(std::cos(phase), std::sin(phase));
        }

        return graph::message::Message(packet);
    }

private:
    size_t sample_count_;
    double sample_rate_hz_;
    double tone_bin_;
    bool produced_{false};
};

class FFTPowerSpectrumNode
    : public graph::NamedInteriorNode<graph::TypeList<graph::message::Message>,
                                      graph::TypeList<graph::message::Message>,
                                      FFTPowerSpectrumNode> {
public:
    static constexpr char kIQInputPort[] = "IQ";
    static constexpr char kSpectrumOutputPort[] = "PowerSpectrum";

    using Ports = std::tuple<
        graph::PortSpec<0, graph::message::Message, graph::PortDirection::Input, kIQInputPort,
                        graph::PayloadList<IQPacket>>,
        graph::PortSpec<0, graph::message::Message, graph::PortDirection::Output, kSpectrumOutputPort,
                        graph::PayloadList<PowerSpectrum>>>;

    FFTPowerSpectrumNode() {
        SetName("FFTPowerSpectrumNode");
    }

    std::optional<graph::message::Message> Transfer(const graph::message::Message& input,
                                                    std::integral_constant<std::size_t, 0>,
                                                    std::integral_constant<std::size_t, 0>) override {
        const auto& packet = input.get<IQPacket>();
        const size_t n_samples = packet.samples.size();

        PowerSpectrum spectrum;
        spectrum.sample_rate_hz = packet.sample_rate_hz;
        spectrum.packet_number = packet.packet_number;
        spectrum.bins.assign(n_samples, 0.0);

        constexpr double kTwoPi = 6.28318530717958647692;
        for (size_t k = 0; k < n_samples; ++k) {
            std::complex<double> accumulator{0.0, 0.0};
            for (size_t n = 0; n < n_samples; ++n) {
                const double phase = -kTwoPi * static_cast<double>(k * n) / static_cast<double>(n_samples);
                accumulator += packet.samples[n] * std::complex<double>(std::cos(phase), std::sin(phase));
            }
            spectrum.bins[k] = std::norm(accumulator);
        }

        return graph::message::Message(spectrum);
    }
};

class AnalyzerSinkNode : public graph::NamedSinkNode<AnalyzerSinkNode, graph::message::Message>,
                         public graph::CompletionCallbackProvider {
public:
    static constexpr char kSpectrumInputPort[] = "PowerSpectrum";

    using Ports = std::tuple<
        graph::PortSpec<0, graph::message::Message, graph::PortDirection::Input, kSpectrumInputPort,
                        graph::PayloadList<PowerSpectrum>>>;

    AnalyzerSinkNode() {
        SetName("AnalyzerSinkNode");
    }

    bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_spectrum_ = msg.get<PowerSpectrum>();
            spectrum_count_++;
        }

        if (this->HasCallbackProvider()) {
            auto provider = dynamic_cast<CompletionNodeCallback*>(this->GetCallbackProvider());
            if (provider) {
                provider->OnComplete();
            }
        }

        return false;
    }

    size_t GetSpectrumCount() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return spectrum_count_;
    }

    PowerSpectrum GetLastSpectrum() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_spectrum_;
    }

    size_t GetPeakBin() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto peak = std::max_element(last_spectrum_.bins.begin(), last_spectrum_.bins.end());
        return static_cast<size_t>(std::distance(last_spectrum_.bins.begin(), peak));
    }

private:
    mutable std::mutex state_mutex_;
    PowerSpectrum last_spectrum_;
    size_t spectrum_count_{0};
};

} // namespace test::sdr
