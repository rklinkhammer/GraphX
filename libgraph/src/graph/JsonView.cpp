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

std::string JsonView::FormatError(const std::string& key,
                                  const std::string& expected,
                                  const std::string& actual) {
    return std::format(
        "Field '{}' has wrong type: expected {}, got {}",
        key, expected, actual
    );
}

// ============================================================================
// C++26 ENHANCED METHODS - std::expected-based implementations
// ============================================================================

std::expected<std::string, ConfigError> JsonView::TryGetString(
    const std::string& key,
    const std::string& default_val) const {
    if (!Contains(key)) {
        return default_val;
    }

    const auto& value = json_[key];
    if (!value.is_string()) {
        return std::unexpected(ConfigError(FormatError(key, "string", value.type_name())));
    }

    return value.get<std::string>();
}

std::expected<float, ConfigError> JsonView::TryGetFloat(
    const std::string& key,
    float default_val) const {
    if (!Contains(key)) {
        if (std::isnan(default_val)) {
            return std::unexpected(ConfigError(std::format("Missing required field: {}", key)));
        }
        return default_val;
    }

    const auto& value = json_[key];
    if (!value.is_number()) {
        return std::unexpected(ConfigError(FormatError(key, "number", value.type_name())));
    }

    return value.get<float>();
}

std::expected<int, ConfigError> JsonView::TryGetInt(
    const std::string& key,
    int default_val) const {
    if (!Contains(key)) {
        if (default_val == -1) {
            return std::unexpected(ConfigError(std::format("Missing required field: {}", key)));
        }
        return default_val;
    }

    const auto& value = json_[key];
    if (!value.is_number_integer()) {
        return std::unexpected(ConfigError(FormatError(key, "integer", value.type_name())));
    }

    return value.get<int>();
}

std::expected<bool, ConfigError> JsonView::TryGetBool(
    const std::string& key,
    bool default_val) const {
    if (!Contains(key)) {
        return default_val;
    }

    const auto& value = json_[key];
    if (!value.is_boolean()) {
        return std::unexpected(ConfigError(FormatError(key, "boolean", value.type_name())));
    }

    return value.get<bool>();
}

std::expected<JsonView, ConfigError> JsonView::TryGetObject(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::unexpected(ConfigError(std::format("Missing required object: {}", key)));
    }

    const auto& value = json_[key];
    if (!value.is_object()) {
        return std::unexpected(ConfigError(FormatError(key, "object", value.type_name())));
    }

    return JsonView(value);
}

std::expected<std::vector<std::string>, ConfigError> JsonView::TryGetStringArray(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::unexpected(ConfigError(std::format("Missing required array: {}", key)));
    }

    const auto& value = json_[key];
    if (!value.is_array()) {
        return std::unexpected(ConfigError(FormatError(key, "array", value.type_name())));
    }

    std::vector<std::string> result;
    for (const auto& item : value) {
        if (!item.is_string()) {
            return std::unexpected(ConfigError(
                std::format("Array '{}' contains non-string element", key)
            ));
        }
        result.push_back(item.get<std::string>());
    }

    return result;
}

std::expected<std::vector<JsonView>, ConfigError> JsonView::TryGetArray(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::unexpected(ConfigError(std::format("Missing required array: {}", key)));
    }

    const auto& value = json_[key];
    if (!value.is_array()) {
        return std::unexpected(ConfigError(FormatError(key, "array", value.type_name())));
    }

    std::vector<JsonView> result;
    for (const auto& item : value) {
        result.emplace_back(item);
    }

    return result;
}

// ============================================================================
// C++26 ENHANCED METHODS - std::optional-based implementations
// ============================================================================

std::optional<std::string> JsonView::GetOptionalString(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::nullopt;
    }

    auto result = TryGetString(key);
    if (!result) {
        throw result.error();
    }

    return result.value();
}

std::optional<float> JsonView::GetOptionalFloat(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::nullopt;
    }

    auto result = TryGetFloat(key);
    if (!result) {
        throw result.error();
    }

    return result.value();
}

std::optional<int> JsonView::GetOptionalInt(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::nullopt;
    }

    auto result = TryGetInt(key);
    if (!result) {
        throw result.error();
    }

    return result.value();
}

std::optional<bool> JsonView::GetOptionalBool(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::nullopt;
    }

    auto result = TryGetBool(key);
    if (!result) {
        throw result.error();
    }

    return result.value();
}

std::optional<JsonView> JsonView::GetOptionalObject(
    const std::string& key) const {
    if (!Contains(key)) {
        return std::nullopt;
    }

    auto result = TryGetObject(key);
    if (!result) {
        throw result.error();
    }

    return result.value();
}

}  // namespace graph
