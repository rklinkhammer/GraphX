/**
 * @file AcceleratorSession.hpp
 * @brief Accelerator Session backend-neutral GPU acceleration support.
 *
 * @details Provides backend-neutral accelerator session descriptors, narrow capabilities, and structured resolution support.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace graph::gpu {

enum class ExecutionMode : std::uint8_t {
    Native = 0,
    Stub,
    Simulated,
    CpuFallback,
    Unavailable,
};

enum class SessionFallbackPolicy : std::uint8_t {
    Strict = 0,
    AllowFallback,
};

enum class SessionResolutionState : std::uint8_t {
    Selected = 0,
    Rejected,
    Fallback,
    Unsupported,
    Unavailable,
};

struct FeatureSet {
    bool device_memory{false};
    bool host_visible_memory{false};
    bool transfers{false};
    bool events{false};
    bool execution{false};
    bool telemetry{false};
    bool collectives{false};
    bool runtime_compilation{false};

    [[nodiscard]] constexpr bool Satisfies(const FeatureSet& required) const noexcept {
        return (!required.device_memory || device_memory) &&
               (!required.host_visible_memory || host_visible_memory) &&
               (!required.transfers || transfers) &&
               (!required.events || events) &&
               (!required.execution || execution) &&
               (!required.telemetry || telemetry) &&
               (!required.collectives || collectives) &&
               (!required.runtime_compilation || runtime_compilation);
    }
};

struct BackendDescriptor {
    accel::BackendKind backend{accel::BackendKind::Unknown};
    ExecutionMode execution_mode{ExecutionMode::Unavailable};
    std::string provider_name;
    std::string runtime_version;
    std::string device_name;
    std::uint32_t device_id{0};
    std::string architecture;
    std::uint64_t session_id{0};
    FeatureSet features{};
};

struct AcceleratorSessionRequest {
    std::optional<accel::BackendKind> required_backend{};
    std::optional<accel::BackendKind> preferred_backend{};
    std::optional<ExecutionMode> required_execution_mode{};
    std::optional<std::uint32_t> device_id{};
    FeatureSet required_features{};
    SessionFallbackPolicy fallback_policy{SessionFallbackPolicy::Strict};
};

struct AcceleratorSessionDiagnostic {
    std::string provider_name;
    accel::BackendKind backend{accel::BackendKind::Unknown};
    ExecutionMode execution_mode{ExecutionMode::Unavailable};
    std::uint32_t device_id{0};
    SessionResolutionState state{SessionResolutionState::Rejected};
    std::string detail;
};

class IMemoryCapability {
public:
    virtual ~IMemoryCapability() = default;
    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateHostVisible(std::uint64_t bytes,
                                     accel::BufferLease& out_lease) = 0;
    virtual bool Release(const accel::BufferLease& lease) = 0;
};

class ITransferCapability {
public:
    virtual ~ITransferCapability() = default;
    virtual bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;
    virtual bool EnqueueD2H(const accel::DeviceBufferView& src,
                            accel::HostPinnedBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;
    virtual bool EnqueueD2D(const accel::DeviceBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;
};

class IEventCapability {
public:
    virtual ~IEventCapability() = default;
    virtual std::uint64_t Create() = 0;
    virtual bool IsComplete(std::uint64_t event_id) const = 0;
    virtual bool Wait(std::uint64_t event_id, std::uint64_t timeout_ms) = 0;
    virtual void Destroy(std::uint64_t event_id) = 0;
};

class IExecutionCapability {
public:
    virtual ~IExecutionCapability() = default;
    virtual std::uint64_t AcquireQueue() = 0;
    virtual void ReleaseQueue(std::uint64_t queue_id) = 0;
    virtual bool Submit(const accel::KernelTicket& ticket, void* const* args,
                        std::size_t arg_count) = 0;
};

class ITelemetryCapability {
public:
    virtual ~ITelemetryCapability() = default;
    virtual void RecordTransfer(const accel::TransferTicket& ticket,
                                std::uint64_t duration_ns) = 0;
    virtual void RecordExecution(const accel::KernelTicket& ticket,
                                 std::uint64_t duration_ns) = 0;
    virtual void IncrementErrorCounter(std::string_view error_code) = 0;
};

class IAcceleratorSession {
public:
    virtual ~IAcceleratorSession() = default;
    virtual const BackendDescriptor& Describe() const = 0;
    virtual IMemoryCapability& Memory() = 0;
    virtual ITransferCapability& Transfer() = 0;
    virtual IEventCapability& Events() = 0;
    virtual IExecutionCapability& Execution() = 0;
    virtual ITelemetryCapability& Telemetry() = 0;
    [[nodiscard]] virtual bool CapabilitiesMatchDescriptor() const = 0;
};

struct AcceleratorSessionResolution {
    std::shared_ptr<IAcceleratorSession> session{};
    BackendDescriptor descriptor{};
    std::vector<AcceleratorSessionDiagnostic> diagnostics{};
    bool fallback_used{false};
};

struct AcceleratorSessionFailure {
    std::optional<accel::BackendKind> requested_backend{};
    std::optional<ExecutionMode> requested_execution_mode{};
    std::string detail;
    std::vector<AcceleratorSessionDiagnostic> diagnostics{};
};

enum class SessionValidationCode : std::uint8_t {
    UnknownBackend,
    UnavailableExecutionMode,
    MissingProviderName,
    MissingSessionId,
    InconsistentCpuMode,
    DuplicateSessionId,
    MissingAdvertisedCapability,
};

struct SessionValidationError {
    SessionValidationCode code{SessionValidationCode::UnknownBackend};
    std::string detail;
};

[[nodiscard]] inline const char* ToString(ExecutionMode execution_mode) noexcept {
    switch (execution_mode) {
    case ExecutionMode::Native:
        return "Native";
    case ExecutionMode::Stub:
        return "Stub";
    case ExecutionMode::Simulated:
        return "Simulated";
    case ExecutionMode::CpuFallback:
        return "CpuFallback";
    case ExecutionMode::Unavailable:
    default:
        return "Unavailable";
    }
}

[[nodiscard]] inline const char* ToString(SessionFallbackPolicy policy) noexcept {
    switch (policy) {
    case SessionFallbackPolicy::AllowFallback:
        return "AllowFallback";
    case SessionFallbackPolicy::Strict:
    default:
        return "Strict";
    }
}

[[nodiscard]] inline const char* ToString(SessionResolutionState state) noexcept {
    switch (state) {
    case SessionResolutionState::Selected:
        return "Selected";
    case SessionResolutionState::Rejected:
        return "Rejected";
    case SessionResolutionState::Fallback:
        return "Fallback";
    case SessionResolutionState::Unsupported:
        return "Unsupported";
    case SessionResolutionState::Unavailable:
    default:
        return "Unavailable";
    }
}

[[nodiscard]] inline bool Matches(const AcceleratorSessionRequest& request,
                                  const BackendDescriptor& descriptor) {
    if (descriptor.backend == accel::BackendKind::Unknown ||
        descriptor.execution_mode == ExecutionMode::Unavailable ||
        descriptor.provider_name.empty() || descriptor.session_id == 0) {
        return false;
    }
    if (request.required_backend && *request.required_backend != descriptor.backend) {
        return false;
    }
    if (request.required_execution_mode &&
        *request.required_execution_mode != descriptor.execution_mode) {
        return false;
    }
    if (request.device_id && *request.device_id != descriptor.device_id) {
        return false;
    }
    return descriptor.features.Satisfies(request.required_features);
}

[[nodiscard]] inline std::optional<SessionValidationError>
ValidateDescriptor(const BackendDescriptor& descriptor) {
    if (descriptor.backend == accel::BackendKind::Unknown) {
        return SessionValidationError{SessionValidationCode::UnknownBackend,
                                      "backend must not be Unknown"};
    }
    if (descriptor.execution_mode == ExecutionMode::Unavailable) {
        return SessionValidationError{SessionValidationCode::UnavailableExecutionMode,
                                      "unavailable providers cannot be registered"};
    }
    if (descriptor.provider_name.empty()) {
        return SessionValidationError{SessionValidationCode::MissingProviderName,
                                      "provider_name must not be empty"};
    }
    if (descriptor.session_id == 0) {
        return SessionValidationError{SessionValidationCode::MissingSessionId,
                                      "session_id must be nonzero"};
    }
    const bool cpu_backend = descriptor.backend == accel::BackendKind::CPU;
    const bool cpu_mode = descriptor.execution_mode == ExecutionMode::CpuFallback;
    if (cpu_backend != cpu_mode) {
        return SessionValidationError{SessionValidationCode::InconsistentCpuMode,
                                      "CPU backend and CpuFallback mode must be used together"};
    }
    return std::nullopt;
}

inline std::ostream& operator<<(std::ostream& out, const FeatureSet& features) {
    out << "FeatureSet{" << "device_memory=" << features.device_memory
        << ", host_visible_memory=" << features.host_visible_memory
        << ", transfers=" << features.transfers << ", events=" << features.events
        << ", execution=" << features.execution << ", telemetry=" << features.telemetry
        << ", collectives=" << features.collectives
        << ", runtime_compilation=" << features.runtime_compilation << '}';
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const BackendDescriptor& descriptor) {
    out << "BackendDescriptor{" << accel::ToString(descriptor.backend)
        << ", execution_mode=" << ToString(descriptor.execution_mode)
        << ", provider=" << descriptor.provider_name
        << ", runtime=" << descriptor.runtime_version
        << ", device=" << descriptor.device_name
        << ", device_id=" << descriptor.device_id
        << ", architecture=" << descriptor.architecture
        << ", session_id=" << descriptor.session_id
        << ", features=" << descriptor.features << '}';
    return out;
}

inline std::ostream& operator<<(std::ostream& out,
                                const AcceleratorSessionRequest& request) {
    out << "AcceleratorSessionRequest{";
    out << "required_backend=";
    if (request.required_backend) {
        out << accel::ToString(*request.required_backend);
    } else {
        out << "<any>";
    }
    out << ", preferred_backend=";
    if (request.preferred_backend) {
        out << accel::ToString(*request.preferred_backend);
    } else {
        out << "<none>";
    }
    out << ", required_execution_mode=";
    if (request.required_execution_mode) {
        out << ToString(*request.required_execution_mode);
    } else {
        out << "<any>";
    }
    out << ", device_id=";
    if (request.device_id) {
        out << *request.device_id;
    } else {
        out << "<any>";
    }
    out << ", fallback_policy=" << ToString(request.fallback_policy)
        << ", required_features=" << request.required_features << '}';
    return out;
}

inline void to_json(nlohmann::json& json, const FeatureSet& features) {
    json = nlohmann::json{{"device_memory", features.device_memory},
                          {"host_visible_memory", features.host_visible_memory},
                          {"transfers", features.transfers},
                          {"events", features.events},
                          {"execution", features.execution},
                          {"telemetry", features.telemetry},
                          {"collectives", features.collectives},
                          {"runtime_compilation", features.runtime_compilation}};
}

inline void from_json(const nlohmann::json& json, FeatureSet& features) {
    json.at("device_memory").get_to(features.device_memory);
    json.at("host_visible_memory").get_to(features.host_visible_memory);
    json.at("transfers").get_to(features.transfers);
    json.at("events").get_to(features.events);
    json.at("execution").get_to(features.execution);
    json.at("telemetry").get_to(features.telemetry);
    json.at("collectives").get_to(features.collectives);
    json.at("runtime_compilation").get_to(features.runtime_compilation);
}

inline void to_json(nlohmann::json& json, const BackendDescriptor& descriptor) {
    json = nlohmann::json{{"backend", accel::ToString(descriptor.backend)},
                          {"execution_mode", ToString(descriptor.execution_mode)},
                          {"provider_name", descriptor.provider_name},
                          {"runtime_version", descriptor.runtime_version},
                          {"device_name", descriptor.device_name},
                          {"device_id", descriptor.device_id},
                          {"architecture", descriptor.architecture},
                          {"session_id", descriptor.session_id},
                          {"features", descriptor.features}};
}

inline void from_json(const nlohmann::json& json, BackendDescriptor& descriptor) {
    const auto backend = json.at("backend").get<std::string>();
    if (backend == "CPU") {
        descriptor.backend = accel::BackendKind::CPU;
    } else if (backend == "CUDA") {
        descriptor.backend = accel::BackendKind::CUDA;
    } else if (backend == "Metal") {
        descriptor.backend = accel::BackendKind::Metal;
    } else {
        throw nlohmann::json::other_error::create(501, "invalid accelerator backend: " + backend, &json);
    }

    const auto execution_mode = json.at("execution_mode").get<std::string>();
    if (execution_mode == "Native") {
        descriptor.execution_mode = ExecutionMode::Native;
    } else if (execution_mode == "Stub") {
        descriptor.execution_mode = ExecutionMode::Stub;
    } else if (execution_mode == "Simulated") {
        descriptor.execution_mode = ExecutionMode::Simulated;
    } else if (execution_mode == "CpuFallback") {
        descriptor.execution_mode = ExecutionMode::CpuFallback;
    } else if (execution_mode == "Unavailable") {
        descriptor.execution_mode = ExecutionMode::Unavailable;
    } else {
        throw nlohmann::json::other_error::create(501, "invalid execution mode: " + execution_mode, &json);
    }

    json.at("provider_name").get_to(descriptor.provider_name);
    json.at("runtime_version").get_to(descriptor.runtime_version);
    json.at("device_name").get_to(descriptor.device_name);
    json.at("device_id").get_to(descriptor.device_id);
    json.at("architecture").get_to(descriptor.architecture);
    json.at("session_id").get_to(descriptor.session_id);
    json.at("features").get_to(descriptor.features);
}

inline void to_json(nlohmann::json& json, const AcceleratorSessionRequest& request) {
    json = nlohmann::json{{"required_backend",
                           request.required_backend
                               ? nlohmann::json(accel::ToString(*request.required_backend))
                               : nlohmann::json(nullptr)},
                          {"preferred_backend",
                           request.preferred_backend
                               ? nlohmann::json(accel::ToString(*request.preferred_backend))
                               : nlohmann::json(nullptr)},
                          {"required_execution_mode",
                           request.required_execution_mode
                               ? nlohmann::json(ToString(*request.required_execution_mode))
                               : nlohmann::json(nullptr)},
                          {"device_id",
                           request.device_id ? nlohmann::json(*request.device_id)
                                             : nlohmann::json(nullptr)},
                          {"required_features", request.required_features},
                          {"fallback_policy", ToString(request.fallback_policy)}};
}

inline void from_json(const nlohmann::json& json, AcceleratorSessionRequest& request) {
    request.required_backend.reset();
    request.preferred_backend.reset();
    request.required_execution_mode.reset();
    request.device_id.reset();
    if (json.contains("required_backend") && !json.at("required_backend").is_null()) {
        const auto value = json.at("required_backend").get<std::string>();
        if (value == "CPU") {
            request.required_backend = accel::BackendKind::CPU;
        } else if (value == "CUDA") {
            request.required_backend = accel::BackendKind::CUDA;
        } else if (value == "Metal") {
            request.required_backend = accel::BackendKind::Metal;
        } else { throw nlohmann::json::other_error::create(501, "invalid required_backend: " + value, &json); }
    }

    if (json.contains("preferred_backend") && !json.at("preferred_backend").is_null()) {
        const auto value = json.at("preferred_backend").get<std::string>();
        if (value == "CPU") {
            request.preferred_backend = accel::BackendKind::CPU;
        } else if (value == "CUDA") {
            request.preferred_backend = accel::BackendKind::CUDA;
        } else if (value == "Metal") {
            request.preferred_backend = accel::BackendKind::Metal;
        } else { throw nlohmann::json::other_error::create(501, "invalid preferred_backend: " + value, &json); }
    }

    if (json.contains("required_execution_mode") && !json.at("required_execution_mode").is_null()) {
        const auto value = json.at("required_execution_mode").get<std::string>();
        if (value == "Native") {
            request.required_execution_mode = ExecutionMode::Native;
        } else if (value == "Stub") {
            request.required_execution_mode = ExecutionMode::Stub;
        } else if (value == "Simulated") {
            request.required_execution_mode = ExecutionMode::Simulated;
        } else if (value == "CpuFallback") {
            request.required_execution_mode = ExecutionMode::CpuFallback;
        } else if (value == "Unavailable") {
            request.required_execution_mode = ExecutionMode::Unavailable;
        } else { throw nlohmann::json::other_error::create(501, "invalid required_execution_mode: " + value, &json); }
    }

    if (json.contains("device_id") && !json.at("device_id").is_null()) {
        request.device_id = json.at("device_id").get<std::uint32_t>();
    }

    json.at("required_features").get_to(request.required_features);

    if (json.contains("fallback_policy")) {
        const auto value = json.at("fallback_policy").get<std::string>();
        if (value == "AllowFallback") {
            request.fallback_policy = SessionFallbackPolicy::AllowFallback;
        } else if (value == "Strict") {
            request.fallback_policy = SessionFallbackPolicy::Strict;
        } else {
            throw nlohmann::json::other_error::create(501, "invalid fallback_policy: " + value, &json);
        }
    }
}

class AcceleratorSessionRegistry {
public:
    using SessionPtr = std::shared_ptr<IAcceleratorSession>;

    [[nodiscard]] std::expected<void, SessionValidationError> Register(SessionPtr session) {
        if (session == nullptr) {
            return std::unexpected(SessionValidationError{
                SessionValidationCode::MissingProviderName, "session must not be null"});
        }
        const auto& descriptor = session->Describe();
        if (auto error = ValidateDescriptor(descriptor)) {
            return std::unexpected(std::move(*error));
        }
        if (!session->CapabilitiesMatchDescriptor()) {
            return std::unexpected(SessionValidationError{
                SessionValidationCode::MissingAdvertisedCapability,
                "advertised feature set does not match provider capabilities"});
        }
        if (!session_ids_.insert(descriptor.session_id).second) {
            return std::unexpected(SessionValidationError{
                SessionValidationCode::DuplicateSessionId, "session_id is already registered"});
        }
        sessions_.push_back(std::move(session));
        std::stable_sort(sessions_.begin(), sessions_.end(), [](const auto& lhs, const auto& rhs) {
            const auto& left = lhs->Describe();
            const auto& right = rhs->Describe();
            return std::tie(left.backend, left.execution_mode, left.device_id, left.provider_name,
                            left.session_id) <
                   std::tie(right.backend, right.execution_mode, right.device_id, right.provider_name,
                            right.session_id);
        });
        return {};
    }

    [[nodiscard]] std::expected<AcceleratorSessionResolution, AcceleratorSessionFailure>
    Resolve(const AcceleratorSessionRequest& request) const {
        if (sessions_.empty()) {
            return std::unexpected(AcceleratorSessionFailure{
                .requested_backend = request.required_backend,
                .requested_execution_mode = request.required_execution_mode,
                .detail = "no accelerator sessions are registered",
            });
        }

        std::vector<std::size_t> ordered_indices;
        ordered_indices.reserve(sessions_.size());

        auto append_indices = [&](auto predicate) {
            for (std::size_t index = 0; index < sessions_.size(); ++index) {
                if (predicate(index) &&
                    std::find(ordered_indices.begin(), ordered_indices.end(), index) ==
                        ordered_indices.end()) {
                    ordered_indices.push_back(index);
                }
            }
        };

        if (request.required_backend) {
            append_indices([&](std::size_t index) {
                return sessions_[index] != nullptr &&
                       sessions_[index]->Describe().backend == *request.required_backend;
            });
        } else if (request.preferred_backend) {
            append_indices([&](std::size_t index) {
                return sessions_[index] != nullptr &&
                       sessions_[index]->Describe().backend == *request.preferred_backend;
            });
            if (request.fallback_policy == SessionFallbackPolicy::AllowFallback) {
                append_indices([&](std::size_t index) { return sessions_[index] != nullptr; });
            }
        } else {
            append_indices([&](std::size_t index) { return sessions_[index] != nullptr; });
        }

        std::vector<AcceleratorSessionDiagnostic> diagnostics;
        diagnostics.reserve(ordered_indices.size());

        for (const auto index : ordered_indices) {
            const auto& session = sessions_[index];
            const auto& descriptor = session->Describe();
            if (!Matches(request, descriptor)) {
                diagnostics.push_back(AcceleratorSessionDiagnostic{
                    .provider_name = descriptor.provider_name,
                    .backend = descriptor.backend,
                    .execution_mode = descriptor.execution_mode,
                    .device_id = descriptor.device_id,
                    .state = SessionResolutionState::Rejected,
                    .detail = "descriptor does not satisfy request requirements",
                });
                continue;
            }

            const bool fallback_used = request.preferred_backend.has_value() &&
                descriptor.backend != *request.preferred_backend;
            diagnostics.push_back(AcceleratorSessionDiagnostic{
                .provider_name = descriptor.provider_name,
                .backend = descriptor.backend,
                .execution_mode = descriptor.execution_mode,
                .device_id = descriptor.device_id,
                .state = fallback_used ? SessionResolutionState::Fallback
                                       : SessionResolutionState::Selected,
                .detail = fallback_used ? "resolved through fallback" : "resolved",
            });
            return AcceleratorSessionResolution{
                .session = session,
                .descriptor = descriptor,
                .diagnostics = std::move(diagnostics),
                .fallback_used = fallback_used,
            };
        }

        return std::unexpected(AcceleratorSessionFailure{
            .requested_backend = request.required_backend,
            .requested_execution_mode = request.required_execution_mode,
            .detail = request.required_backend
                ? "no registered accelerator session matched the required backend"
                : "no registered accelerator session satisfied the request",
            .diagnostics = std::move(diagnostics),
        });
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return sessions_.size();
    }

private:
    std::vector<SessionPtr> sessions_{};
    std::unordered_set<std::uint64_t> session_ids_{};
};

inline void to_json(nlohmann::json& json, const AcceleratorSessionDiagnostic& diagnostic) {
    json = nlohmann::json{{"provider_name", diagnostic.provider_name},
                          {"backend", accel::ToString(diagnostic.backend)},
                          {"execution_mode", ToString(diagnostic.execution_mode)},
                          {"device_id", diagnostic.device_id},
                          {"state", ToString(diagnostic.state)},
                          {"detail", diagnostic.detail}};
}

inline void to_json(nlohmann::json& json, const AcceleratorSessionFailure& failure) {
    json = nlohmann::json{{"requested_backend",
                           failure.requested_backend
                               ? nlohmann::json(accel::ToString(*failure.requested_backend))
                               : nlohmann::json(nullptr)},
                          {"requested_execution_mode",
                           failure.requested_execution_mode
                               ? nlohmann::json(ToString(*failure.requested_execution_mode))
                               : nlohmann::json(nullptr)},
                          {"detail", failure.detail},
                          {"diagnostics", failure.diagnostics}};
}

inline void to_json(nlohmann::json& json, const AcceleratorSessionResolution& resolution) {
    json = nlohmann::json{{"descriptor", resolution.descriptor},
                          {"diagnostics", resolution.diagnostics},
                          {"fallback_used", resolution.fallback_used}};
}

} // namespace graph::gpu
