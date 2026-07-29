#include "graph/GraphCli.hpp"

#include <cctype>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

void PrintHelp() {
  std::cout
      << "GraphX generic graph CLI\n\n"
      << "Usage:\n"
      << "  graph-cli                         Start an interactive session\n"
      << "  graph-cli load <path>             Validate and load one graph\n"
      << "  graph-cli --graph <path> <command> [options]\n\n"
      << "Commands:\n"
      << "  load <path>\n"
      << "  save <path>\n"
      << "  show [--format table|json]\n"
      << "  list-nodes [--type TYPE] [--format table|json]\n"
      << "  get-node --id ID [--format table|json]\n"
      << "  update-node --id ID --config JSON\n"
      << "  node-count | node-ids | nodes-by-type TYPE\n"
      << "  init | start | run | stop | join | state\n"
      << "  plugins <directory>\n"
      << "  help | quit\n\n"
      << "Structural graph edits are intentionally unsupported. Save parameter "
         "changes before init.\n";
}

std::optional<std::string>
OptionValue(const std::vector<std::string> &arguments,
            const std::string_view option) {
  for (std::size_t index = 0; index + 1U < arguments.size(); ++index) {
    if (arguments[index] == option) {
      return arguments[index + 1U];
    }
  }
  return std::nullopt;
}

std::vector<std::string> Tokenize(const std::string &line) {
  std::vector<std::string> tokens;
  std::string token;
  char quote = '\0';
  bool escaped = false;
  for (const char character : line) {
    if (escaped) {
      token.push_back(character);
      escaped = false;
      continue;
    }
    if (character == '\\' && quote != '\'') {
      escaped = true;
      continue;
    }
    if (quote == '\0' && (character == '\'' || character == '"')) {
      quote = character;
      continue;
    }
    if (quote == character) {
      quote = '\0';
      continue;
    }
    if (quote == '\0' && std::isspace(static_cast<unsigned char>(character))) {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      continue;
    }
    token.push_back(character);
  }
  if (escaped) {
    token.push_back('\\');
  }
  if (!token.empty()) {
    tokens.push_back(std::move(token));
  }
  return tokens;
}

int Execute(graph::GraphCli &cli, const std::vector<std::string> &arguments) {
  if (arguments.empty()) {
    return 0;
  }
  const auto &command = arguments.front();
  if (command == "help" || command == "--help" || command == "-h") {
    PrintHelp();
    return 0;
  }
  if (command == "load") {
    if (arguments.size() != 2U) {
      std::cerr << "Error: load requires a path\n";
      return 2;
    }
    return cli.LoadGraph(arguments[1]) ? 0 : 1;
  }
  if (command == "save") {
    if (arguments.size() != 2U) {
      std::cerr << "Error: save requires a path\n";
      return 2;
    }
    return cli.SaveGraph(arguments[1]) ? 0 : 1;
  }
  if (command == "show") {
    std::cout << cli.ShowGraph(
                     OptionValue(arguments, "--format").value_or("table"))
              << '\n';
    return cli.IsGraphLoaded() ? 0 : 1;
  }
  if (command == "list-nodes") {
    std::cout << cli.ListNodes(
                     OptionValue(arguments, "--type").value_or(""),
                     OptionValue(arguments, "--format").value_or("table"))
              << '\n';
    return cli.IsGraphLoaded() ? 0 : 1;
  }
  if (command == "get-node") {
    const auto id = OptionValue(arguments, "--id");
    if (!id) {
      std::cerr << "Error: get-node requires --id\n";
      return 2;
    }
    const auto result = cli.GetNode(
        *id, OptionValue(arguments, "--format").value_or("table"));
    std::cout << result << '\n';
    return result.starts_with("Error:") ? 1 : 0;
  }
  if (command == "update-node") {
    const auto id = OptionValue(arguments, "--id");
    const auto config = OptionValue(arguments, "--config");
    if (!id || !config) {
      std::cerr << "Error: update-node requires --id and --config\n";
      return 2;
    }
    try {
      const auto parsed = nlohmann::json::parse(*config);
      if (!parsed.is_object()) {
        std::cerr << "Error: --config must be a JSON object\n";
        return 2;
      }
      return cli.UpdateNode(*id, parsed) ? 0 : 1;
    } catch (const nlohmann::json::parse_error &error) {
      std::cerr << "Error: invalid --config JSON: " << error.what() << '\n';
      return 2;
    }
  }
  if (command == "node-count") {
    if (!cli.IsGraphLoaded()) {
      std::cerr << "Error: No graph loaded\n";
      return 1;
    }
    const auto &document = cli.GetGraphJson();
    std::cout << (document.contains("nodes") && document["nodes"].is_array()
                      ? document["nodes"].size()
                      : 0U)
              << '\n';
    return 0;
  }
  if (command == "node-ids") {
    if (!cli.IsGraphLoaded()) {
      std::cerr << "Error: No graph loaded\n";
      return 1;
    }
    for (const auto &node : cli.GetGraphJson().value(
             "nodes", nlohmann::json::array())) {
      if (node.contains("id") && node["id"].is_string()) {
        std::cout << node["id"].get<std::string>() << '\n';
      }
    }
    return 0;
  }
  if (command == "nodes-by-type") {
    if (arguments.size() != 2U) {
      std::cerr << "Error: nodes-by-type requires a type\n";
      return 2;
    }
    std::cout << cli.ListNodes(arguments[1], "json") << '\n';
    return cli.IsGraphLoaded() ? 0 : 1;
  }
  if (command == "plugins") {
    if (arguments.size() != 2U) {
      std::cerr << "Error: plugins requires a directory\n";
      return 2;
    }
    cli.SetPluginDirectory(arguments[1]);
    return 0;
  }
  if (command == "init") {
    return cli.Init() ? 0 : 1;
  }
  if (command == "start") {
    return cli.Start() ? 0 : 1;
  }
  if (command == "run") {
    return cli.Run() ? 0 : 1;
  }
  if (command == "stop") {
    return cli.Stop() ? 0 : 1;
  }
  if (command == "join") {
    return cli.Join() ? 0 : 1;
  }
  if (command == "state") {
    std::cout << cli.GetState() << '\n';
    return 0;
  }
  if (command == "pause" || command == "resume" || command == "step") {
    std::cerr << "Error: " << command
              << " is not supported by GraphExecutor\n";
    return 3;
  }
  std::cerr << "Error: unknown command '" << command << "'\n";
  return 2;
}

int Interactive(graph::GraphCli &cli) {
  std::cout << "GraphX generic graph CLI. Type 'help' for commands.\n";
  std::string line;
  while (std::cout << "graphx> " && std::getline(std::cin, line)) {
    auto arguments = Tokenize(line);
    if (arguments.empty()) {
      continue;
    }
    if (arguments.front() == "quit" || arguments.front() == "exit") {
      return 0;
    }
    static_cast<void>(Execute(cli, arguments));
  }
  return 0;
}

} // namespace

int main(const int argc, char **argv) {
  graph::GraphCli cli;
  std::vector<std::string> arguments(argv + 1, argv + argc);
  if (arguments.empty()) {
    return Interactive(cli);
  }

  if (arguments.front() == "--graph") {
    if (arguments.size() < 3U) {
      std::cerr << "Error: --graph requires a path and command\n";
      return 2;
    }
    if (!cli.LoadGraph(arguments[1])) {
      return 1;
    }
    arguments.erase(arguments.begin(), arguments.begin() + 2);
    return Execute(cli, arguments);
  }
  return Execute(cli, arguments);
}
