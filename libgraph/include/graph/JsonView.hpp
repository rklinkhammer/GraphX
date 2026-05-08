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

#pragma once

#include "config/ConfigError.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace graph {

/**
 * Safe wrapper around nlohmann::json providing:
 * - Type-safe field access
 * - Default values
 * - Clear error messages
 * 
 * Usage:
 * @code
 * JsonView view(json_object);
 * float q = view.GetFloat("q", 0.01f);
 * std::string name = view.GetString("name", "default");
 * @endcode
 * 
 * If a field is missing and no default provided, throws ConfigError.
 * If a field has wrong type, throws ConfigError with context.
 */
class JsonView {
public:
    /**
     * Wrap a JSON object for safe access.
     * 
     * @param json The JSON object to wrap (can be null)
     */
    explicit JsonView(const nlohmann::json& json)
        : json_(json) {}
    
    /// Access the underlying JSON object
    const nlohmann::json& Raw() const { return json_; }
    
    /**
     * Check if a field exists.
     * 
     * @param key Field name
     * @return True if field exists and is not null
     */
    bool Contains(const std::string& key) const;
    
    /**
     * Get a string field.
     * 
     * @param key Field name
     * @param default_val Value to use if field missing
     * @return The string value
     * @throws ConfigError if field wrong type
     */
    std::string GetString(const std::string& key,
                         const std::string& default_val = "") const;
    
    /**
     * Get a floating-point field.
     * 
     * @param key Field name
     * @param default_val Value to use if field missing
     * @return The float value
     * @throws ConfigError if field missing (no default) or wrong type
     */
    float GetFloat(const std::string& key,
                   float default_val = std::numeric_limits<float>::quiet_NaN()) const;
    
    /**
     * Get an integer field.
     * 
     * @param key Field name
     * @param default_val Value to use if field missing
     * @return The int value
     * @throws ConfigError if field missing (no default) or wrong type
     */
    int GetInt(const std::string& key,
               int default_val = -1) const;
    
    /**
     * Get a boolean field.
     * 
     * @param key Field name
     * @param default_val Value to use if field missing
     * @return The bool value
     * @throws ConfigError if field wrong type
     */
    bool GetBool(const std::string& key,
                 bool default_val = false) const;
    
    /**
     * Get a nested object as a JsonView.
     * 
     * @param key Field name
     * @return JsonView wrapping the nested object
     * @throws ConfigError if field missing or not an object
     */
    JsonView GetObject(const std::string& key) const;
    
    /**
     * Get a string array.
     * 
     * @param key Field name
     * @return Vector of strings
     * @throws ConfigError if field missing or not an array
     */
    std::vector<std::string> GetStringArray(const std::string& key) const;
    
    /**
     * Get array elements as JsonView objects.
     * 
     * @param key Field name
     * @return Vector of JsonView objects
     * @throws ConfigError if field missing or not an array
     */
    std::vector<JsonView> GetArray(const std::string& key) const;

private:
    const nlohmann::json& json_;
    
    // Helper to format error messages
    static std::string FormatError(const std::string& key,
                                   const std::string& expected,
                                   const std::string& actual);
};

}  // namespace config
