// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "avionics/nodes/FlightMonitorNode.hpp"

namespace avionics {

std::optional<PhaseAdaptiveFeedbackMsg> FlightMonitorNode::Transfer(
    const sensors::StateVector& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>
) {
    // ===== STEP 1: Classify current flight phase =====
    classifier_.Classify(input, input.GetTimestamp().count());
    
    // ===== STEP 2: Increment sample counter =====
    sample_count_++;
    
    // ===== STEP 3: Check if we should emit output (every 5th sample) =====
    bool should_emit = ShouldEmitOutput();
    
    if (should_emit) {
        // Build and return output message
        auto msg = BuildOutputMessage(input);
        output_count_++;
        
        // Reset decimation counter for next cycle
        decimation_counter_ = 0;
        
        return msg;
    }
    
    // Increment decimation counter for next sample
    decimation_counter_++;
    
    // No output this sample
    return std::nullopt;
}

PhaseAdaptiveFeedbackMsg FlightMonitorNode::BuildOutputMessage(
    const sensors::StateVector& input
) {
    // Create output message with current state
    PhaseAdaptiveFeedbackMsg msg(input.GetTimestamp().count());
    
    // ===== Flight Phase Information =====
    auto current_phase = classifier_.GetCurrentPhase();
    auto previous_phase = classifier_.GetPreviousPhase();
    
    msg.current_phase = current_phase;
    msg.previous_phase = previous_phase;
    // Calculate time in current phase (duration since last transition)
    uint64_t current_time_us = input.GetTimestamp().count() / 1000;  // Convert ns to us
    msg.time_in_phase_us = (current_time_us > phase_transition_timestamp_us_) 
        ? (current_time_us - phase_transition_timestamp_us_)
        : 0;
    msg.is_phase_transition = (last_reported_phase_ != current_phase);
    
    // Update last reported phase for next call
    last_reported_phase_ = current_phase;
    if (msg.is_phase_transition) {
        phase_transition_timestamp_us_ = current_time_us;
        
        // ===== Publish phase transition event =====
        if (metrics_callback_) {
            try {
                app::metrics::MetricsEvent event{
                    .timestamp = std::chrono::system_clock::now(),
                    .source = GetName(),
                    .event_type = "phase_transition",
                    .data = {
                        {"previous_phase", std::to_string(static_cast<int>(previous_phase))},
                        {"current_phase", std::to_string(static_cast<int>(current_phase))},
                        {"timestamp_us", std::to_string(current_time_us)}
                    }
                };
                metrics_callback_->PublishAsync(event);
            } catch (...) {
                // Silently ignore metrics publishing errors to avoid disrupting flight
            }
        }
    }
    
    // ===== Flight State Information =====
    msg.altitude_m = input.position.z;
    msg.velocity_mps = input.velocity.z;
    msg.acceleration_mps2 = input.acceleration.z;
    
    // ===== Adaptive Control Parameters =====
    msg.parameters = computer_.GetParametersByPhase(current_phase);
    
    // ===== Transition Blending Information =====
    // Transition progress: 0.0 = start, 1.0 = complete
    // For now, just mark as complete (1.0) when not transitioning, 0.5 when transitioning
    msg.transition_progress = msg.is_phase_transition ? 0.5f : 1.0f;
    
    // ===== Metadata =====
    msg.sample_count = static_cast<uint32_t>(sample_count_);
    
    return msg;
}

void FlightMonitorNode::Reset() {
    // Reset the phase classifier to initial state
    classifier_ = FlightPhaseClassifier();
    
    // Reset decimation counter
    decimation_counter_ = 0;
    
    // Reset tracking state
    last_reported_phase_ = FlightPhaseClassifier::FlightPhase::Rail;
    phase_transition_timestamp_us_ = 0;
    
    // Note: We intentionally do NOT reset sample_count_ or output_count_
    // Those are statistics that should persist across Reset() calls
    // (they track lifetime statistics, not operational state)
}

// ============================================================================
// IMetricsCallbackProvider Implementation
// ============================================================================

bool FlightMonitorNode::SetMetricsCallback(
    graph::IMetricsCallback* callback) noexcept {
  try {
    metrics_callback_ = callback;
    return true;
  } catch (...) {
    return false;
  }
}

bool FlightMonitorNode::HasMetricsCallback() const noexcept {
  return metrics_callback_ != nullptr;
}

graph::IMetricsCallback* FlightMonitorNode::GetMetricsCallback()
    const noexcept {
  return metrics_callback_;
}

// ============================================================================
// Port Metadata
// ============================================================================

std::vector<graph::PortMetadata> FlightMonitorNode::GetInputPortMetadata() const {
    std::vector<graph::PortMetadata> metadata;
    metadata.push_back({
        0,                              // port_index
        "StateVector",                  // payload_type
        "input",                        // direction
        "Input"                         // port_name
    });
    return metadata;
}

std::vector<graph::PortMetadata> FlightMonitorNode::GetOutputPortMetadata() const {
    std::vector<graph::PortMetadata> metadata;
    metadata.push_back({
        0,                              // port_index
        "PhaseAdaptiveFeedbackMsg",     // payload_type
        "output",                       // direction
        "Output"                        // port_name
    });
    return metadata;
}


}  // namespace avionics
