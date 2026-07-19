// SPDX-License-Identifier: MIT
#include "FHSSJobController.hpp"

#include "FHSSIqArtifactGenerator.hpp"
#include "FHSSIqOutputTransaction.hpp"
#include "FHSSObservationService.hpp"
#include "dsp/fhss/FHSSProtocol.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>

namespace dsp::fhss::dashboard {
namespace {

nlohmann::json Problem(int status, std::string code, std::string detail) {
  return {{"type", "urn:graphx:dashboard:problem:" + code},
          {"title", code},
          {"status", status},
          {"detail", detail},
          {"code", std::move(code)},
          {"instance", "/api/v1/fhss/jobs"}};
}

bool SafeToken(std::string_view value) {
  return !value.empty() && value.size() <= 64 &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isalnum(character) || character == '-' ||
                  character == '_' || character == '.' || character == ':';
         });
}

bool ContainsForbiddenReceiverKey(const nlohmann::json &value) {
  static const std::set<std::string, std::less<>> forbidden{
      "messages",
      "schedule",
      "expected",
      "expected_words",
      "truth",
      "truth_hash",
      "truth_sha256",
      "generator_metadata",
      "generator_id",
      "scenario_correlation_id",
      "job_id",
      "request_id",
      "active_frequency_indices"};
  if (value.is_object()) {
    for (const auto &[key, child] : value.items())
      if (forbidden.contains(key) || ContainsForbiddenReceiverKey(child))
        return true;
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (ContainsForbiddenReceiverKey(child))
        return true;
  }
  return false;
}

std::string HashJson(const nlohmann::json &document) {
  const auto text = document.dump(2) + "\n";
  return graphx::examples::fhss::Sha256(
      std::as_bytes(std::span(text.data(), text.size())));
}

std::string HashFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    throw std::runtime_error("artifact hash open failed");
  const auto end = input.tellg();
  if (end < 0)
    throw std::runtime_error("artifact hash size failed");
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!bytes.empty() && !input.read(reinterpret_cast<char *>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("artifact hash read failed");
  return graphx::examples::fhss::Sha256(bytes);
}

class TruthWithholdingGuard {
public:
  TruthWithholdingGuard(std::filesystem::path path, nlohmann::json document)
      : path_(std::move(path)), document_(std::move(document)) {}
  ~TruthWithholdingGuard() { RestoreNoThrow(); }
  TruthWithholdingGuard(const TruthWithholdingGuard &) = delete;
  TruthWithholdingGuard &operator=(const TruthWithholdingGuard &) = delete;

  void Restore() {
    if (!active_)
      return;
    graphx::examples::fhss::WriteJson(path_, document_);
    std::filesystem::permissions(path_,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    active_ = false;
  }

private:
  void RestoreNoThrow() noexcept {
    try {
      Restore();
    } catch (...) {
    }
  }
  std::filesystem::path path_;
  nlohmann::json document_;
  bool active_ = true;
};

} // namespace

struct FHSSJobController::Job {
  std::uint64_t controller_epoch = 0;
  std::uint64_t sequence = 0;
  std::string job_id;
  std::string request_id;
  std::string idempotency_digest;
  std::string canonical_request;
  std::string scenario_correlation_id;
  std::string operation;
  std::string sample_format;
  std::size_t message_cursor = 0;
  std::size_t message_count = 0;
  std::chrono::milliseconds timeout{30'000};
  nlohmann::json generator_input;
  std::uint64_t config_revision = 0;
  std::string config_etag;
  std::string state = "queued";
  std::string terminal_code;
  std::string terminal_detail;
  std::string created_at;
  std::string started_at;
  std::string terminal_at;
  std::chrono::steady_clock::time_point created_monotonic =
      std::chrono::steady_clock::now();
  std::uint64_t graph_generation = 0;
  std::uint64_t run_epoch = 0;
  nlohmann::json artifacts = nlohmann::json::object();
  nlohmann::json graph_lifecycle = nullptr;
  nlohmann::json receiver_result = nullptr;
  nlohmann::json receiver_observation = nullptr;
  nlohmann::json comparison = nullptr;
  bool cancel_requested = false;
  bool generator_invoked = false;
  bool replay_invoked = false;
  std::filesystem::path directory;
};

struct FHSSJobController::IdempotencyRecord {
  std::string canonical_request;
  std::weak_ptr<Job> job;
};

FHSSJobController::FHSSJobController(
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
    std::filesystem::path artifact_root)
    : configuration_service_(std::move(configuration_service)),
      runtime_session_(std::move(runtime_session)),
      observation_service_(std::make_shared<FHSSObservationService>(
          configuration_service_, runtime_session_)),
      artifact_root_(std::filesystem::absolute(std::move(artifact_root)) /
                     "fhss-jobs") {
  if (!configuration_service_ || !runtime_session_)
    throw std::invalid_argument(
        "FHSS job controller dependencies are required");
  std::filesystem::create_directories(artifact_root_);
  controller_epoch_ = static_cast<std::uint64_t>(
      std::chrono::system_clock::now().time_since_epoch().count());
  if (controller_epoch_ == 0)
    controller_epoch_ = 1;
  for (const auto &entry :
       std::filesystem::directory_iterator(artifact_root_)) {
    if (entry.is_regular_file() && entry.path().extension() == ".tmp") {
      std::error_code ignored;
      std::filesystem::remove(entry.path(), ignored);
    } else if (entry.is_directory() &&
               entry.path().filename().string().starts_with("j-") &&
               entry.path().filename().string().size() == 26) {
      for (const auto &artifact :
           std::filesystem::directory_iterator(entry.path())) {
        if (artifact.is_regular_file() &&
            artifact.path().extension() == ".tmp") {
          std::error_code ignored;
          std::filesystem::remove(artifact.path(), ignored);
        }
      }
    }
  }
  worker_ =
      std::jthread([this](std::stop_token stop_token) { Worker(stop_token); });
}

FHSSJobController::~FHSSJobController() { Shutdown(); }

bool FHSSJobController::IsTerminal(std::string_view state) {
  return state == "completed" || state == "cancelled" || state == "timed_out" ||
         state == "failed" || state == "abandoned_on_restart";
}

bool FHSSJobController::IsLegalTransition(std::string_view from,
                                          std::string_view to) {
  if (from == "queued")
    return to == "generating" || to == "cancelled" || to == "failed";
  if (from == "generating")
    return to == "generated" || to == "cancelling" || to == "cancelled" ||
           to == "timed_out" || to == "failed";
  if (from == "generated")
    return to == "replay_pending" || to == "cancelling" || to == "cancelled" ||
           to == "timed_out" || to == "failed";
  if (from == "replay_pending")
    return to == "running" || to == "cancelling" || to == "cancelled" ||
           to == "timed_out" || to == "failed";
  if (from == "running")
    return to == "completed" || to == "cancelling" || to == "timed_out" ||
           to == "failed";
  if (from == "cancelling")
    return to == "cancelled" || to == "failed";
  return false;
}

std::string FHSSJobController::NowRfc3339() {
  const auto value =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm time{};
  gmtime_r(&value, &time);
  std::ostringstream output;
  output << std::put_time(&time, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

std::string FHSSJobController::Canonical(const nlohmann::json &request) {
  return request.dump();
}

std::string FHSSJobController::Digest(std::string_view value) {
  return graphx::examples::fhss::Sha256(
      std::as_bytes(std::span(value.data(), value.size())));
}

FHSSJobController::Result
FHSSJobController::Submit(const nlohmann::json &raw_request,
                          std::string_view idempotency_key) {
  if (!raw_request.is_object())
    return {400,
            Problem(400, "invalid_job_request", "request must be an object")};
  static const std::set<std::string, std::less<>> allowed{
      "operation", "request_id", "message_count", "sample_format",
      "timeout_ms"};
  for (const auto &[key, unused] : raw_request.items()) {
    (void)unused;
    if (!allowed.contains(key))
      return {400, Problem(400, "unknown_job_field",
                           "unknown request field: " + key)};
  }
  if (!SafeToken(idempotency_key))
    return {400, Problem(400, "invalid_idempotency_key",
                         "Idempotency-Key must be 1-64 safe token characters")};
  if (!raw_request.contains("request_id") ||
      !raw_request.at("request_id").is_string() ||
      !SafeToken(raw_request.at("request_id").get_ref<const std::string &>()))
    return {400, Problem(400, "invalid_request_id",
                         "request_id must be a 1-64 character safe token")};
  if (raw_request.contains("operation") &&
      !raw_request.at("operation").is_string())
    return {400, Problem(400, "invalid_job_operation",
                         "operation must be a string")};
  const auto operation = raw_request.contains("operation")
                             ? raw_request.at("operation").get<std::string>()
                             : std::string{"step"};
  if (operation != "step" && operation != "continue")
    return {422, Problem(422, "unsupported_job_operation",
                         "operation must be step or continue")};
  const auto default_count =
      operation == "step" ? std::uint64_t{1} : std::uint64_t{4};
  if (raw_request.contains("message_count") &&
      !raw_request.at("message_count").is_number_unsigned() &&
      !(raw_request.at("message_count").is_number_integer() &&
        raw_request.at("message_count").get<std::int64_t>() >= 0))
    return {400, Problem(400, "invalid_message_count",
                         "message_count must be a non-negative integer")};
  const auto count = raw_request.contains("message_count")
                         ? raw_request.at("message_count").get<std::uint64_t>()
                         : default_count;
  if (count == 0 || count > kMaxMessagesPerJob ||
      (operation == "step" && count != 1))
    return {422, Problem(422, "message_count_out_of_range",
                         "message_count is outside the operation bound")};
  if (raw_request.contains("sample_format") &&
      !raw_request.at("sample_format").is_string())
    return {400, Problem(400, "invalid_sample_format",
                         "sample_format must be a string")};
  const auto sample_format =
      raw_request.contains("sample_format")
          ? raw_request.at("sample_format").get<std::string>()
          : std::string{"cf32_le"};
  if (sample_format != "cf32_le" && sample_format != "cf64_le")
    return {422, Problem(422, "unsupported_sample_format",
                         "sample_format must be cf32_le or cf64_le")};
  if (raw_request.contains("timeout_ms") &&
      !raw_request.at("timeout_ms").is_number_unsigned() &&
      !(raw_request.at("timeout_ms").is_number_integer() &&
        raw_request.at("timeout_ms").get<std::int64_t>() >= 0))
    return {400, Problem(400, "invalid_timeout",
                         "timeout_ms must be a non-negative integer")};
  const auto timeout_ms =
      raw_request.contains("timeout_ms")
          ? raw_request.at("timeout_ms").get<std::uint64_t>()
          : std::uint64_t{30'000};
  if (timeout_ms < static_cast<std::uint64_t>(kMinTimeout.count()) ||
      timeout_ms > static_cast<std::uint64_t>(kMaxTimeout.count()))
    return {422, Problem(422, "timeout_out_of_range",
                         "timeout_ms must be in [100,120000]")};

  nlohmann::json request{{"operation", operation},
                         {"request_id", raw_request.at("request_id")},
                         {"message_count", count},
                         {"sample_format", sample_format},
                         {"timeout_ms", timeout_ms}};
  const auto canonical = Canonical(request);
  const auto key_digest = Digest(idempotency_key);

  std::lock_guard lock(mutex_);
  if (shutting_down_)
    return {503, Problem(503, "controller_shutting_down",
                         "job controller is shutting down")};
  if (const auto existing = idempotency_.find(key_digest);
      existing != idempotency_.end()) {
    if (existing->second.canonical_request != canonical)
      return {409,
              Problem(409, "idempotency_key_reused_with_different_payload",
                      "idempotency key is already bound to another request")};
    if (const auto job = existing->second.job.lock()) {
      auto document = JobJson(*job);
      document["idempotency"] = {{"reused", true}, {"key_digest", key_digest}};
      return {200, std::move(document)};
    }
    idempotency_.erase(existing);
  }
  PurgeUnlocked();
  if (jobs_.size() >= kMaxJobs)
    return {429,
            Problem(429, "job_history_full",
                    "bounded job history has no terminal eviction candidate")};

  const auto scenario_response = configuration_service_->GetScenarioResponse();
  auto scenario = scenario_response.at("scenario");
  if (!scenario.is_object() || !scenario.contains("messages") ||
      !scenario.at("messages").is_array() || scenario.at("messages").empty())
    return {422, Problem(422, "scenario_has_no_messages",
                         "configured scenario has no messages")};
  const auto &messages = scenario.at("messages");
  if (message_cursor_ + count > messages.size())
    return {409, Problem(409, "scenario_cursor_exhausted",
                         "reset before requesting more messages")};
  nlohmann::json selected = nlohmann::json::array();
  const auto base_start = messages.at(message_cursor_)
                              .value("transmit_start_sample", std::uint64_t{0});
  std::size_t pulse_count = 0;
  for (std::size_t index = 0; index < count; ++index) {
    auto message = messages.at(message_cursor_ + index);
    const auto start = message.value("transmit_start_sample", std::uint64_t{0});
    message["transmit_start_sample"] = start - base_start;
    pulse_count += message.at("pulses").size();
    selected.push_back(std::move(message));
  }
  if (pulse_count > kMaxPulsesPerJob)
    return {413, Problem(413, "pulse_quota_exceeded",
                         "selected messages exceed the pulse quota")};
  scenario["messages"] = std::move(selected);
  scenario.erase("receiver_input");
  const auto receiver_response =
      configuration_service_->GetReceiverGraphResponse();
  const auto &receiver_graph = receiver_response.at("graph");
  const auto channelizer =
      std::ranges::find_if(receiver_graph.at("nodes"), [](const auto &node) {
        return node.value("id", std::string{}) == "channelizer";
      });
  if (channelizer == receiver_graph.at("nodes").end() ||
      !channelizer->at("node_config").contains("iq_offsets"))
    return {422, Problem(422, "receiver_iq_map_unavailable",
                         "receiver channel IQ offsets are required for "
                         "architecture-conformant replay generation")};
  scenario["iq_offsets"] = channelizer->at("node_config").at("iq_offsets");
  for (auto &message : scenario.at("messages"))
    message["transmit_start_sample"] =
        message.at("transmit_start_sample").get<std::uint64_t>() +
        FHSSProtocolConstants::kPulsePeriodSamples;
  std::vector<std::uint32_t> active;
  for (const auto &pulse : scenario.at("messages").at(0).at("pulses")) {
    const auto index = pulse.at("frequency_index").get<std::uint32_t>();
    if (std::ranges::find(active, index) == active.end())
      active.push_back(index);
    if (active.size() == 4)
      break;
  }
  scenario["active_frequency_indices"] = active;

  auto job = std::make_shared<Job>();
  job->controller_epoch = controller_epoch_;
  job->sequence = next_job_sequence_++;
  job->job_id = "j-" + Digest(std::to_string(controller_epoch_) + ":" +
                              std::to_string(job->sequence))
                           .substr(0, 24);
  job->request_id = request.at("request_id").get<std::string>();
  job->idempotency_digest = key_digest;
  job->canonical_request = canonical;
  job->scenario_correlation_id =
      "s-" +
      Digest(std::to_string(controller_epoch_) + ":scenario").substr(0, 24);
  job->operation = operation;
  job->sample_format = sample_format;
  job->message_cursor = message_cursor_;
  job->message_count = count;
  job->timeout = std::chrono::milliseconds(timeout_ms);
  job->generator_input = std::move(scenario);
  job->config_revision =
      scenario_response.at("config_revision").get<std::uint64_t>();
  job->config_etag = scenario_response.at("etag").get<std::string>();
  job->created_at = NowRfc3339();
  job->created_monotonic = std::chrono::steady_clock::now();
  job->directory = artifact_root_ / job->job_id;
  message_cursor_ += count;
  jobs_.push_back(job);
  queue_.push_back(job);
  idempotency_[key_digest] = {canonical, job};
  cv_.notify_all();
  auto document = JobJson(*job);
  document["idempotency"] = {{"reused", false}, {"key_digest", key_digest}};
  return {202, std::move(document)};
}

FHSSJobController::Result
FHSSJobController::Get(std::string_view job_id) const {
  std::lock_guard lock(mutex_);
  const auto found = std::ranges::find(jobs_, job_id, &Job::job_id);
  if (found == jobs_.end())
    return {404, Problem(404, "job_not_found", "job does not exist")};
  return {200, JobJson(**found)};
}

FHSSJobController::Result FHSSJobController::List() const {
  std::lock_guard lock(mutex_);
  nlohmann::json entries = nlohmann::json::array();
  std::size_t bytes = 0;
  for (auto iterator = jobs_.rbegin(); iterator != jobs_.rend(); ++iterator) {
    auto entry = JobJson(**iterator);
    const auto size = entry.dump().size();
    if (entries.size() >= kMaxJobs || bytes + size > kMaxHistoryBytes)
      break;
    bytes += size;
    entries.push_back(std::move(entry));
  }
  return {200,
          {{"schema", "graphx.dashboard.fhss_job_history.v1"},
           {"controller_epoch", controller_epoch_},
           {"entries", std::move(entries)},
           {"bounds",
            {{"max_entries", kMaxJobs},
             {"max_metadata_bytes", kMaxHistoryBytes},
             {"original_count", jobs_.size()},
             {"returned_count", entries.size()},
             {"truncated", entries.size() < jobs_.size()}}}}};
}

FHSSJobController::Result FHSSJobController::Cancel(std::string_view job_id) {
  bool stop_runtime = false;
  std::shared_ptr<Job> job;
  {
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find(jobs_, job_id, &Job::job_id);
    if (found == jobs_.end())
      return {404, Problem(404, "job_not_found", "job does not exist")};
    job = *found;
    if (IsTerminal(job->state))
      return {200, JobJson(*job)};
    job->cancel_requested = true;
    if (job->state == "queued") {
      std::erase(queue_, job);
      Terminal(job, "cancelled", "cancelled_before_generation",
               "queued job cancelled without generator or receiver work");
    } else if (job->state != "cancelling") {
      Transition(job, "cancelling");
      stop_runtime = job->replay_invoked;
    }
    cv_.notify_all();
  }
  if (stop_runtime)
    (void)runtime_session_->Stop();
  return {202, Get(job_id).document};
}

FHSSJobController::Result FHSSJobController::Reset() {
  std::lock_guard lock(mutex_);
  if (const auto active = active_job_.lock();
      active && !IsTerminal(active->state))
    return {409, Problem(409, "reset_conflict_active_job",
                         "cancel the active job before reset")};
  if (std::ranges::any_of(
          queue_, [](const auto &job) { return !IsTerminal(job->state); }))
    return {409, Problem(409, "reset_conflict_queued_jobs",
                         "cancel queued jobs before reset")};
  const bool already_reset = message_cursor_ == 0 && idempotency_.empty();
  if (!already_reset)
    ++controller_epoch_;
  message_cursor_ = 0;
  idempotency_.clear();
  return {200,
          {{"schema", "graphx.dashboard.fhss_job_reset.v1"},
           {"controller_epoch", controller_epoch_},
           {"message_cursor", 0},
           {"retained_job_count", jobs_.size()},
           {"idempotency_entries_retained", 0},
           {"status", already_reset ? "already_reset" : "reset_completed"}}};
}

void FHSSJobController::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (shutting_down_)
      return;
    shutting_down_ = true;
    for (const auto &job : queue_)
      if (!IsTerminal(job->state)) {
        job->cancel_requested = true;
        Terminal(job, "cancelled", "application_shutdown",
                 "job cancelled during application shutdown");
      }
    queue_.clear();
    if (const auto active = active_job_.lock()) {
      active->cancel_requested = true;
      if (!IsTerminal(active->state) && active->state != "cancelling")
        Transition(active, "cancelling");
    }
    cv_.notify_all();
  }
  if (runtime_session_->GetState() ==
      graph::dashboard::GraphRuntimeSession::State::running)
    (void)runtime_session_->Stop();
  worker_.request_stop();
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

void FHSSJobController::Worker(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, stop_token,
               [this] { return shutting_down_ || !queue_.empty(); });
      if (stop_token.stop_requested() || shutting_down_)
        break;
      job = queue_.front();
      queue_.pop_front();
      active_job_ = job;
    }
    Process(job, stop_token);
    {
      std::lock_guard lock(mutex_);
      active_job_.reset();
    }
  }
}

void FHSSJobController::Process(const std::shared_ptr<Job> &job,
                                std::stop_token stop_token) {
  const auto deadline = std::chrono::steady_clock::now() + job->timeout;
  try {
    {
      std::lock_guard lock(mutex_);
      if (job->cancel_requested) {
        Terminal(job, "cancelled", "cancelled_before_generation",
                 "job cancelled before generation");
        return;
      }
      Transition(job, "generating");
      job->started_at = NowRfc3339();
      job->generator_invoked = true;
    }
    auto bundle = graphx::examples::fhss::GenerateIqArtifacts(
        job->generator_input, job->sample_format, kMaxIqSamples, kMaxIqBytes);
    if (bundle.truth.dump().size() > kMaxMetadataBytes ||
        bundle.sigmf.dump().size() > kMaxMetadataBytes)
      throw std::length_error("generated metadata exceeds quota");
    {
      std::lock_guard lock(mutex_);
      if (job->cancel_requested || stop_token.stop_requested()) {
        Terminal(job, "cancelled", "cancelled_during_generation",
                 "generation cancelled before artifact publication");
        return;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        Terminal(job, "timed_out", "job_timeout",
                 "job exceeded its generation deadline");
        return;
      }
    }

    if (!std::filesystem::create_directory(job->directory))
      throw std::runtime_error("job artifact directory collision");
    std::filesystem::permissions(job->directory,
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    const auto iq_name =
        job->sample_format == "cf32_le" ? "iq.cf32" : "iq.cf64";
    const auto iq = job->directory / iq_name;
    const auto truth = job->directory / "truth.withheld.json";
    const auto sigmf = job->directory / "iq.sigmf-meta";
    const auto receiver = job->directory / "receiver-minimal.json";
    const auto manifest = job->directory / "manifest.json";
    auto receiver_response = configuration_service_->GetReceiverGraphResponse();
    auto receiver_graph = receiver_response.at("graph");
    auto source =
        std::ranges::find_if(receiver_graph.at("nodes"), [](const auto &node) {
          return node.value("id", std::string{}) == "source";
        });
    if (source == receiver_graph.at("nodes").end())
      throw std::runtime_error("receiver source missing");
    (*source)["node_config"]["file_path"] = iq.string();
    (*source)["node_config"]["sample_format"] = job->sample_format;
    (*source)["node_config"]["max_read_complex_samples"] = kMaxIqSamples;
    if (ContainsForbiddenReceiverKey(receiver_graph))
      throw std::runtime_error("receiver truth isolation audit failed");
    nlohmann::json manifest_document{
        {"schema", "graphx.dashboard.fhss_job_manifest.v1"},
        {"job_id", job->job_id},
        {"controller_epoch", job->controller_epoch},
        {"scenario_correlation_id", job->scenario_correlation_id},
        {"sample_format", job->sample_format},
        {"sample_count", bundle.fixture.samples.size()},
        {"receiver_truth_access", "withheld_before_replay"},
        {"artifacts",
         {{"iq",
           {{"relative_path", iq_name},
            {"sha256", bundle.iq_sha256},
            {"bytes", bundle.iq_bytes.size()}}},
          {"truth",
           {{"relative_path", "truth.withheld.json"},
            {"sha256", HashJson(bundle.truth)}}},
          {"sigmf",
           {{"relative_path", "iq.sigmf-meta"},
            {"sha256", HashJson(bundle.sigmf)}}},
          {"receiver_config",
           {{"relative_path", "receiver-minimal.json"},
            {"sha256", HashJson(receiver_graph)}}}}}};
    const auto temporary = [](const std::filesystem::path &path) {
      return path.string() + ".tmp";
    };
    std::vector<graphx::examples::fhss::OutputFileTransaction> files{
        {temporary(iq), iq},
        {temporary(truth), truth},
        {temporary(sigmf), sigmf},
        {temporary(receiver), receiver},
        {temporary(manifest), manifest}};
    try {
      graphx::examples::fhss::WriteBytes(files[0].first, bundle.iq_bytes);
      graphx::examples::fhss::WriteJson(files[1].first, bundle.truth);
      graphx::examples::fhss::WriteJson(files[2].first, bundle.sigmf);
      graphx::examples::fhss::WriteJson(files[3].first, receiver_graph);
      graphx::examples::fhss::WriteJson(files[4].first, manifest_document);
      {
        std::lock_guard lock(mutex_);
        if (job->cancel_requested || stop_token.stop_requested() ||
            std::chrono::steady_clock::now() >= deadline) {
          for (const auto &[temporary_path, unused] : files) {
            (void)unused;
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
          }
          std::error_code ignored;
          std::filesystem::remove(job->directory, ignored);
          if (job->cancel_requested || stop_token.stop_requested())
            Terminal(job, "cancelled", "cancelled_during_artifact_write",
                     "generation cancelled before atomic artifact publication");
          else
            Terminal(job, "timed_out", "job_timeout",
                     "job exceeded its artifact publication deadline");
          return;
        }
      }
      graphx::examples::fhss::CommitOutputFiles(files, false);
      for (const auto &[unused, final_path] : files) {
        (void)unused;
        std::filesystem::permissions(final_path,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
      }
    } catch (...) {
      for (const auto &[temporary_path, unused] : files) {
        (void)unused;
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
      }
      throw;
    }
    if (!std::filesystem::remove(truth))
      throw std::runtime_error(
          "truth withholding failed before receiver replay");
    TruthWithholdingGuard truth_guard(truth, bundle.truth);
    {
      std::lock_guard lock(mutex_);
      job->artifacts = manifest_document.at("artifacts");
      job->artifacts["manifest"] = {{"relative_path", "manifest.json"},
                                    {"sha256", HashFile(manifest)}};
      Transition(job, "generated");
      if (job->cancel_requested) {
        Transition(job, "cancelling");
        truth_guard.Restore();
        Terminal(job, "cancelled", "cancelled_before_replay",
                 "job cancelled after generation and before replay");
        return;
      }
      Transition(job, "replay_pending");
    }
    auto rebuild =
        runtime_session_->Rebuild({.receiver_graph = receiver_graph,
                                   .config_revision = job->config_revision,
                                   .config_etag = job->config_etag});
    if (rebuild.status_code >= 400) {
      truth_guard.Restore();
      Terminal(job, "failed", "receiver_build_failed", rebuild.message);
      return;
    }
    auto start = runtime_session_->Start();
    if (start.status_code >= 400) {
      truth_guard.Restore();
      Terminal(job, "failed", "receiver_start_failed", start.message);
      return;
    }
    {
      std::lock_guard lock(mutex_);
      job->replay_invoked = true;
      const auto identity = runtime_session_->SnapshotGeneration();
      job->graph_generation = identity.generation;
      job->run_epoch = identity.run_epoch;
      Transition(job, "running");
    }
    for (;;) {
      bool cancel = false;
      {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(5));
        cancel = job->cancel_requested || stop_token.stop_requested();
      }
      if (cancel) {
        if (runtime_session_->GetState() ==
            graph::dashboard::GraphRuntimeSession::State::running)
          (void)runtime_session_->Stop();
        truth_guard.Restore();
        Terminal(job, "cancelled", "cancelled_during_replay",
                 "receiver replay cancelled cooperatively");
        return;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        if (runtime_session_->GetState() ==
            graph::dashboard::GraphRuntimeSession::State::running)
          (void)runtime_session_->Stop();
        truth_guard.Restore();
        Terminal(job, "timed_out", "job_timeout",
                 "job exceeded its generation and replay deadline");
        return;
      }
      const auto state = runtime_session_->GetState();
      if (state == graph::dashboard::GraphRuntimeSession::State::completed ||
          state == graph::dashboard::GraphRuntimeSession::State::failed)
        break;
    }
    const auto status = runtime_session_->SnapshotStatus();
    auto observation = observation_service_->ReceiverObservation().document;
    nlohmann::json expected_pulses = nlohmann::json::array();
    for (std::size_t index = 0;
         index < bundle.truth.at("truth_pulses").size() &&
         index < FHSSObservationService::kMaxObservedPulses;
         ++index) {
      const auto &pulse = bundle.truth.at("truth_pulses").at(index);
      expected_pulses.push_back(
          {{"global_start_sample", pulse.at("received_global_start_sample")},
           {"logical_frequency_index", pulse.at("frequency_index")},
           {"transmitted_word", pulse.at("word")}});
    }
    nlohmann::json expected{{"semantic_class", "expected"},
                            {"truth_sha256", HashJson(bundle.truth)},
                            {"config_revision", job->config_revision},
                            {"config_etag", job->config_etag},
                            {"expected_receiver_message",
                             {{"accepted", !expected_pulses.empty()},
                              {"decoded_pulse_count", expected_pulses.size()}}},
                            {"pulses", std::move(expected_pulses)}};
    auto comparison =
        FHSSObservationService::CompareDocuments(expected, observation)
            .document;
    {
      std::lock_guard lock(mutex_);
      job->graph_lifecycle = {
          {"state",
           graph::dashboard::GraphRuntimeSession::StateToString(status.state)},
          {"terminal_code", status.terminal_result_code},
          {"terminal_detail", status.terminal_result_message}};
      job->receiver_observation = {
          {"observation_id", observation.value("observation_id", "")},
          {"observation_sha256", observation.value("observation_sha256", "")}};
      job->receiver_result = observation.contains("receiver_message_result")
                                 ? observation.at("receiver_message_result")
                                 : nlohmann::json(nullptr);
      job->comparison = {
          {"evaluation_state",
           comparison.value("evaluation_state", "unavailable")},
          {"comparison_sha256", comparison.value("comparison_sha256", "")},
          {"availability",
           comparison.value("availability", nlohmann::json::object())}};
    }
    truth_guard.Restore();
    if (status.state !=
        graph::dashboard::GraphRuntimeSession::State::completed) {
      Terminal(job, "failed", "receiver_execution_failed",
               status.terminal_result_message);
      return;
    }
    if (!job->receiver_result.is_object() ||
        job->receiver_result.value("accepted", false) != true) {
      Terminal(job, "failed", "missing_receiver_message",
               "graph completed without an accepted terminal receiver message");
      return;
    }
    Terminal(job, "completed", "job_completed",
             "generation and receiver replay completed");
  } catch (const std::invalid_argument &error) {
    Terminal(job, "failed", "generator_request_invalid", error.what());
  } catch (const std::length_error &error) {
    Terminal(job, "failed", "artifact_quota_exceeded", error.what());
  } catch (const std::exception &error) {
    Terminal(job, "failed", "job_execution_failed", error.what());
  }
}

void FHSSJobController::Transition(const std::shared_ptr<Job> &job,
                                   std::string state) {
  if (job->state == state)
    return;
  if (!IsLegalTransition(job->state, state))
    throw std::logic_error("illegal FHSS job transition " + job->state +
                           " -> " + state);
  job->state = std::move(state);
}

void FHSSJobController::Terminal(const std::shared_ptr<Job> &job,
                                 std::string state, std::string code,
                                 std::string detail) {
  std::lock_guard lock(mutex_);
  if (IsTerminal(job->state))
    return;
  if (!IsLegalTransition(job->state, state)) {
    if (job->state == "cancelling" && state == "cancelled") {
      // explicitly legal; retained for clarity at the terminal-write seam
    } else {
      state = "failed";
      code = "illegal_state_transition";
      detail = "controller rejected an illegal terminal transition";
    }
  }
  job->state = std::move(state);
  job->terminal_code = std::move(code);
  job->terminal_detail = std::move(detail);
  job->terminal_at = NowRfc3339();
  cv_.notify_all();
}

nlohmann::json FHSSJobController::JobJson(const Job &job) const {
  return {
      {"schema", "graphx.dashboard.fhss_job.v1"},
      {"controller_epoch", job.controller_epoch},
      {"job_id", job.job_id},
      {"request_id", job.request_id},
      {"idempotency_key_digest", job.idempotency_digest},
      {"scenario_correlation_id", job.scenario_correlation_id},
      {"job_sequence", job.sequence},
      {"operation", job.operation},
      {"state", job.state},
      {"message_cursor", job.message_cursor},
      {"message_count", job.message_count},
      {"sample_format", job.sample_format},
      {"config_revision", job.config_revision},
      {"config_etag", job.config_etag},
      {"graph_generation", job.graph_generation},
      {"run_epoch", job.run_epoch},
      {"created_at", job.created_at},
      {"started_at", job.started_at.empty() ? nlohmann::json(nullptr)
                                            : nlohmann::json(job.started_at)},
      {"terminal_at", job.terminal_at.empty()
                          ? nlohmann::json(nullptr)
                          : nlohmann::json(job.terminal_at)},
      {"terminal",
       {{"code", job.terminal_code.empty() ? nlohmann::json(nullptr)
                                           : nlohmann::json(job.terminal_code)},
        {"detail", job.terminal_detail.empty()
                       ? nlohmann::json(nullptr)
                       : nlohmann::json(job.terminal_detail)}}},
      {"work",
       {{"generator_invoked", job.generator_invoked},
        {"receiver_replay_invoked", job.replay_invoked}}},
      {"artifacts", job.artifacts},
      {"graph_lifecycle", job.graph_lifecycle},
      {"receiver_message_result", job.receiver_result},
      {"receiver_observation", job.receiver_observation},
      {"comparison", job.comparison}};
}

void FHSSJobController::PurgeUnlocked() {
  const auto retention_cutoff =
      std::chrono::steady_clock::now() - kMaxRetentionAge;
  for (auto iterator = jobs_.begin(); iterator != jobs_.end();) {
    const auto &job = *iterator;
    if (IsTerminal(job->state) && job->created_monotonic < retention_cutoff) {
      idempotency_.erase(job->idempotency_digest);
      std::error_code ignored;
      std::filesystem::remove_all(job->directory, ignored);
      iterator = jobs_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  while (jobs_.size() >= kMaxJobs) {
    const auto found = std::ranges::find_if(
        jobs_, [](const auto &job) { return IsTerminal(job->state); });
    if (found == jobs_.end())
      return;
    idempotency_.erase((*found)->idempotency_digest);
    std::error_code ignored;
    std::filesystem::remove_all((*found)->directory, ignored);
    jobs_.erase(found);
  }
  while (idempotency_.size() >= kMaxIdempotencyEntries) {
    const auto found =
        std::ranges::find_if(idempotency_, [](const auto &entry) {
          const auto job = entry.second.job.lock();
          return !job || IsTerminal(job->state);
        });
    if (found == idempotency_.end())
      break;
    idempotency_.erase(found);
  }
}

} // namespace dsp::fhss::dashboard
