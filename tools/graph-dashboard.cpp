#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphHttpServer.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void RequestStop(int) { stop_requested = 1; }

void PrintHelp() {
  std::cout
      << "GraphX generic graph dashboard\n\n"
      << "Usage:\n"
      << "  graphx-dashboard --graph PATH [--port PORT]\n"
      << "                   [--enable-execution] [--plugins DIRECTORY]\n\n"
      << "The default provides graph inspection and in-memory parameter editing "
         "while\n"
      << "execution endpoints are disabled. The server binds to 127.0.0.1.\n"
      << "Press Ctrl-C to stop it.\n";
}

std::optional<std::string> ValueAfter(const int argc, char **argv,
                                      const std::string_view option) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == option) {
      return argv[index + 1];
    }
  }
  return std::nullopt;
}

bool HasFlag(const int argc, char **argv, const std::string_view flag) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == flag) {
      return true;
    }
  }
  return false;
}

std::optional<int> ParsePort(const std::optional<std::string> &value) {
  if (!value) {
    return 8080;
  }
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoi(*value, &consumed);
    if (consumed != value->size() || parsed < 1 || parsed > 65535) {
      return std::nullopt;
    }
    return parsed;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::filesystem::path ResolveExecutable(const std::string_view argument) {
  std::error_code error;
  std::filesystem::path candidate{argument};
  if (candidate.has_parent_path()) {
    return std::filesystem::weakly_canonical(
        std::filesystem::absolute(candidate, error), error);
  }
  if (const char *path_value = std::getenv("PATH")) {
    std::string paths{path_value};
    std::size_t begin = 0;
    while (begin <= paths.size()) {
      const auto end = paths.find(':', begin);
      candidate = std::filesystem::path{
                      paths.substr(begin, end - begin)} /
                  argument;
      if (std::filesystem::is_regular_file(candidate, error)) {
        return std::filesystem::weakly_canonical(candidate, error);
      }
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1U;
    }
  }
  return std::filesystem::absolute(std::filesystem::path{argument}, error);
}

} // namespace

int main(const int argc, char **argv) {
  if (HasFlag(argc, argv, "--help") || HasFlag(argc, argv, "-h")) {
    PrintHelp();
    return 0;
  }

  const auto graph_path = ValueAfter(argc, argv, "--graph");
  const auto port = ParsePort(ValueAfter(argc, argv, "--port"));
  if (!graph_path || !port) {
    std::cerr << "Error: --graph PATH is required and --port must be 1-65535\n";
    PrintHelp();
    return 2;
  }

  nlohmann::json graph_document;
  try {
    std::ifstream graph_file(*graph_path);
    if (!graph_file) {
      std::cerr << "Error: could not open graph '" << *graph_path << "'\n";
      return 1;
    }
    graph_file >> graph_document;
    if (!graph_document.is_object() ||
        !graph_document.value("nodes", nlohmann::json::array()).is_array()) {
      std::cerr << "Error: graph must be a JSON object with a nodes array\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Error: could not load graph: " << error.what() << '\n';
    return 1;
  }

  std::shared_ptr<graph::GraphExecutor> executor;
  if (HasFlag(argc, argv, "--enable-execution")) {
    try {
      const auto plugin_directory =
          ValueAfter(argc, argv, "--plugins").value_or("./plugins");
      executor = graph::GraphExecutorBuilder()
                     .WithJsonConfig(*graph_path)
                     .WithPluginDirectory(plugin_directory)
                     .Build();
    } catch (const std::exception &error) {
      std::cerr << "Error: could not build executor: " << error.what() << '\n';
      return 1;
    }
  }

  const auto executable = ResolveExecutable(argv[0]);
  const auto index_path =
      executable.parent_path().parent_path() / "share" / "graphx" /
      "dashboard" / "index.html";
  graph::GraphHttpServer server(graph_document, executor.get(), *port,
                                index_path.string());
  if (!server.Start()) {
    std::cerr << "Error: could not bind dashboard to 127.0.0.1:" << *port
              << '\n';
    return 1;
  }

  std::signal(SIGINT, RequestStop);
  std::signal(SIGTERM, RequestStop);
  std::cout << "GraphX dashboard: http://127.0.0.1:" << *port << '\n';
  while (stop_requested == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return server.Stop() ? 0 : 1;
}
