#include "sar/SarAccelTokenSidecarStore.hpp"

#include <mutex>
#include <unordered_map>

namespace sar::detail {

namespace {

std::mutex g_sidecar_mutex;
std::unordered_map<std::uint64_t, AccelTokenSidecar> g_sidecars;

} // namespace

void StoreAccelTokenSidecar(std::uint64_t token, const AccelTokenSidecar& sidecar) {
    std::scoped_lock lock(g_sidecar_mutex);
    g_sidecars[token] = sidecar;
}

std::optional<AccelTokenSidecar> FindAccelTokenSidecar(std::uint64_t token) {
    std::scoped_lock lock(g_sidecar_mutex);
    const auto it = g_sidecars.find(token);
    if (it == g_sidecars.end()) {
        return std::nullopt;
    }
    return it->second;
}

void UpdateAccelTokenSidecarH2D(std::uint64_t token,
                                std::uint32_t backend_id,
                                SarBackendKind backend,
                                std::uint64_t queue_id) {
    std::scoped_lock lock(g_sidecar_mutex);
    auto& sidecar = g_sidecars[token];
    sidecar.backend_id = backend_id;
    sidecar.backend = backend;
    sidecar.h2d_queue_id = queue_id;
}

void UpdateAccelTokenSidecarKernel(std::uint64_t token,
                                   std::uint32_t backend_id,
                                   SarBackendKind backend,
                                   std::uint64_t queue_id) {
    std::scoped_lock lock(g_sidecar_mutex);
    auto& sidecar = g_sidecars[token];
    sidecar.backend_id = backend_id;
    sidecar.backend = backend;
    sidecar.kernel_queue_id = queue_id;
}

void UpdateAccelTokenSidecarD2H(std::uint64_t token,
                                std::uint32_t backend_id,
                                SarBackendKind backend,
                                std::uint64_t queue_id) {
    std::scoped_lock lock(g_sidecar_mutex);
    auto& sidecar = g_sidecars[token];
    sidecar.backend_id = backend_id;
    sidecar.backend = backend;
    sidecar.d2h_queue_id = queue_id;
}

} // namespace sar::detail
