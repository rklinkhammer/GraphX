// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

class AcceleratorRegistry {
public:
    bool RegisterProvider(std::shared_ptr<IAcceleratorProvider> provider);

    [[nodiscard]] std::vector<AcceleratorProviderInfo> ListProviders() const;

    [[nodiscard]] std::shared_ptr<IAcceleratorProvider>
    FindProviderById(const AcceleratorProviderId& provider_id) const;

    [[nodiscard]] std::shared_ptr<IAcceleratorProvider>
    FindProviderByBackend(AcceleratorBackend backend) const;

private:
    std::vector<std::shared_ptr<IAcceleratorProvider>> providers_;
};

}  // namespace accelgraph