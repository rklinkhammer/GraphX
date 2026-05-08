// MIT License
//
// Copyright (c) 2025 graphlib contributors
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

#include "config/JsonView.hpp"
#include "config/ConfigError.hpp"
#include <format>
#include <limits>

namespace graph {

bool JsonView::Contains(const std::string& key) const {
    return json_.contains(key) && !json_[key].is_null();
}

std::string JsonView::GetString(const std::string& key,
                                const std::string& default_val) const {
    if (!Contains(key)) {
        return default_val;
    }
    
    const auto& value = json_[key];
    if (!value.is_string()) {
        throw ConfigError(FormatError(key, "string", value.type_name()));
    }
    
    return value.get<std::string>();
}

float JsonView::GetFloat(const std::string& key,
                         float default_val) const {
    if (!Contains(key)) {
        if (std::isnan(default_val)) {
            throw ConfigError(std::format("Missing required field: {}", key));
        }
        return default_val;
    }
    
    const auto& value = json_[key];
    if (!value.is_number()) {
        throw ConfigError(FormatError(key, "number", value.type_name()));
    }
    
    return value.get<float>();
}

int JsonView::GetInt(const std::string& key,
                     int default_val) const {
    if (!Contains(key)) {
        if (default_val == -1) {
            throw ConfigError(std::format("Missing required field: {}", key));
        }
        return default_val;
    }
    
    const auto& value = json_[key];
    if (!value.is_number_integer()) {
        throw ConfigError(FormatError(key, "integer", value.type_name()));
    }
    
    return value.get<int>();
}

bool JsonView::GetBool(const std::string& key,
                       bool default_val) const {
    if (!Contains(key)) {
        return default_val;
    }
    
    const auto& value = json_[key];
    if (!value.is_boolean()) {
        throw ConfigError(FormatError(key, "boolean", value.type_name()));
    }
    
    return value.get<bool>();
}

JsonView JsonView::GetObject(const std::string& key) const {
    if (!Contains(key)) {
        throw ConfigError(std::format("Missing required object: {}", key));
    }
    
    const auto& value = json_[key];
    if (!value.is_object()) {
        throw ConfigError(FormatError(key, "object", value.type_name()));
    }
    
    return JsonView(value);
}

std::vector<std::string> JsonView::GetStringArray(const std::string& key) const {
    if (!Contains(key)) {
        throw ConfigError(std::format("Missing required array: {}", key));
    }
    
    const auto& value = json_[key];
    if (!value.is_array()) {
        throw ConfigError(FormatError(key, "array", value.type_name()));
    }
    
    std::vector<std::string> result;
    for (const auto& item : value) {
        if (!item.is_string()) {
            throw ConfigError(
                std::format("Array '{}' contains non-string element", key)
            );
        }
        result.push_back(item.get<std::string>());
    }
    
    return result;
}

std::vector<JsonView> JsonView::GetArray(const std::string& key) const {
    if (!Contains(key)) {
        throw ConfigError(std::format("Missing required array: {}", key));
    }
    
    const auto& value = json_[key];
    if (!value.is_array()) {
        throw ConfigError(FormatError(key, "array", value.type_name()));
    }
    
    std::vector<JsonView> result;
    for (const auto& item : value) {
        result.emplace_back(item);
    }
    
    return result;
}

std::string JsonView::FormatError(const std::string& key,
                                  const std::string& expected,
                                  const std::string& actual) {
    return std::format(
        "Field '{}' has wrong type: expected {}, got {}",
        key, expected, actual
    );
}

}  // namespace graph
