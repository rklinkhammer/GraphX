// SPDX-License-Identifier: MIT

#include "accelgraph/AcceleratorRegistry.hpp"

#include <algorithm>

namespace accelgraph {

bool AcceleratorRegistry::RegisterProvider(std::shared_ptr<IAcceleratorProvider> provider) {
    if (!provider) {
        return false;
    }

    const AcceleratorProviderId provider_id = provider->Info().provider_id;
    if (provider_id.empty()) {
        return false;
    }

    const auto duplicate_it = std::find_if(
        providers_.begin(), providers_.end(), [&](const std::shared_ptr<IAcceleratorProvider>& item) {
            return item && item->Info().provider_id == provider_id;
        });
    if (duplicate_it != providers_.end()) {
        return false;
    }

    providers_.push_back(std::move(provider));
    return true;
}

std::vector<AcceleratorProviderInfo> AcceleratorRegistry::ListProviders() const {
    std::vector<AcceleratorProviderInfo> providers;
    providers.reserve(providers_.size());
    for (const auto& provider : providers_) {
        if (provider) {
            providers.push_back(provider->Info());
        }
    }
    return providers;
}

std::shared_ptr<IAcceleratorProvider>
AcceleratorRegistry::FindProviderById(const AcceleratorProviderId& provider_id) const {
    const auto it = std::find_if(
        providers_.begin(), providers_.end(), [&](const std::shared_ptr<IAcceleratorProvider>& provider) {
            return provider && provider->Info().provider_id == provider_id;
        });
    return it != providers_.end() ? *it : nullptr;
}

std::shared_ptr<IAcceleratorProvider>
AcceleratorRegistry::FindProviderByBackend(AcceleratorBackend backend) const {
    const auto it = std::find_if(
        providers_.begin(), providers_.end(), [&](const std::shared_ptr<IAcceleratorProvider>& provider) {
            return provider && provider->Info().backend == backend;
        });
    return it != providers_.end() ? *it : nullptr;
}

}  // namespace accelgraph