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
#include "JsonView.hpp"

namespace graph {

/**
 * Base template for parsing JSON into strongly-typed config structs.
 * 
 * Specialize this template for each config type you want to support:
 * 
 * @code
 * template<>
 * struct ConfigParser<MyConfig> {
 *     static MyConfig Parse(const JsonView& view) {
 *         MyConfig cfg{...};
 *         cfg.Validate();
 *         return cfg;
 *     }
 * };
 * @endcode
 * 
 * Each specialization should:
 * 1. Extract values from the JsonView
 * 2. Call Validate() on the resulting config
 * 3. Throw ConfigError if validation fails
 * 4. Return the validated config
 * 
 * @tparam ConfigType The config struct type to parse into
 */
template<typename ConfigType>
struct ConfigParser {
    /**
     * Parse JSON into a config struct.
     * 
     * @param view The JSON to parse
     * @return The parsed and validated config
     * @throws ConfigError if parsing or validation fails
     */
    static ConfigType Parse(const JsonView& view);
    
    // Delete default instantiation - specializations required
    ConfigParser() = delete;
};

}  // namespace graph
