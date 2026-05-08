// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <memory>
#include <typeindex>
#include <map>

namespace graph {

class CapabilityBus {
public:
    virtual ~CapabilityBus() = default;

    template<typename CapabilityT>
    void Register(std::shared_ptr<CapabilityT> capability) {
        capabilities_[std::type_index(typeid(CapabilityT))] = capability;
    }

    template<typename CapabilityT>
    std::shared_ptr<CapabilityT> Get() const {
        auto it = capabilities_.find(std::type_index(typeid(CapabilityT)));
        if (it != capabilities_.end()) {
            return std::static_pointer_cast<CapabilityT>(it->second);
        }
        return nullptr;
    }

    template<typename CapabilityT>
    bool Has() const {
        return capabilities_.find(std::type_index(typeid(CapabilityT))) != capabilities_.end();
    }

    void Clear() {
        capabilities_.clear();
    }

private:
    std::map<std::type_index, std::shared_ptr<void>> capabilities_;
};

}