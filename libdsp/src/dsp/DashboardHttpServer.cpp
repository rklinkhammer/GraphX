#include "dsp/DashboardHttpServer.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <errno.h>

namespace graphx::dsp::dashboard {

// Boost.Beast HTTP server implementation
class DashboardHttpServer::Impl {
public:
    Options options;
    std::atomic<bool> running{false};
    std::atomic<bool> ready{true};
    std::vector<std::thread> executor_threads;
    int socket_fd = -1;
    std::mutex handler_mutex;
    std::vector<std::tuple<std::string, std::string, RequestHandler>> handlers;

    Impl(const Options& opts) : options(opts) {}

    ~Impl() {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }

    bool Bind() {
        struct sockaddr_in6 addr6{};
        struct sockaddr_in addr4{};
        struct sockaddr* addr = nullptr;
        socklen_t addr_len = 0;

        socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            socket_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_fd < 0) {
                std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
                return false;
            }
        }

        // Set socket options
        int opt = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Bind based on address family
        if (options.host == "::1") {
            addr6.sin6_family = AF_INET6;
            addr6.sin6_port = htons(options.port);
            inet_pton(AF_INET6, "::1", &addr6.sin6_addr);
            addr = (struct sockaddr*)&addr6;
            addr_len = sizeof(addr6);
        } else if (options.host == "127.0.0.1" || options.host == "127.0.0.2" ||
                   options.host == "127.0.0.3" || options.host.substr(0, 4) == "127.") {
            addr4.sin_family = AF_INET;
            addr4.sin_port = htons(options.port);
            inet_pton(AF_INET, options.host.c_str(), &addr4.sin_addr);
            addr = (struct sockaddr*)&addr4;
            addr_len = sizeof(addr4);
        } else {
            std::cerr << "Invalid loopback address: " << options.host << std::endl;
            close(socket_fd);
            socket_fd = -1;
            return false;
        }

        if (bind(socket_fd, addr, addr_len) < 0) {
            std::cerr << "Bind failed: " << strerror(errno) << std::endl;
            close(socket_fd);
            socket_fd = -1;
            return false;
        }

        if (listen(socket_fd, options.max_concurrent_connections) < 0) {
            std::cerr << "Listen failed: " << strerror(errno) << std::endl;
            close(socket_fd);
            socket_fd = -1;
            return false;
        }

        return true;
    }

    void AcceptConnections() {
        while (running) {
            struct sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);

            int client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (running) {
                    std::cerr << "Accept failed: " << strerror(errno) << std::endl;
                }
                continue;
            }

            // Set timeouts on client socket
            struct timeval tv{};
            tv.tv_sec = options.read_timeout_seconds;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            tv.tv_sec = options.write_timeout_seconds;
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            // Handle connection in a thread
            executor_threads.emplace_back(&Impl::HandleConnection, this, client_fd);
        }
    }

    void HandleConnection(int client_fd) {
        std::string request_line;
        std::vector<std::pair<std::string, std::string>> request_headers;
        std::string request_body;

        // Read request line and headers
        char buffer[4096];
        std::string raw_request;

        while (true) {
            ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) break;

            raw_request.append(buffer, bytes);
            if (raw_request.find("\r\n\r\n") != std::string::npos) {
                break;
            }

            if (raw_request.size() > options.max_header_bytes) {
                SendErrorResponse(client_fd, 413, "Payload Too Large");
                close(client_fd);
                return;
            }
        }

        // Parse request
        std::istringstream iss(raw_request);
        std::string method, path, version;
        std::getline(iss, request_line);

        std::istringstream req_line_stream(request_line);
        req_line_stream >> method >> path >> version;

        if (method.empty() || path.empty()) {
            SendErrorResponse(client_fd, 400, "Bad Request");
            close(client_fd);
            return;
        }

        // Parse headers
        std::string line;
        uint32_t body_size = 0;
        while (std::getline(iss, line)) {
            line.erase(line.find_last_not_of("\r\n") + 1);
            if (line.empty()) break;

            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                // Trim leading whitespace from value
                value.erase(0, value.find_first_not_of(" \t"));
                request_headers.push_back({key, value});

                if (key == "Content-Length") {
                    body_size = std::stoul(value);
                    if (body_size > options.max_body_bytes) {
                        SendErrorResponse(client_fd, 413, "Payload Too Large");
                        close(client_fd);
                        return;
                    }
                }
            }
        }

        // Read body if present
        if (body_size > 0) {
            char body_buffer[65536];
            uint32_t bytes_read = 0;
            while (bytes_read < body_size) {
                ssize_t bytes = recv(client_fd, body_buffer, sizeof(body_buffer), 0);
                if (bytes <= 0) break;
                request_body.append(body_buffer, bytes);
                bytes_read += bytes;
            }
        }

        // Find matching handler
        std::string response_body;
        int response_status = 404;
        std::vector<std::pair<std::string, std::string>> response_headers;

        {
            std::lock_guard<std::mutex> lock(handler_mutex);
            bool handled = false;

            for (const auto& [h_method, h_path, handler] : handlers) {
                if ((h_method.empty() || h_method == method) &&
                    (path.find(h_path) == 0)) {
                    handled = handler(method, path, request_headers, request_body,
                                    response_status, response_headers, response_body);
                    if (handled) break;
                }
            }

            if (!handled) {
                response_status = 404;
                response_body = "Not Found";
            }
        }

        // Send response
        SendResponse(client_fd, response_status, response_headers, response_body);
        close(client_fd);
    }

    void SendResponse(int client_fd, int status,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     const std::string& body) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << " " << StatusMessage(status) << "\r\n";

        for (const auto& [key, value] : headers) {
            response << key << ": " << value << "\r\n";
        }

        response << "Content-Length: " << body.size() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";

        std::string response_str = response.str();
        send(client_fd, response_str.c_str(), response_str.size(), 0);
        if (!body.empty()) {
            send(client_fd, body.c_str(), body.size(), 0);
        }
    }

    void SendErrorResponse(int client_fd, int status, const std::string& message) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << " " << StatusMessage(status) << "\r\n";
        response << "Content-Type: application/problem+json\r\n";
        response << "Content-Length: " << message.size() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << message;

        std::string response_str = response.str();
        send(client_fd, response_str.c_str(), response_str.size(), 0);
    }

    static std::string StatusMessage(int status) {
        switch (status) {
            case 200: return "OK";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 412: return "Precondition Failed";
            case 413: return "Payload Too Large";
            case 414: return "URI Too Long";
            case 429: return "Too Many Requests";
            default: return "Unknown";
        }
    }
};

// ============================================================================

DashboardHttpServer::DashboardHttpServer(const Options& options)
    : impl_(std::make_unique<Impl>(options)) {
    ValidateOptions();
}

DashboardHttpServer::~DashboardHttpServer() {
    Stop();
}

bool DashboardHttpServer::ValidateLoopbackBinding(std::string_view host) {
    // IPv6 loopback
    if (host == "::1") {
        return true;
    }

    // IPv4 loopback (127.x.x.x)
    if (host.substr(0, 4) == "127.") {
        return true;
    }

    // Reject common unsafe patterns
    if (host == "0.0.0.0" || host == "::" || host == "::/0") {
        throw std::runtime_error(
            "Loopback binding validation failed: binding to 0.0.0.0 or ::/0 is not allowed. "
            "Please use 127.0.0.1 (IPv4) or ::1 (IPv6).");
    }

    // Reject public addresses
    if (host.find(".") != std::string::npos || host.find(":") != std::string::npos) {
        throw std::runtime_error(
            std::string("Loopback binding validation failed: address '") + std::string(host) +
            "' is not a loopback address. Please use 127.0.0.1 (IPv4) or ::1 (IPv6).");
    }

    return false;
}

void DashboardHttpServer::ValidateOptions() {
    ValidateLoopbackBinding(impl_->options.host);
}

void DashboardHttpServer::RegisterHandler(
    std::string_view method,
    std::string_view path_prefix,
    RequestHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->handler_mutex);
    impl_->handlers.emplace_back(std::string(method), std::string(path_prefix), handler);
}

bool DashboardHttpServer::Start() {
    if (impl_->running) {
        return true;
    }

    if (!impl_->Bind()) {
        return false;
    }

    impl_->running = true;
    impl_->executor_threads.emplace_back(&Impl::AcceptConnections, impl_.get());

    std::cout << "Dashboard HTTP server started on " << impl_->options.host << ":"
              << impl_->options.port << std::endl;
    return true;
}

bool DashboardHttpServer::Stop() {
    if (!impl_->running) {
        return true;
    }

    impl_->ready = false;
    impl_->running = false;

    // Close socket to interrupt accept()
    if (impl_->socket_fd >= 0) {
        close(impl_->socket_fd);
        impl_->socket_fd = -1;
    }

    // Wait for threads to finish
    for (auto& thread : impl_->executor_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::cout << "Dashboard HTTP server stopped" << std::endl;
    return true;
}

bool DashboardHttpServer::IsRunning() const {
    return impl_->running;
}

}  // namespace graphx::dsp::dashboard
