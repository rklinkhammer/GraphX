// SPDX-License-Identifier: MIT
#pragma once
#include "graph/dashboard/IGraphRuntimeOwner.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
namespace graph {
class GraphExecutor;
}
namespace graph::dashboard {
class GraphConfigurationService;
}
namespace dsp::fhss::dashboard {
class FHSSGraphRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  FHSSGraphRuntimeOwner(std::filesystem::path plugin_directory,
                        std::filesystem::path runtime_directory);
  ~FHSSGraphRuntimeOwner() override;
  Result Rebuild(std::uint64_t generation,
                 const BuildSnapshot &snapshot) override;
  Result Start(std::uint64_t generation, std::uint64_t run_epoch) override;
  Result Stop(std::uint64_t generation) override;
  Result Shutdown(std::uint64_t generation) override;
  void SetCompletionCallback(CompletionCallback callback) override;
  void SetBeforeExecuteHookForTesting(std::function<void()> hook);
  void SetExecutorTimeoutForTesting(std::chrono::seconds timeout);
  void SetAfterStartupHookForTesting(
      std::function<void(std::shared_ptr<graph::GraphManager>)> hook);

private:
  struct ReceiverInputSnapshot {
    std::filesystem::path path;
    std::string sample_format;
    std::uintmax_t byte_size = 0;
    std::uint64_t first_complex_sample = 0;
    std::uint64_t max_complex_samples = 0;
    std::uint64_t max_read_complex_samples = 0;
  };
  std::mutex operation_mutex_;
  std::mutex callback_mutex_;
  std::mutex completion_mutex_;
  std::condition_variable completion_cv_;
  std::filesystem::path plugin_directory_, runtime_directory_;
  std::filesystem::path receiver_config_path_;
  ReceiverInputSnapshot receiver_input_snapshot_;
  std::shared_ptr<graph::GraphExecutor> executor_;
  std::jthread execution_thread_;
  std::uint64_t generation_ = 0;
  bool execution_finished_ = true;
  bool executor_used_ = false;
  std::chrono::seconds executor_timeout_{30};
  CompletionCallback completion_callback_;
  std::function<void()> before_execute_hook_;
  std::function<void(std::shared_ptr<graph::GraphManager>)> after_startup_hook_;
};
} // namespace dsp::fhss::dashboard
