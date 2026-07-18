// SPDX-License-Identifier: MIT

#pragma once

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

class CpuAcceleratorProvider final : public IAcceleratorProvider {
public:
    CpuAcceleratorProvider();

    [[nodiscard]] AcceleratorProviderInfo Info() const override;

    std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
    CreateSession(const AcceleratorSessionCreateRequest& request) override;

private:
    AcceleratorProviderInfo info_;
};

}  // namespace accelgraph