// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "FHSSDashboardConfigurationPolicy.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <array>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

namespace {

struct HttpResponse {
  int status_code = 0;
  std::string body;
};

std::filesystem::path MakeTempAssetDirectory(const std::string &name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);

  std::ofstream index(dir / "index.html", std::ios::trunc);
  index << "<html><body>GraphX Dashboard Event Replay Test</body></html>";
  return dir;
}

nlohmann::json LoadJsonFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

HttpResponse HttpGet(std::uint16_t port, const std::string &target) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return {};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {};
  }

  const std::string request =
      "GET " + target +
      " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  ::send(fd, request.c_str(), request.size(), 0);

  std::string response;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto read = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (read <= 0) {
      break;
    }
    response.append(buffer.data(), static_cast<std::size_t>(read));
  }
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);

  HttpResponse parsed;
  const auto first_line_end = response.find("\r\n");
  if (first_line_end == std::string::npos) {
    return parsed;
  }
  {
    std::istringstream status_line(response.substr(0, first_line_end));
    std::string http;
    status_line >> http >> parsed.status_code;
  }
  const auto body_pos = response.find("\r\n\r\n");
  if (body_pos != std::string::npos) {
    parsed.body = response.substr(body_pos + 4);
  }
  return parsed;
}

std::pair<int, std::string>
RawWebSocketUpgradeRequest(std::uint16_t port, std::string host,
                           std::vector<std::string> origins,
                           std::string extra_headers = {},
                           std::string key = "dGhlIHNhbXBsZSBub25jZQ==") {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return {-1, {}};
  timeval timeout{.tv_sec = 3, .tv_usec = 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {-1, {}};
  }
  const auto authority =
      host.empty() ? "127.0.0.1:" + std::to_string(port) : std::move(host);
  const std::string request =
      "GET /api/v1/fhss/events/stream HTTP/1.1\r\nHost: " + authority +
      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: " +
      key + "\r\n";
  std::string complete_request = request;
  for (const auto &origin : origins)
    complete_request += "Origin: " + origin + "\r\n";
  complete_request += extra_headers + "\r\n";
  ::send(fd, complete_request.data(), complete_request.size(), 0);
  std::string response;
  std::array<char, 1024> bytes{};
  while (response.find("\r\n\r\n") == std::string::npos) {
    const auto count = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (count <= 0) {
      ::close(fd);
      return {-1, {}};
    }
    response.append(bytes.data(), static_cast<std::size_t>(count));
  }
  if (!response.starts_with("HTTP/1.1 101"))
    return {fd, response};
  // The server's hello is intentionally left unread. A compliant peer may
  // send its subscribe message immediately after the successful upgrade.
  return {fd, response};
}

int RawWebSocketUpgrade(std::uint16_t port) {
  const auto authority = "127.0.0.1:" + std::to_string(port);
  auto [fd, response] =
      RawWebSocketUpgradeRequest(port, authority, {"http://" + authority});
  if (fd < 0 || !response.starts_with("HTTP/1.1 101")) {
    if (fd >= 0)
      ::close(fd);
    return -1;
  }
  return fd;
}

void SendRawFrame(int fd, std::span<const std::uint8_t> payload, bool masked,
                  std::uint8_t first_byte = 0x81) {
  std::vector<std::uint8_t> frame{first_byte};
  const std::uint8_t mask_bit = masked ? 0x80 : 0;
  if (payload.size() <= 125) {
    frame.push_back(mask_bit | static_cast<std::uint8_t>(payload.size()));
  } else if (payload.size() <= 0xffff) {
    frame.push_back(mask_bit | 126);
    frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
    frame.push_back(static_cast<std::uint8_t>(payload.size()));
  } else {
    frame.push_back(mask_bit | 127);
    for (int shift = 56; shift >= 0; shift -= 8)
      frame.push_back(static_cast<std::uint8_t>(payload.size() >> shift));
  }
  constexpr std::array<std::uint8_t, 4> mask{0x12, 0x34, 0x56, 0x78};
  if (masked)
    frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t index = 0; index < payload.size(); ++index)
    frame.push_back(payload[index] ^ (masked ? mask[index % mask.size()] : 0));
  std::size_t sent = 0;
  while (sent < frame.size()) {
    const auto count = ::send(fd, frame.data() + sent, frame.size() - sent, 0);
    if (count <= 0)
      break;
    sent += static_cast<std::size_t>(count);
  }
}

struct RawCloseFrame {
  std::uint16_t code = 0;
  std::string reason;
};

std::optional<RawCloseFrame> ReadCloseFrame(int fd) {
  std::vector<std::uint8_t> pending;
  std::array<std::uint8_t, 4096> bytes{};
  for (;;) {
    const auto count = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (count <= 0)
      return std::nullopt;
    pending.insert(pending.end(), bytes.begin(), bytes.begin() + count);
    std::size_t offset = 0;
    while (offset + 2 <= pending.size()) {
      const auto opcode = pending[offset] & 0x0f;
      std::uint64_t length = pending[offset + 1] & 0x7f;
      std::size_t header = 2;
      if (length == 126) {
        if (offset + 4 > pending.size())
          break;
        length = (static_cast<std::uint64_t>(pending[offset + 2]) << 8) |
                 pending[offset + 3];
        header = 4;
      } else if (length == 127) {
        if (offset + 10 > pending.size())
          break;
        length = 0;
        for (std::size_t index = 0; index < 8; ++index)
          length = (length << 8) | pending[offset + 2 + index];
        header = 10;
      }
      if (length > pending.size() || offset + header + length > pending.size())
        break;
      if (opcode == 0x8) {
        RawCloseFrame close;
        if (length >= 2) {
          close.code = static_cast<std::uint16_t>(
              (pending[offset + header] << 8) | pending[offset + header + 1]);
          close.reason.assign(reinterpret_cast<const char *>(
                                  pending.data() + offset + header + 2),
                              static_cast<std::size_t>(length - 2));
        }
        return close;
      }
      offset += header + static_cast<std::size_t>(length);
    }
    pending.erase(pending.begin(), pending.begin() + offset);
  }
}

void ExpectCloseFrame(int fd, std::uint16_t code, std::string_view reason) {
  const auto close = ReadCloseFrame(fd);
  ASSERT_TRUE(close.has_value()) << "expected RFC6455 close frame";
  EXPECT_EQ(close->code, code);
  EXPECT_EQ(close->reason, reason);
}

std::pair<bool, bool> ReadUntilPongAndText(int fd, std::string_view needle) {
  std::vector<std::uint8_t> pending;
  std::array<std::uint8_t, 4096> bytes{};
  bool pong = false;
  bool text_match = false;
  while (!pong || !text_match) {
    const auto count = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (count <= 0)
      break;
    pending.insert(pending.end(), bytes.begin(), bytes.begin() + count);
    std::size_t offset = 0;
    while (offset + 2 <= pending.size()) {
      const auto opcode = pending[offset] & 0x0f;
      std::uint64_t length = pending[offset + 1] & 0x7f;
      std::size_t header = 2;
      if (length == 126) {
        if (offset + 4 > pending.size())
          break;
        length = (static_cast<std::uint64_t>(pending[offset + 2]) << 8) |
                 pending[offset + 3];
        header = 4;
      } else if (length == 127) {
        if (offset + 10 > pending.size())
          break;
        length = 0;
        for (std::size_t index = 0; index < 8; ++index)
          length = (length << 8) | pending[offset + 2 + index];
        header = 10;
      }
      if (offset + header + length > pending.size())
        break;
      if (opcode == 0x0a)
        pong = true;
      if (opcode == 0x01) {
        const std::string_view payload(
            reinterpret_cast<const char *>(pending.data() + offset + header),
            static_cast<std::size_t>(length));
        text_match = text_match || payload.contains(needle);
      }
      offset += header + static_cast<std::size_t>(length);
    }
    pending.erase(pending.begin(), pending.begin() + offset);
  }
  return {pong, text_match};
}

class FhssDashboardEventReplayTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step6_assets");
    config_ =
        LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            config_,
            std::make_shared<
                dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
    runtime_session_ =
        std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ =
        std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.port = 0;
    options.asset_directory = assets_;

    server_ = std::make_unique<graph::dashboard::EmbeddedDashboardServer>(
        options, configuration_service_, runtime_session_, snapshot_collector_);
    ASSERT_TRUE(server_->Start()) << server_->LastError();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
    }
    std::error_code error;
    std::filesystem::remove_all(assets_, error);
  }

  nlohmann::json
  PollEvents(const std::string &client_id,
             std::optional<std::uint64_t> last_sequence = std::nullopt,
             bool disconnect = false) {
    std::ostringstream target;
    target << "/api/v1/fhss/events?client_id=" << client_id;
    if (last_sequence.has_value()) {
      target << "&last_sequence=" << *last_sequence;
    }
    if (disconnect) {
      target << "&disconnect=1";
    }
    const auto response = HttpGet(server_->BoundPort(), target.str());
    EXPECT_EQ(response.status_code, 200) << response.body;
    return nlohmann::json::parse(response.body);
  }

  void RestartWithOptions(
      graph::dashboard::EmbeddedDashboardServer::Options options) {
    server_->Stop();
    options.port = 0;
    options.asset_directory = assets_;
    server_ = std::make_unique<graph::dashboard::EmbeddedDashboardServer>(
        std::move(options), configuration_service_, runtime_session_,
        snapshot_collector_);
    ASSERT_TRUE(server_->Start()) << server_->LastError();
  }

  std::filesystem::path assets_;
  nlohmann::json config_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardEventReplayTest, EventsUseMonotonicSequenceContract) {
  server_->PublishEvent("status", nlohmann::json{{"state", "ready"}});
  server_->PublishEvent("metrics", nlohmann::json{{"graph_total_enqueued", 1}});
  server_->PublishEvent("diagnostics", nlohmann::json{{"ok", true}});

  const auto batch = PollEvents("seq-client", 0);
  ASSERT_TRUE(batch.contains("events"));
  ASSERT_GE(batch.at("events").size(), 3u);
  EXPECT_FALSE(batch.at("resync_required").get<bool>());
  EXPECT_EQ(batch.at("reason"), "none");
  EXPECT_FALSE(batch.at("truncated").get<bool>());
  EXPECT_LE(batch.at("oldest_available_sequence").get<std::uint64_t>(),
            batch.at("newest_available_sequence").get<std::uint64_t>());

  std::uint64_t previous = 0;
  for (const auto &event : batch.at("events")) {
    const auto sequence = event.at("sequence").get<std::uint64_t>();
    EXPECT_GT(sequence, previous);
    previous = sequence;
  }
}

TEST_F(FhssDashboardEventReplayTest,
       MaintainedWebSocketClientNegotiatesAndReceivesProductionEnvelope) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;

  asio::io_context context;
  tcp::resolver resolver(context);
  websocket::stream<tcp::socket> client(context);
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");

  beast::flat_buffer buffer;
  client.read(buffer);
  const auto hello =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  EXPECT_EQ(hello.at("schema"), "graphx.dashboard.websocket_hello.v1");
  ASSERT_TRUE(hello.at("publisher_epoch").is_string());
  EXPECT_EQ(hello.at("publisher_epoch").get<std::string>().size(), 32u);
  EXPECT_EQ(hello.at("latest_sequence"), 0u);

  client.write(asio::buffer(
      nlohmann::json{{"action", "subscribe"},
                     {"client_id", "beast-client"},
                     {"last_sequence", 0},
                     {"publisher_epoch", hello.at("publisher_epoch")}}
          .dump()));
  server_->PublishEvent("job_terminal", {{"state", "completed"}},
                        {{"controller_epoch", "controller-1"},
                         {"job_id", "job-1"},
                         {"correlation_id", "correlation-1"},
                         {"semantic_class", "receiver_result"}});

  buffer.clear();
  client.read(buffer);
  const auto event =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  EXPECT_EQ(event.at("schema"), "graphx.dashboard.event.v1");
  EXPECT_EQ(event.at("publisher_epoch"), hello.at("publisher_epoch"));
  EXPECT_EQ(event.at("job_id"), "job-1");
  EXPECT_EQ(event.at("correlation_id"), "correlation-1");
  EXPECT_EQ(event.at("semantic_class"), "receiver_result");
  EXPECT_TRUE(event.contains("generation"));
  EXPECT_TRUE(event.contains("run_epoch"));
  EXPECT_TRUE(event.contains("config_revision"));
  EXPECT_TRUE(event.contains("config_etag"));
  boost::system::error_code ignored;
  client.close(websocket::close_code::normal, ignored);
}

TEST_F(FhssDashboardEventReplayTest,
       FreshBrowserEquivalentReceivesRetainedFramesThroughHelloBoundary) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;

  server_->PublishEvent("status", {{"ordinal", 1}});
  server_->PublishEvent("status", {{"ordinal", 2}});

  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  tcp::resolver resolver(context);
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");

  beast::flat_buffer buffer;
  client.read(buffer);
  const auto hello =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  ASSERT_EQ(hello.at("latest_sequence"), 2u);
  client.write(
      asio::buffer(nlohmann::json{{"action", "subscribe"},
                                  {"client_id", "fresh-browser-equivalent"},
                                  {"publisher_epoch", ""},
                                  {"last_sequence", 0}}
                       .dump()));

  for (std::uint64_t expected = 1; expected <= 2; ++expected) {
    buffer.clear();
    client.read(buffer);
    const auto event =
        nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
    EXPECT_EQ(event.at("schema"), "graphx.dashboard.event.v1");
    EXPECT_EQ(event.at("sequence"), expected);
    EXPECT_LE(event.at("sequence").get<std::uint64_t>(),
              hello.at("latest_sequence").get<std::uint64_t>());
  }
  boost::system::error_code ignored;
  client.close(websocket::close_code::normal, ignored);
}

TEST(FhssDashboardBrowserScriptTest,
     EventPayloadCannotShadowDocumentAndReplayUpdatesTransportDom) {
  const auto script_path = std::filesystem::path(GRAPHX_SOURCE_ROOT) /
      "examples/DSP/dashboard/frontend/src/useEventTransport.ts";
  std::ifstream input(script_path);
  ASSERT_TRUE(input.good()) << script_path;
  const std::string script((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const auto transport_path = std::filesystem::path(GRAPHX_SOURCE_ROOT) /
      "examples/DSP/dashboard/frontend/src/transportState.ts";
  std::ifstream transport_input(transport_path);
  ASSERT_TRUE(transport_input.good()) << transport_path;
  const std::string transport((std::istreambuf_iterator<char>(transport_input)),
                              std::istreambuf_iterator<char>());
  EXPECT_NE(script.find("const raw: unknown = JSON.parse"), std::string::npos);
  EXPECT_EQ(script.find("let document;"), std::string::npos);
  EXPECT_NE(script.find("/api/v1/fhss/snapshot"), std::string::npos);
  EXPECT_NE(script.find("/api/v1/fhss/events?client_id=${CLIENT}"),
            std::string::npos);
  EXPECT_NE(script.find("/api/v1/fhss/events/stream"), std::string::npos);
  EXPECT_NE(script.find("heartbeat_ack"), std::string::npos);
  EXPECT_NE(script.find("bounded polling fallback"), std::string::npos);
  EXPECT_NE(script.find("parseHello(raw)"), std::string::npos);
  EXPECT_NE(script.find("parseEventBatch"), std::string::npos);
  EXPECT_NE(script.find("parseHeartbeat"), std::string::npos);
  EXPECT_NE(script.find("parseResyncRequired"), std::string::npos);
  EXPECT_NE(script.find("sessionStorage"), std::string::npos);
  EXPECT_NE(script.find("reconnect budget exhausted"), std::string::npos);
  EXPECT_NE(transport.find("exactFields"), std::string::npos);
  EXPECT_NE(transport.find("nextReconnect"), std::string::npos);
  EXPECT_NE(transport.find("'duplicate'"), std::string::npos);
  EXPECT_NE(transport.find("'gap'"), std::string::npos);
  EXPECT_NE(transport.find("'resync'"), std::string::npos);
}

TEST_F(FhssDashboardEventReplayTest,
       ProductionSnapshotPublisherCoversRuntimeMetricsAndDiagnostics) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler = [](const auto &, const auto &)
              -> std::optional<
                  graph::dashboard::EmbeddedDashboardServer::ApiResponse> {
            return std::nullopt;
          },
          .cooperative_cancellation = true,
          .maximum_checkpoint_latency = std::chrono::milliseconds(1)};
  RestartWithOptions(options);
  std::set<std::string> types;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline && types.size() < 3) {
    const auto batch = PollEvents("publisher-coverage", 0);
    for (const auto &event : batch.at("events"))
      types.insert(event.at("event_type").get<std::string>());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_TRUE(types.contains("runtime_status"));
  EXPECT_TRUE(types.contains("metrics"));
  EXPECT_TRUE(types.contains("diagnostics"));
}

TEST_F(FhssDashboardEventReplayTest, WebSocketRejectsCrossOriginUpgrade) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;

  asio::io_context context;
  tcp::resolver resolver(context);
  websocket::stream<tcp::socket> client(context);
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  client.set_option(
      websocket::stream_base::decorator([](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin,
                    "https://attacker.invalid");
      }));
  boost::system::error_code error;
  client.handshake(authority, "/api/v1/fhss/events/stream", error);
  EXPECT_EQ(error, websocket::error::upgrade_declined);
}

TEST_F(FhssDashboardEventReplayTest,
       FragmentedSubscribeIsReassembledByMaintainedProtocolStack) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  tcp::resolver resolver(context);
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");
  beast::flat_buffer buffer;
  client.read(buffer);
  const auto hello =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  client.auto_fragment(true);
  client.write_buffer_bytes(8);
  client.write(asio::buffer(
      nlohmann::json{{"action", "subscribe"},
                     {"client_id", "fragmented"},
                     {"last_sequence", 0},
                     {"publisher_epoch", hello.at("publisher_epoch")}}
          .dump()));
  server_->PublishEvent("fragment_test", {{"independent_expected", 17}});
  buffer.clear();
  client.read(buffer);
  const auto event =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  EXPECT_EQ(event.at("event_type"), "fragment_test");
  EXPECT_EQ(event.at("payload").at("independent_expected"), 17);
  boost::system::error_code ignored;
  client.close(websocket::close_code::normal, ignored);
}

TEST_F(FhssDashboardEventReplayTest,
       PingInterleavedInsideFragmentedSubscribePreservesMessage) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string subscription =
      R"({"action":"subscribe","client_id":"interleaved","last_sequence":0})";
  const auto split = subscription.size() / 2;
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                split),
      true, 0x01);
  const std::array<std::uint8_t, 1> ping{'p'};
  SendRawFrame(fd, ping, true, 0x89);
  SendRawFrame(fd,
               std::span(reinterpret_cast<const std::uint8_t *>(
                             subscription.data() + split),
                         subscription.size() - split),
               true, 0x80);
  server_->PublishEvent("interleaved_control", {{"accepted", true}});
  const auto [pong, event] = ReadUntilPongAndText(fd, "interleaved_control");
  EXPECT_TRUE(pong);
  EXPECT_TRUE(event);
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest, MissingOriginIsRejectedBeforeUpgrade) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  tcp::resolver resolver(context);
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  boost::system::error_code error;
  client.handshake("127.0.0.1:" + std::to_string(server_->BoundPort()),
                   "/api/v1/fhss/events/stream", error);
  EXPECT_EQ(error, websocket::error::upgrade_declined);
}

TEST_F(FhssDashboardEventReplayTest,
       RequesterControlledHostCannotAuthorizeForeignOrigin) {
  auto [fd, response] = RawWebSocketUpgradeRequest(
      server_->BoundPort(), "attacker.invalid", {"http://attacker.invalid"});
  ASSERT_GE(fd, 0);
  EXPECT_TRUE(response.starts_with("HTTP/1.1 403"));
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest, DuplicateOriginIsRejectedBeforeUpgrade) {
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  auto [fd, response] = RawWebSocketUpgradeRequest(
      server_->BoundPort(), authority,
      {"http://" + authority, "http://" + authority});
  ASSERT_GE(fd, 0);
  EXPECT_TRUE(response.starts_with("HTTP/1.1 403"));
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest,
       ExtensionsMalformedAndDuplicateKeysAndSubprotocolsAreQualified) {
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  {
    auto [fd, response] = RawWebSocketUpgradeRequest(
        server_->BoundPort(), authority, {"http://" + authority},
        "Sec-WebSocket-Extensions: permessage-deflate\r\n");
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(response.starts_with("HTTP/1.1 101"));
    EXPECT_EQ(response.find("Sec-WebSocket-Extensions"), std::string::npos);
    ::close(fd);
  }
  {
    auto [fd, response] = RawWebSocketUpgradeRequest(
        server_->BoundPort(), authority, {"http://" + authority},
        "Sec-WebSocket-Protocol: graphx.v1\r\n");
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(response.starts_with("HTTP/1.1 400"));
    ::close(fd);
  }
  {
    auto [fd, response] =
        RawWebSocketUpgradeRequest(server_->BoundPort(), authority,
                                   {"http://" + authority}, {}, "invalid-key");
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(response.starts_with("HTTP/1.1 400"));
    ::close(fd);
  }
  {
    auto [fd, response] = RawWebSocketUpgradeRequest(
        server_->BoundPort(), authority, {"http://" + authority},
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(response.starts_with("HTTP/1.1 400"));
    ::close(fd);
  }
}

TEST_F(FhssDashboardEventReplayTest,
       ServerShutdownClosesSubscribedClientWithinBound) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  tcp::resolver resolver(context);
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");
  beast::flat_buffer buffer;
  client.read(buffer);
  client.write(asio::buffer(nlohmann::json{{"action", "subscribe"},
                                           {"client_id", "shutdown-client"},
                                           {"last_sequence", 0}}
                                .dump()));
  auto stopped = std::async(std::launch::async, [this] { server_->Stop(); });
  boost::system::error_code error;
  buffer.clear();
  client.read(buffer, error);
  EXPECT_TRUE(error == websocket::error::closed || error == asio::error::eof ||
              error == asio::error::connection_reset);
  EXPECT_EQ(stopped.wait_for(std::chrono::seconds(6)),
            std::future_status::ready);
  stopped.get();
}

TEST_F(FhssDashboardEventReplayTest,
       IncompleteFragmentCannotBlockServerShutdown) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string partial = R"({"action":"subscribe")";
  SendRawFrame(fd,
               std::span(reinterpret_cast<const std::uint8_t *>(partial.data()),
                         partial.size()),
               true, 0x01); // text, FIN=0: continuation never arrives
  auto stopped = std::async(std::launch::async, [this] { server_->Stop(); });
  EXPECT_EQ(stopped.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  stopped.get();
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest, GracefulPeerCloseEchoesCodeAndReason) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::array<std::uint8_t, 6> close_payload{0x03, 0xe8, 'd',
                                                  'o',  'n',  'e'};
  SendRawFrame(fd, close_payload, true, 0x88);
  // RFC 6455 requires the status code to be echoed; the reason is optional.
  ExpectCloseFrame(fd, 1000, "");
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest,
       MaximumConnectionLifetimeUsesStableGoingAwayClose) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.websocket_max_lifetime = std::chrono::milliseconds(200);
  options.websocket_heartbeat_interval = std::chrono::seconds(1);
  RestartWithOptions(options);
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string subscription =
      R"({"action":"subscribe","client_id":"lifetime","last_sequence":0})";
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                subscription.size()),
      true);
  ExpectCloseFrame(fd, 1001, "connection lifetime reached");
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest, NonUpgradeStreamRequestRequiresUpgrade) {
  const auto response =
      HttpGet(server_->BoundPort(), "/api/v1/fhss/events/stream");
  EXPECT_EQ(response.status_code, 426);
  const auto problem = nlohmann::json::parse(response.body);
  EXPECT_EQ(problem.at("status"), 426);
  EXPECT_EQ(problem.at("code"), "websocket_upgrade_required");
}

TEST_F(FhssDashboardEventReplayTest, UnmaskedClientDataFailsSafely) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string subscription =
      R"({"action":"subscribe","client_id":"unmasked","last_sequence":0})";
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                subscription.size()),
      false);
  ExpectCloseFrame(fd, 1002, "");
  ::close(fd);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardEventReplayTest, InvalidUtf8ClientDataFailsSafely) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::array<std::uint8_t, 2> invalid_utf8{0xc3, 0x28};
  SendRawFrame(fd, invalid_utf8, true);
  ExpectCloseFrame(fd, 1007, "");
  ::close(fd);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardEventReplayTest,
       InvalidOpcodesRsvControlAndBinaryFramesFailSafely) {
  const std::array<std::uint8_t, 2> payload{'{', '}'};
  for (const auto [first_byte, expected_code, expected_reason] :
       {std::tuple{std::uint8_t{0x83}, std::uint16_t{1002}, ""},
        std::tuple{std::uint8_t{0xc1}, std::uint16_t{1002}, ""},
        std::tuple{std::uint8_t{0x09}, std::uint16_t{1002}, ""},
        std::tuple{std::uint8_t{0x80}, std::uint16_t{1002}, ""},
        std::tuple{std::uint8_t{0x82}, std::uint16_t{1003},
                   "text commands required"}}) {
    const int fd = RawWebSocketUpgrade(server_->BoundPort());
    ASSERT_GE(fd, 0);
    SendRawFrame(fd, payload, true, first_byte);
    ExpectCloseFrame(fd, expected_code, expected_reason);
    ::close(fd);
  }
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardEventReplayTest,
       OversizedControlAndInvalidCloseCodeFailSafely) {
  {
    const int fd = RawWebSocketUpgrade(server_->BoundPort());
    ASSERT_GE(fd, 0);
    std::vector<std::uint8_t> payload(126, 'p');
    SendRawFrame(fd, payload, true, 0x89);
    ExpectCloseFrame(fd, 1002, "");
    ::close(fd);
  }
  {
    const int fd = RawWebSocketUpgrade(server_->BoundPort());
    ASSERT_GE(fd, 0);
    const std::array<std::uint8_t, 2> reserved_close{0x03, 0xed};
    SendRawFrame(fd, reserved_close, true, 0x88);
    ExpectCloseFrame(fd, 1002, "");
    ::close(fd);
  }
  {
    const int fd = RawWebSocketUpgrade(server_->BoundPort());
    ASSERT_GE(fd, 0);
    const std::array<std::uint8_t, 4> invalid_utf8_close_reason{0x03, 0xe8,
                                                                0xc3, 0x28};
    SendRawFrame(fd, invalid_utf8_close_reason, true, 0x88);
    ExpectCloseFrame(fd, 1002, "");
    ::close(fd);
  }
}

TEST_F(FhssDashboardEventReplayTest,
       FragmentCountLimitRejectsExcessiveFragmentation) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_fragments_per_message = 2;
  RestartWithOptions(options);
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::array<std::uint8_t, 1> first{'{'};
  const std::array<std::uint8_t, 1> middle{' '};
  const std::array<std::uint8_t, 1> last{'}'};
  SendRawFrame(fd, first, true, 0x01);
  SendRawFrame(fd, middle, true, 0x00);
  SendRawFrame(fd, last, true, 0x80);
  ExpectCloseFrame(fd, 1009, "frame or message limit exceeded");
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest, SubscribeUnknownFieldsAreRejected) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string subscription =
      R"({"action":"subscribe","client_id":"unknown-field","last_sequence":0,"unexpected":true})";
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                subscription.size()),
      true);
  ExpectCloseFrame(fd, 1008, "invalid subscribe command");
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest,
       MalformedTypedSubscribeAndHeartbeatCommandsCloseWithPolicyError) {
  const std::array<std::string, 6> invalid_subscriptions{
      R"({"action":7,"client_id":"typed"})",
      R"({"action":"subscribe","client_id":"typed","publisher_epoch":7})",
      R"({"action":"subscribe","client_id":"typed","last_sequence":-1})",
      R"({"action":"subscribe","client_id":"typed","last_sequence":"1"})",
      R"({"action":"subscribe","client_id":"typed","last_sequence":1.5})",
      R"({"action":"subscribe","client_id":"typed","last_sequence":18446744073709551616})"};
  for (const auto &subscription : invalid_subscriptions) {
    const int fd = RawWebSocketUpgrade(server_->BoundPort());
    ASSERT_GE(fd, 0);
    SendRawFrame(
        fd,
        std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                  subscription.size()),
        true);
    ExpectCloseFrame(fd, 1008, "invalid subscribe command");
    ::close(fd);
    EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
  }

  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  const std::string subscription =
      R"({"action":"subscribe","client_id":"typed-heartbeat","last_sequence":0})";
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(subscription.data()),
                subscription.size()),
      true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const std::string heartbeat =
      R"({"action":"heartbeat_ack","publisher_epoch":7})";
  SendRawFrame(
      fd,
      std::span(reinterpret_cast<const std::uint8_t *>(heartbeat.data()),
                heartbeat.size()),
      true);
  ExpectCloseFrame(fd, 1008, "invalid heartbeat acknowledgement");
  ::close(fd);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardEventReplayTest,
       CommandRateLimitClosesWithStablePolicyReason) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_commands_per_second = 1;
  options.websocket_heartbeat_interval = std::chrono::seconds(2);
  RestartWithOptions(options);
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  tcp::resolver resolver(context);
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");
  beast::flat_buffer buffer;
  client.read(buffer);
  const auto hello =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  client.write(asio::buffer(
      nlohmann::json{{"action", "subscribe"},
                     {"client_id", "command-rate"},
                     {"last_sequence", hello.at("latest_sequence")}}
          .dump()));
  const auto acknowledgement =
      nlohmann::json{{"action", "heartbeat_ack"},
                     {"publisher_epoch", hello.at("publisher_epoch")}}
          .dump();
  client.write(asio::buffer(acknowledgement));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.write(asio::buffer(acknowledgement));
  boost::system::error_code error;
  buffer.clear();
  client.read(buffer, error);
  EXPECT_EQ(error, websocket::error::closed);
  EXPECT_EQ(client.reason().code, websocket::close_code::policy_error);
  EXPECT_EQ(client.reason().reason, "command rate limit exceeded");
}

TEST_F(FhssDashboardEventReplayTest,
       HeartbeatAcknowledgementKeepsMaintainedClientAlive) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.websocket_heartbeat_interval = std::chrono::milliseconds(200);
  options.websocket_idle_timeout = std::chrono::milliseconds(1200);
  RestartWithOptions(options);
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;
  asio::io_context context;
  websocket::stream<tcp::socket> client(context);
  tcp::resolver resolver(context);
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  asio::connect(
      client.next_layer(),
      resolver.resolve("127.0.0.1", std::to_string(server_->BoundPort())));
  client.set_option(
      websocket::stream_base::decorator([&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  client.handshake(authority, "/api/v1/fhss/events/stream");
  beast::flat_buffer buffer;
  client.read(buffer);
  const auto hello =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  client.write(asio::buffer(
      nlohmann::json{{"action", "subscribe"},
                     {"client_id", "heartbeat-maintained"},
                     {"last_sequence", hello.at("latest_sequence")}}
          .dump()));
  buffer.clear();
  boost::system::error_code error;
  client.read(buffer, error);
  ASSERT_FALSE(error) << error.message() << "; close="
                      << static_cast<unsigned>(client.reason().code);
  const auto heartbeat =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  ASSERT_EQ(heartbeat.at("schema"), "graphx.dashboard.websocket_heartbeat.v1");
  client.write(asio::buffer(
      nlohmann::json{{"action", "heartbeat_ack"},
                     {"publisher_epoch", heartbeat.at("publisher_epoch")}}
          .dump()));
  server_->PublishEvent("after_heartbeat", {{"alive", true}});
  buffer.clear();
  client.read(buffer, error);
  ASSERT_FALSE(error) << error.message();
  const auto event =
      nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
  EXPECT_EQ(event.at("event_type"), "after_heartbeat");
  client.close(websocket::close_code::normal, error);
}

TEST_F(FhssDashboardEventReplayTest, OversizedReassembledMessageFailsSafely) {
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  std::vector<std::uint8_t> oversized(300 * 1024, 'x');
  SendRawFrame(fd, oversized, true);
  ExpectCloseFrame(fd, 1009, "");
  ::close(fd);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardEventReplayTest,
       FrameLimitIsEnforcedBelowReassembledMessageLimit) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_frame_bytes = 512;
  options.max_websocket_message_bytes = 4096;
  options.max_websocket_queue_bytes = 8192;
  options.max_retained_event_bytes = 8192;
  RestartWithOptions(options);
  const int fd = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(fd, 0);
  std::vector<std::uint8_t> oversized_frame(1024, 'x');
  SendRawFrame(fd, oversized_frame, true);
  ExpectCloseFrame(fd, 1009, "frame or message limit exceeded");
  ::close(fd);
}

TEST_F(FhssDashboardEventReplayTest,
       ReplayResumeRequiresContiguousRetainedRangeOnly) {
  server_->PublishEvent("command", nlohmann::json{{"code", "a"}});
  server_->PublishEvent("command", nlohmann::json{{"code", "b"}});

  const auto first = PollEvents("resume-client", 0);
  ASSERT_FALSE(first.at("events").empty());
  const auto last_seen =
      first.at("events").back().at("sequence").get<std::uint64_t>();

  server_->PublishEvent("fhss_progress", nlohmann::json{{"step", 1}});
  server_->PublishEvent("fhss_progress", nlohmann::json{{"step", 2}});

  (void)PollEvents("resume-client", std::nullopt, true);
  const auto resumed = PollEvents("resume-client", last_seen);
  EXPECT_FALSE(resumed.at("resync_required").get<bool>());
  ASSERT_EQ(resumed.at("events").size(), 2u);
  EXPECT_EQ(resumed.at("events").at(0).at("sequence").get<std::uint64_t>(),
            last_seen + 1);
  EXPECT_EQ(resumed.at("events").at(1).at("sequence").get<std::uint64_t>(),
            last_seen + 2);
}

TEST_F(FhssDashboardEventReplayTest, DisconnectDoesNotLeakClientCapacity) {
  for (int index = 0; index < 32; ++index) {
    const auto response = HttpGet(server_->BoundPort(),
                                  "/api/v1/fhss/events?client_id=ephemeral-" +
                                      std::to_string(index) + "&disconnect=1");
    ASSERT_EQ(response.status_code, 200);
  }
  EXPECT_EQ(HttpGet(server_->BoundPort(),
                    "/api/v1/fhss/events?client_id=still-available")
                .status_code,
            200);
}

TEST_F(FhssDashboardEventReplayTest, ExpiredClientStateReleasesCapacity) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_clients = 2;
  options.websocket_client_state_ttl = std::chrono::milliseconds(40);
  RestartWithOptions(options);

  EXPECT_EQ(HttpGet(server_->BoundPort(), "/api/v1/fhss/events?client_id=ttl-a")
                .status_code,
            200);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/api/v1/fhss/events?client_id=ttl-b")
                .status_code,
            200);
  EXPECT_EQ(
      HttpGet(server_->BoundPort(), "/api/v1/fhss/events?client_id=ttl-full")
          .status_code,
      429);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  EXPECT_EQ(HttpGet(server_->BoundPort(),
                    "/api/v1/fhss/events?client_id=ttl-reclaimed")
                .status_code,
            200);
}

TEST_F(FhssDashboardEventReplayTest,
       ConcurrentDistinctClientsCannotOversubscribeReservedCapacity) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_clients = 2;
  options.max_concurrent_connections = 16;
  RestartWithOptions(options);

  std::barrier start(9);
  std::array<int, 8> statuses{};
  std::array<std::jthread, 8> clients;
  for (std::size_t index = 0; index < clients.size(); ++index) {
    clients[index] = std::jthread([&, index] {
      start.arrive_and_wait();
      statuses[index] = HttpGet(server_->BoundPort(),
                                "/api/v1/fhss/events?client_id=concurrent-" +
                                    std::to_string(index))
                            .status_code;
    });
  }
  start.arrive_and_wait();
  for (auto &client : clients)
    client.join();
  EXPECT_EQ(std::ranges::count(statuses, 200), 2);
  EXPECT_EQ(std::ranges::count(statuses, 429), 6);
}

TEST_F(FhssDashboardEventReplayTest,
       MissingOrExpiredRangeForcesResyncRequired) {
  server_->SetEventRetentionForTesting(std::chrono::milliseconds(1));
  server_->PublishEvent("status", nlohmann::json{{"phase", "initial"}});
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server_->PublishEvent("status", nlohmann::json{{"phase", "after-expire"}});

  const auto reconnect = PollEvents("gap-client", 0);
  EXPECT_TRUE(reconnect.at("resync_required").get<bool>());
  EXPECT_TRUE(reconnect.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest,
       ResyncSnapshotCarriesOneCoherentIdentityTuple) {
  server_->PublishEvent("snapshot_seed", {{"oracle", 23}});
  const auto response = HttpGet(server_->BoundPort(), "/api/v1/fhss/snapshot");
  ASSERT_EQ(response.status_code, 200) << response.body;
  const auto snapshot = nlohmann::json::parse(response.body);
  EXPECT_EQ(snapshot.at("schema"), "graphx.dashboard.fhss_snapshot.v1");
  EXPECT_EQ(snapshot.at("config_revision"),
            snapshot.at("configuration").at("config_revision"));
  EXPECT_EQ(snapshot.at("config_etag"),
            snapshot.at("configuration").at("etag"));
  EXPECT_EQ(snapshot.at("generation"),
            snapshot.at("runtime").at("active_generation"));
  EXPECT_EQ(snapshot.at("run_epoch"),
            snapshot.at("runtime").at("active_run_epoch"));
  EXPECT_GE(snapshot.at("latest_sequence").get<std::uint64_t>(), 1u);
}

TEST_F(FhssDashboardEventReplayTest,
       SlowClientBackpressureDoesNotBlockPublishers) {
  server_->SetEventQueueDepthForTesting(8);
  (void)PollEvents("slow-client");

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 400; ++i) {
    server_->PublishEvent("metrics", nlohmann::json{{"sample", i}});
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  const auto slow_batch = PollEvents("slow-client");
  EXPECT_TRUE(slow_batch.at("resync_required").get<bool>());
  EXPECT_TRUE(slow_batch.at("events").empty());
  EXPECT_LT(elapsed.count(), 250);
}

TEST_F(FhssDashboardEventReplayTest,
       NonReadingWebSocketDoesNotBlockHealthyWebSocketOrBoundedShutdown) {
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace websocket = beast::websocket;
  using tcp = asio::ip::tcp;

  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_retained_events = 256;
  options.max_retained_event_bytes = 16 * 1024 * 1024;
  options.max_websocket_queue_bytes = 16 * 1024 * 1024;
  options.max_websocket_replay_bytes = 16 * 1024 * 1024;
  options.max_publisher_ingress_bytes = 16 * 1024 * 1024;
  options.write_timeout = std::chrono::milliseconds(250);
  options.websocket_close_timeout = std::chrono::milliseconds(250);
  RestartWithOptions(options);

  const int stalled = RawWebSocketUpgrade(server_->BoundPort());
  ASSERT_GE(stalled, 0);
  int receive_buffer = 1024;
  ASSERT_EQ(::setsockopt(stalled, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
                        sizeof(receive_buffer)),
            0);
  const auto stalled_command = nlohmann::json{
      {"action", "subscribe"}, {"client_id", "non-reading-websocket"},
      {"last_sequence", 0}}.dump();
  SendRawFrame(stalled,
               std::span<const std::uint8_t>(
                   reinterpret_cast<const std::uint8_t *>(stalled_command.data()),
                   stalled_command.size()),
               true);

  asio::io_context context;
  tcp::resolver resolver(context);
  websocket::stream<tcp::socket> healthy(context);
  const auto authority = "127.0.0.1:" + std::to_string(server_->BoundPort());
  asio::connect(healthy.next_layer(),
                resolver.resolve("127.0.0.1",
                                 std::to_string(server_->BoundPort())));
  healthy.set_option(websocket::stream_base::decorator(
      [&](websocket::request_type &request) {
        request.set(boost::beast::http::field::origin, "http://" + authority);
      }));
  healthy.handshake(authority, "/api/v1/fhss/events/stream");
  beast::flat_buffer buffer;
  healthy.read(buffer);
  const auto hello = nlohmann::json::parse(
      beast::buffers_to_string(buffer.data()));
  healthy.write(asio::buffer(nlohmann::json{
      {"action", "subscribe"}, {"client_id", "draining-websocket"},
      {"publisher_epoch", hello.at("publisher_epoch")},
      {"last_sequence", hello.at("latest_sequence")}}.dump()));

  const auto publish_started = std::chrono::steady_clock::now();
  std::chrono::milliseconds slowest_publish{0};
  for (int index = 0; index < 64; ++index) {
    const auto item_started = std::chrono::steady_clock::now();
    server_->PublishEvent("large_stream_event",
                          {{"index", index},
                           {"payload", std::string(96 * 1024, 'x')}});
    slowest_publish = std::max(
        slowest_publish,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - item_started));
  }
  const auto publish_elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - publish_started);
  // Each producer call must remain well below the transport write timeout;
  // the looser aggregate bound includes 6 MiB of JSON construction under
  // sanitizer instrumentation without weakening the non-blocking assertion.
  EXPECT_LT(slowest_publish.count(), 100);
  EXPECT_LT(publish_elapsed.count(), 1500);

  bool observed = false;
  for (int index = 0; index < 64 && !observed; ++index) {
    buffer.clear();
    boost::system::error_code error;
    healthy.read(buffer, error);
    ASSERT_FALSE(error) << error.message();
    const auto event = nlohmann::json::parse(
        beast::buffers_to_string(buffer.data()));
    observed = event.value("event_type", std::string{}) ==
                   "large_stream_event" &&
               event.at("payload").value("index", -1) == 63;
  }
  EXPECT_TRUE(observed);

  const auto snapshot = nlohmann::json::parse(
      HttpGet(server_->BoundPort(), "/api/v1/fhss/snapshot").body);
  EXPECT_GE(snapshot.at("transport").at("active_websocket_clients")
                .get<std::size_t>(),
            2u);
  const auto stop_started = std::chrono::steady_clock::now();
  server_->Stop();
  const auto stop_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - stop_started);
  EXPECT_LT(stop_elapsed.count(), 3000);
  ::close(stalled);
}

TEST_F(FhssDashboardEventReplayTest,
       FailureInjectionDisconnectReconnectUnderLoadMaintainsReplay) {
  server_->SetEventQueueDepthForTesting(1024);

  server_->PublishEvent("status", nlohmann::json{{"seed", 0}});
  const auto first = PollEvents("load-client", 0);
  ASSERT_FALSE(first.at("events").empty());
  const auto seen =
      first.at("events").back().at("sequence").get<std::uint64_t>();

  std::thread producer([this]() {
    for (int i = 0; i < 300; ++i) {
      server_->PublishEvent("metrics", nlohmann::json{{"burst", i}});
    }
  });

  (void)PollEvents("load-client", std::nullopt, true);
  producer.join();

  const auto resumed = PollEvents("load-client", seen);
  EXPECT_FALSE(resumed.at("resync_required").get<bool>());
  EXPECT_FALSE(resumed.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest,
       FailureInjectionRetentionGapDuringReconnectForcesResync) {
  server_->SetEventRetentionForTesting(std::chrono::milliseconds(50));
  server_->PublishEvent("command", nlohmann::json{{"state", "pre-gap"}});
  const auto initial = PollEvents("retention-gap-client", 0);
  ASSERT_FALSE(initial.at("events").empty());

  (void)PollEvents("retention-gap-client", std::nullopt, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server_->PublishEvent("command", nlohmann::json{{"state", "post-gap"}});

  const auto reconnect = PollEvents("retention-gap-client", 0);
  EXPECT_TRUE(reconnect.at("resync_required").get<bool>());
  EXPECT_TRUE(reconnect.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, EncodedByteRetentionGapRequiresResync) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_frame_bytes = 125;
  options.max_websocket_message_bytes = 256;
  options.max_websocket_queue_bytes = 512;
  options.max_retained_event_bytes = 512;
  options.max_retained_events = 100;
  RestartWithOptions(std::move(options));
  server_->PublishEvent("large", {{"data", std::string(700, 'x')}});
  const auto batch = PollEvents("byte-gap", 0);
  EXPECT_TRUE(batch.at("resync_required").get<bool>());
  EXPECT_TRUE(batch.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, CountRetentionGapRequiresResync) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_retained_events = 2;
  RestartWithOptions(std::move(options));
  server_->PublishEvent("one", {{"oracle", 1}});
  server_->PublishEvent("two", {{"oracle", 2}});
  server_->PublishEvent("three", {{"oracle", 3}});
  const auto batch = PollEvents("count-gap", 0);
  EXPECT_TRUE(batch.at("resync_required").get<bool>());
  EXPECT_TRUE(batch.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, ReplayBatchCountLimitForcesResync) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_replay_events = 2;
  RestartWithOptions(options);
  server_->PublishEvent("one", {{"oracle", 1}});
  server_->PublishEvent("two", {{"oracle", 2}});
  server_->PublishEvent("three", {{"oracle", 3}});
  const auto batch = PollEvents("replay-cap", 0);
  EXPECT_TRUE(batch.at("resync_required").get<bool>());
  EXPECT_TRUE(batch.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, ReplayBatchByteLimitForcesStableResync) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_frame_bytes = 125;
  options.max_websocket_message_bytes = 256;
  options.max_websocket_queue_bytes = 1024;
  options.max_websocket_replay_bytes = 512;
  options.max_retained_event_bytes = 4096;
  RestartWithOptions(options);
  server_->PublishEvent("one", {{"payload", std::string(180, 'a')}});
  server_->PublishEvent("two", {{"payload", std::string(180, 'b')}});
  const auto batch = PollEvents("replay-byte-cap", 0);
  EXPECT_TRUE(batch.at("resync_required").get<bool>());
  EXPECT_TRUE(batch.at("truncated").get<bool>());
  EXPECT_EQ(batch.at("reason"), "replay_limit");
  EXPECT_TRUE(batch.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest,
       QueuedEventRetainsIngressConfigurationAndRuntimeProvenance) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_events_per_second = 1;
  RestartWithOptions(options);
  const auto captured_revision = configuration_service_->ConfigRevision();
  const auto captured_etag = configuration_service_->ETag();
  server_->PublishEvent("warmup", {{"step", 0}});
  server_->PublishEvent("queued_provenance", {{"step", 1}});
  const auto update = configuration_service_->PatchConfig(
      {{"schema", "graphx.dashboard.config_update.v1"},
       {"command_id", "queued-provenance-update"},
       {"expected_revision", captured_revision},
       {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
       {"value", 1240000001.0},
       {"apply", "staged"}});
  ASSERT_EQ(update.value("status", std::string{}), "staged") << update.dump();
  ASSERT_GT(configuration_service_->ConfigRevision(), captured_revision);
  std::this_thread::sleep_for(std::chrono::milliseconds(1150));
  const auto batch = PollEvents("queued-provenance-client", 0);
  const auto event =
      std::ranges::find_if(batch.at("events"), [](const auto &item) {
        return item.value("event_type", std::string{}) == "queued_provenance";
      });
  ASSERT_NE(event, batch.at("events").end());
  EXPECT_EQ(event->at("config_revision"), captured_revision);
  EXPECT_EQ(event->at("config_etag"), captured_etag);
}

TEST_F(FhssDashboardEventReplayTest,
       IngressOverflowCoalescesDiagnosticsButPreservesTerminalTransition) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_websocket_events_per_second = 1;
  options.max_publisher_ingress_events = 1;
  RestartWithOptions(options);
  server_->PublishEvent("warmup", {{"step", 0}});
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  server_->PublishEvent("job_progress", {{"state", "running"}});
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  server_->PublishEvent("diagnostics", {{"sample", 1}});
  server_->PublishEvent("job_terminal", {{"state", "completed"}},
                        {{"semantic_class", "terminal"}});
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  const auto batch = PollEvents("terminal-priority-client", 0);
  EXPECT_NE(std::ranges::find_if(batch.at("events"),
                                 [](const auto &item) {
                                   return item.value("event_type",
                                                     std::string{}) ==
                                          "job_terminal";
                                 }),
            batch.at("events").end());
  const auto snapshot = nlohmann::json::parse(
      HttpGet(server_->BoundPort(), "/api/v1/fhss/snapshot").body);
  EXPECT_GT(snapshot.at("transport").at("queue_overflows").get<std::uint64_t>(),
            0u);
}

TEST_F(FhssDashboardEventReplayTest,
       PublisherIngressReplacementRespectsByteCapAndCriticalFifo) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_publisher_ingress_events = 3;
  options.max_publisher_ingress_bytes = 256 * 1024;
  RestartWithOptions(options);
  server_->SetPublisherPausedForTesting(true);
  server_->PublishEvent("critical_a",
                        {{"payload", std::string(170 * 1024, 'a')}});
  server_->PublishEvent("diagnostics", {{"sample", 1}});
  server_->PublishEvent("diagnostics",
                        {{"payload", std::string(120 * 1024, 'b')}});
  server_->PublishEvent("critical_b", {{"state", "completed"}},
                        {{"semantic_class", "terminal"}});
  server_->SetPublisherPausedForTesting(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto batch = PollEvents("byte-cap-order-client", 0);
  std::vector<std::string> event_types;
  for (const auto &event : batch.at("events"))
    event_types.push_back(event.at("event_type").get<std::string>());
  const auto a = std::ranges::find(event_types, "critical_a");
  const auto diagnostics = std::ranges::find(event_types, "diagnostics");
  const auto b = std::ranges::find(event_types, "critical_b");
  ASSERT_NE(a, event_types.end());
  ASSERT_NE(diagnostics, event_types.end());
  ASSERT_NE(b, event_types.end());
  EXPECT_LT(a, diagnostics);
  EXPECT_LT(diagnostics, b);
  const auto retained_diagnostics =
      std::ranges::find_if(batch.at("events"), [](const auto &event) {
        return event.value("event_type", std::string{}) == "diagnostics";
      });
  ASSERT_NE(retained_diagnostics, batch.at("events").end());
  EXPECT_EQ(retained_diagnostics->at("payload").value("sample", 0), 1);
  EXPECT_FALSE(retained_diagnostics->at("payload").contains("payload"));
}

TEST_F(FhssDashboardEventReplayTest,
       PublisherIngressRejectsNewestCriticalWithoutBlockingOrReordering) {
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.max_publisher_ingress_events = 1;
  options.max_publisher_ingress_bytes = 256 * 1024;
  RestartWithOptions(options);
  server_->SetPublisherPausedForTesting(true);
  server_->PublishEvent("critical_a", {{"order", 1}});
  const auto started = std::chrono::steady_clock::now();
  for (int index = 0; index < 500; ++index)
    server_->PublishEvent("critical_rejected", {{"order", index + 2}});
  const auto elapsed = std::chrono::steady_clock::now() - started;
  EXPECT_LT(elapsed, std::chrono::milliseconds(250));

  const auto immediate = PollEvents("critical-order-immediate", 0);
  EXPECT_EQ(std::ranges::find_if(immediate.at("events"),
                                 [](const auto &event) {
                                   return event.value("event_type",
                                                      std::string{}) ==
                                          "critical_rejected";
                                 }),
            immediate.at("events").end());
  server_->SetPublisherPausedForTesting(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto completed = PollEvents("critical-order-completed", 0);
  const auto accepted =
      std::ranges::find_if(completed.at("events"), [](const auto &event) {
        return event.value("event_type", std::string{}) == "critical_a";
      });
  ASSERT_NE(accepted, completed.at("events").end());
  EXPECT_EQ(std::ranges::find_if(completed.at("events"),
                                 [](const auto &event) {
                                   return event.value("event_type",
                                                      std::string{}) ==
                                          "critical_rejected";
                                 }),
            completed.at("events").end());

  server_->PublishEvent("oversized_critical",
                        {{"payload", std::string(300 * 1024, 'x')}});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto oversized = PollEvents("oversized-critical-client", 0);
  EXPECT_EQ(std::ranges::find_if(oversized.at("events"),
                                 [](const auto &event) {
                                   return event.value("event_type",
                                                      std::string{}) ==
                                          "oversized_critical";
                                 }),
            oversized.at("events").end());
}

} // namespace
