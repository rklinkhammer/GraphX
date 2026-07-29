/**
 * @file GraphHttpServer.cpp
 * @brief Implementation of HTTP server for graph parameter viewing and execution control.
 */

#include "graph/GraphHttpServer.hpp"
#include "graph/GraphCoordinator.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/ExecutionState.hpp"

#include <nlohmann/json.hpp>
#include <thread>
#include <memory>
#include <mutex>
#include <atomic>
#include <sstream>
#include <cstring>
#include <fstream>

// Simple embedded HTTP server implementation
namespace {

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/select.h>
#endif

constexpr size_t BUFFER_SIZE = 65536;

class SimpleHttpServer {
public:
    using RequestHandler = std::function<std::string(const std::string& method,
                                                      const std::string& path,
                                                      const std::string& body,
                                                      std::string& content_type)>;

    explicit SimpleHttpServer(int port) : port_(port), running_(false), server_socket_(-1) {}

    ~SimpleHttpServer() { Stop(); }

    bool Start(RequestHandler handler) {
        if (running_) return false;

        handler_ = handler;
        server_thread_ = std::thread([this]() { RunServer(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return running_.load();
    }

    bool Stop() {
        if (!running_) return true;
        
        running_ = false;
        
        if (server_socket_ != -1) {
#ifdef _WIN32
            closesocket(server_socket_);
#else
            close(server_socket_);
#endif
            server_socket_ = -1;
        }

        if (server_thread_.joinable()) {
            server_thread_.join();
        }

        return true;
    }

    bool IsRunning() const { return running_; }

private:
    void RunServer() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return;
#endif

        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == -1) {
#ifdef _WIN32
            WSACleanup();
#endif
            return;
        }

        int reuse = 1;
#ifdef _WIN32
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
                  reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        struct sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
            closesocket(server_socket_);
            WSACleanup();
#else
            close(server_socket_);
#endif
            return;
        }

        if (listen(server_socket_, 5) < 0) {
#ifdef _WIN32
            closesocket(server_socket_);
            WSACleanup();
#else
            close(server_socket_);
#endif
            return;
        }

        running_ = true;

        while (running_) {
            struct sockaddr_in client_addr {};
            socklen_t client_addr_len = sizeof(client_addr);

#ifdef _WIN32
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_socket_, &read_fds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int select_result = select(server_socket_ + 1, &read_fds, nullptr, nullptr, &tv);
            if (select_result <= 0) continue;
#else
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_socket_, &read_fds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int select_result = select(server_socket_ + 1, &read_fds, nullptr, nullptr, &tv);
            if (select_result <= 0) continue;
#endif

            int client_socket = accept(server_socket_,
                                      reinterpret_cast<struct sockaddr*>(&client_addr),
                                      &client_addr_len);
            if (client_socket < 0) continue;

            std::thread([this, client_socket]() {
                HandleClient(client_socket);
            }).detach();
        }

#ifdef _WIN32
        closesocket(server_socket_);
        WSACleanup();
#else
        close(server_socket_);
#endif
    }

    void HandleClient(int client_socket) {
        try {
            char buffer[BUFFER_SIZE] = {0};
            int bytes_read = 0;

#ifdef _WIN32
            bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
#else
            bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
#endif

            if (bytes_read <= 0) {
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                return;
            }

            buffer[bytes_read] = '\0';

            std::string request(buffer);
            std::string method, path, body;
            ParseRequest(request, method, path, body);

            std::string content_type = "application/json";
            std::string response_body = handler_(method, path, body, content_type);

            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: " << content_type << "\r\n";
            response << "Content-Length: " << response_body.length() << "\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Access-Control-Allow-Methods: GET, POST, PATCH, OPTIONS\r\n";
            response << "Access-Control-Allow-Headers: Content-Type\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << response_body;

            std::string response_str = response.str();

#ifdef _WIN32
            send(client_socket, response_str.c_str(), response_str.length(), 0);
            closesocket(client_socket);
#else
            send(client_socket, response_str.c_str(), response_str.length(), 0);
            close(client_socket);
#endif
        } catch (...) {
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
        }
    }

    void ParseRequest(const std::string& request, std::string& method,
                     std::string& path, std::string& body) {
        std::istringstream stream(request);
        std::string line;

        if (std::getline(stream, line)) {
            std::istringstream line_stream(line);
            line_stream >> method >> path;
        }

        while (std::getline(stream, line) && line != "\r") {}
        std::getline(stream, body);
    }

    int port_;
    std::atomic<bool> running_;
    int server_socket_;
    std::thread server_thread_;
    RequestHandler handler_;
};

}  // namespace

namespace graph {

/**
 * @brief Implementation details for GraphHttpServer.
 */
class GraphHttpServer::Impl {
public:
    Impl(nlohmann::json& graph, GraphExecutor* executor, int port)
        : executor_(executor), coordinator_(graph), server_(port) {}

    bool Start();
    bool Stop();
    bool IsRunning() const;

private:
    std::string HandleRequest(const std::string& method,
                             const std::string& path,
                             const std::string& body,
                             std::string& content_type);

    std::string HandleGetGraph();
    std::string HandleGetNodes();
    std::string HandleGetNode(const std::string& id);
    std::string HandleGetNodesByType(const std::string& type);
    std::string HandlePatchNode(const std::string& id, const std::string& body);
    std::string HandleGetExecutionState();
    std::string HandlePostExecutionInit();
    std::string HandlePostExecutionStart();
    std::string HandlePostExecutionRun();
    std::string HandlePostExecutionStop();
    std::string HandlePostExecutionJoin();
    std::string HandleGetIndexHtml();

    GraphExecutor* executor_;
    GraphCoordinator coordinator_;
    SimpleHttpServer server_;
};

bool GraphHttpServer::Impl::Start() {
    auto handler = [this](const std::string& method, const std::string& path,
                         const std::string& body, std::string& content_type)
                   -> std::string {
        return HandleRequest(method, path, body, content_type);
    };

    return server_.Start(handler);
}

bool GraphHttpServer::Impl::Stop() {
    return server_.Stop();
}

bool GraphHttpServer::Impl::IsRunning() const {
    return server_.IsRunning();
}

std::string GraphHttpServer::Impl::HandleRequest(const std::string& method,
                                                 const std::string& path,
                                                 const std::string& body,
                                                 std::string& content_type) {
    if (path == "/" || path == "/index.html") {
        content_type = "text/html";
        return HandleGetIndexHtml();
    }

    if (method == "GET" && path == "/api/v1/graph") {
        return HandleGetGraph();
    }

    if (method == "GET" && path == "/api/v1/nodes") {
        return HandleGetNodes();
    }

    if (method == "GET" && path.substr(0, 16) == "/api/v1/nodes/id/") {
        std::string id = path.substr(16);
        return HandleGetNode(id);
    }

    if (method == "GET" && path.substr(0, 18) == "/api/v1/nodes/type/") {
        std::string type = path.substr(18);
        return HandleGetNodesByType(type);
    }

    if (method == "PATCH" && path.substr(0, 16) == "/api/v1/nodes/id/") {
        std::string id = path.substr(16);
        return HandlePatchNode(id, body);
    }

    if (method == "GET" && path == "/api/v1/execution/state") {
        return HandleGetExecutionState();
    }

    if (method == "POST" && path == "/api/v1/execution/init") {
        return HandlePostExecutionInit();
    }

    if (method == "POST" && path == "/api/v1/execution/start") {
        return HandlePostExecutionStart();
    }

    if (method == "POST" && path == "/api/v1/execution/run") {
        return HandlePostExecutionRun();
    }

    if (method == "POST" && path == "/api/v1/execution/stop") {
        return HandlePostExecutionStop();
    }

    if (method == "POST" && path == "/api/v1/execution/join") {
        return HandlePostExecutionJoin();
    }

    nlohmann::json response;
    response["success"] = false;
    response["error"] = "not_found";
    response["message"] = "Endpoint not found";
    return response.dump();
}

std::string GraphHttpServer::Impl::HandleGetGraph() {
    nlohmann::json response;
    response["success"] = true;
    response["data"] = coordinator_.GetGraphJson();
    return response.dump();
}

std::string GraphHttpServer::Impl::HandleGetNodes() {
    nlohmann::json response;
    auto graph = coordinator_.GetGraphJson();
    response["success"] = true;
    if (graph.contains("nodes") && graph["nodes"].is_array()) {
        response["data"] = graph["nodes"];
    } else {
        response["data"] = nlohmann::json::array();
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandleGetNode(const std::string& id) {
    nlohmann::json response;
    auto node = coordinator_.GetNode(id);
    if (node.is_null()) {
        response["success"] = false;
        response["error"] = "not_found";
        response["message"] = "Node not found: " + id;
    } else {
        response["success"] = true;
        response["data"] = node;
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandleGetNodesByType(const std::string& type) {
    nlohmann::json response;
    auto nodes = coordinator_.GetNodesByType(type);
    response["success"] = true;
    response["data"] = nlohmann::json::array();
    for (const auto& node : nodes) {
        response["data"].push_back(node);
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePatchNode(const std::string& id,
                                                   const std::string& body) {
    nlohmann::json response;

    try {
        nlohmann::json request = nlohmann::json::parse(body);

        if (!request.contains("node_config")) {
            response["success"] = false;
            response["error"] = "bad_request";
            response["message"] = "Missing 'node_config' field";
            return response.dump();
        }

        if (!coordinator_.UpdateNodeConfig(id, request["node_config"])) {
            response["success"] = false;
            response["error"] = "not_found";
            response["message"] = "Node not found: " + id;
            return response.dump();
        }

        response["success"] = true;
        response["data"] = coordinator_.GetNode(id);
        return response.dump();
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = "bad_request";
        response["message"] = std::string("Invalid JSON: ") + e.what();
        return response.dump();
    }
}

std::string GraphHttpServer::Impl::HandleGetExecutionState() {
    nlohmann::json response;
    response["success"] = true;

    if (executor_) {
        auto state = executor_->GetExecutionState();
        std::string state_str;
        switch (state) {
            case ExecutionState::INITIALIZED: state_str = "INITIALIZED"; break;
            case ExecutionState::PAUSED: state_str = "PAUSED"; break;
            case ExecutionState::RUNNING: state_str = "RUNNING"; break;
            case ExecutionState::STEPPING: state_str = "STEPPING"; break;
            case ExecutionState::STOPPING: state_str = "STOPPING"; break;
            case ExecutionState::STOPPED: state_str = "STOPPED"; break;
            case ExecutionState::ERROR: state_str = "ERROR"; break;
            default: state_str = "UNKNOWN"; break;
        }
        response["data"]["state"] = state_str;
    } else {
        response["data"]["state"] = "NOT_AVAILABLE";
        response["message"] = "No executor connected";
    }

    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePostExecutionInit() {
    nlohmann::json response;

    if (!executor_) {
        response["success"] = false;
        response["error"] = "not_available";
        response["message"] = "No executor connected";
        return response.dump();
    }

    auto result = executor_->Init();
    response["success"] = result.success;
    response["data"]["state"] = (result.success ? "INITIALIZED" : "STOPPED");
    if (!result.success) {
        response["error"] = "init_failed";
        response["message"] = result.message;
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePostExecutionStart() {
    nlohmann::json response;

    if (!executor_) {
        response["success"] = false;
        response["error"] = "not_available";
        response["message"] = "No executor connected";
        return response.dump();
    }

    auto result = executor_->Start();
    response["success"] = result.success;
    auto state = executor_->GetExecutionState();
    response["data"]["state"] = (state == ExecutionState::RUNNING ? "RUNNING" : "STOPPED");
    if (!result.success) {
        response["error"] = "start_failed";
        response["message"] = result.message;
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePostExecutionRun() {
    nlohmann::json response;

    if (!executor_) {
        response["success"] = false;
        response["error"] = "not_available";
        response["message"] = "No executor connected";
        return response.dump();
    }

    auto result = executor_->Run();
    response["success"] = result.success;
    response["data"]["state"] = "STOPPED";
    if (!result.success) {
        response["error"] = "run_failed";
        response["message"] = result.message;
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePostExecutionStop() {
    nlohmann::json response;

    if (!executor_) {
        response["success"] = false;
        response["error"] = "not_available";
        response["message"] = "No executor connected";
        return response.dump();
    }

    executor_->Stop();
    response["success"] = true;
    response["data"]["message"] = "Stop requested";
    return response.dump();
}

std::string GraphHttpServer::Impl::HandlePostExecutionJoin() {
    nlohmann::json response;

    if (!executor_) {
        response["success"] = false;
        response["error"] = "not_available";
        response["message"] = "No executor connected";
        return response.dump();
    }

    auto result = executor_->Join();
    response["success"] = result.success;
    response["data"]["state"] = "STOPPED";
    if (!result.success) {
        response["error"] = "join_failed";
        response["message"] = result.message;
    }
    return response.dump();
}

std::string GraphHttpServer::Impl::HandleGetIndexHtml() {
    // Return minimal HTML for web UI
    return R"HTML(<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width"><title>GraphX</title>
<style>body{font-family:sans-serif;margin:20px}h1{color:#333}button{padding:10px;margin:5px}.nodes{border:1px solid #ccc;padding:10px}table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:8px;text-align:left}</style></head>
<body><h1>GraphX Node Manager</h1><button onclick="loadNodes()">Refresh Nodes</button><button onclick="updateState()">Update State</button>
<div class="nodes"><h2>Nodes</h2><table id="nodeTable"><thead><tr><th>ID</th><th>Type</th><th>Config</th></tr></thead><tbody id="nodeBody"><tr><td colspan="3">Loading...</td></tr></tbody></table></div>
<script>function loadNodes(){fetch('/api/v1/nodes').then(r=>r.json()).then(d=>{const tbody=document.getElementById('nodeBody');if(d.success){tbody.innerHTML=d.data.map(n=>'<tr><td>'+n.id+'</td><td>'+n.type+'</td><td>'+JSON.stringify(n.node_config).substring(0,50)+'</td></tr>').join('')}});}
function updateState(){fetch('/api/v1/execution/state').then(r=>r.json()).then(d=>{alert('State: '+(d.data?.state||'N/A'))});}
loadNodes();</script></body></html>)HTML";
}

// ========== GraphHttpServer Implementation ==========

GraphHttpServer::GraphHttpServer(nlohmann::json& graph,
                               GraphExecutor* executor,
                               int port)
    : impl_(std::make_unique<Impl>(graph, executor, port)) {}

GraphHttpServer::~GraphHttpServer() noexcept = default;

bool GraphHttpServer::Start() {
    return impl_->Start();
}

bool GraphHttpServer::Stop() {
    return impl_->Stop();
}

bool GraphHttpServer::IsRunning() const {
    return impl_->IsRunning();
}

}  // namespace graph
