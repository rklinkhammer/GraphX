#include "sar/SarAccelTokenImagePayloadStore.hpp"

#include <mutex>
#include <unordered_map>

namespace sar::detail {

namespace {

std::mutex g_payload_mutex;
std::unordered_map<std::uint64_t, AccelTokenImagePayload> g_payloads;

} // namespace

void StoreAccelTokenImagePayload(std::uint64_t token, AccelTokenImagePayload payload) {
    std::lock_guard<std::mutex> lock(g_payload_mutex);
    g_payloads[token] = std::move(payload);
}

std::optional<AccelTokenImagePayload> ConsumeAccelTokenImagePayload(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(g_payload_mutex);
    const auto it = g_payloads.find(token);
    if (it == g_payloads.end()) {
        return std::nullopt;
    }

    auto payload = std::move(it->second);
    g_payloads.erase(it);
    return payload;
}

} // namespace sar::detail
