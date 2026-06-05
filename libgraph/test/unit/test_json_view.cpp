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

#include <gtest/gtest.h>
#include "config/JsonView.hpp"
#include "config/ConfigError.hpp"
#include <nlohmann/json.hpp>
#include <limits>
#include <cmath>

using namespace graph;
using json = nlohmann::json;

// Test adapter: preserve legacy Get* behavior while internally exercising
// expected-based TryGet* APIs.
class JsonViewAdapter : public graph::JsonView {
public:
    using graph::JsonView::JsonView;

    std::string GetString(const std::string& key,
                          const std::string& default_val = "") const {
        auto result = TryGetString(key, default_val);
        if (!result) {
            throw result.error();
        }
        return result.value();
    }

    float GetFloat(const std::string& key,
                   float default_val = std::numeric_limits<float>::quiet_NaN()) const {
        auto result = TryGetFloat(key, default_val);
        if (!result) {
            throw result.error();
        }
        return result.value();
    }

    int GetInt(const std::string& key,
               int default_val = -1) const {
        auto result = TryGetInt(key, default_val);
        if (!result) {
            throw result.error();
        }
        return result.value();
    }

    bool GetBool(const std::string& key,
                 bool default_val = false) const {
        auto result = TryGetBool(key, default_val);
        if (!result) {
            throw result.error();
        }
        return result.value();
    }

    JsonViewAdapter GetObject(const std::string& key) const {
        auto result = TryGetObject(key);
        if (!result) {
            throw result.error();
        }
        return JsonViewAdapter(result.value().Raw());
    }

    std::vector<std::string> GetStringArray(const std::string& key) const {
        auto result = TryGetStringArray(key);
        if (!result) {
            throw result.error();
        }
        return result.value();
    }

    std::vector<JsonViewAdapter> GetArray(const std::string& key) const {
        auto result = TryGetArray(key);
        if (!result) {
            throw result.error();
        }
        std::vector<JsonViewAdapter> converted;
        converted.reserve(result.value().size());
        for (const auto& item : result.value()) {
            converted.emplace_back(item.Raw());
        }
        return converted;
    }
};

#define JsonView JsonViewAdapter

// ============================================================================
// Test Fixture for JsonView Basic Operations
// ============================================================================

class JsonViewBasicTest : public ::testing::Test {
protected:
    json empty_object = json::object();
    json simple_object = json::parse(R"({
        "name": "test",
        "count": 42,
        "ratio": 3.14,
        "enabled": true
    })");
    
    json null_json = json(nullptr);
};

// ============================================================================
// Constructor and Raw() Tests (2 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, Constructor_EmptyObject) {
    JsonView view(empty_object);
    EXPECT_TRUE(view.Raw().is_object());
    EXPECT_TRUE(view.Raw().empty());
}

TEST_F(JsonViewBasicTest, Constructor_PopulatedObject) {
    JsonView view(simple_object);
    EXPECT_TRUE(view.Raw().is_object());
    EXPECT_FALSE(view.Raw().empty());
    EXPECT_EQ(view.Raw().size(), 4);
}

// ============================================================================
// Contains() Tests (5 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, Contains_ExistingField) {
    JsonView view(simple_object);
    EXPECT_TRUE(view.Contains("name"));
    EXPECT_TRUE(view.Contains("count"));
}

TEST_F(JsonViewBasicTest, Contains_MissingField) {
    JsonView view(simple_object);
    EXPECT_FALSE(view.Contains("nonexistent"));
}

TEST_F(JsonViewBasicTest, Contains_NullField) {
    json obj_with_null = json::parse(R"({"field": null})");
    JsonView view(obj_with_null);
    EXPECT_FALSE(view.Contains("field"));
}

TEST_F(JsonViewBasicTest, Contains_EmptyStringKey) {
    json obj = json::parse(R"({"": "empty_key", "name": "value"})");
    JsonView view(obj);
    EXPECT_TRUE(view.Contains(""));
}

TEST_F(JsonViewBasicTest, Contains_FalsyButNotNull) {
    json obj = json::parse(R"({"zero": 0, "false": false, "empty": ""})");
    JsonView view(obj);
    // Falsy values should still return true if they exist and are not null
    EXPECT_TRUE(view.Contains("zero"));
    EXPECT_TRUE(view.Contains("false"));
    EXPECT_TRUE(view.Contains("empty"));
}

// ============================================================================
// GetString() Tests (10 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetString_ValidValue) {
    JsonView view(simple_object);
    EXPECT_EQ(view.GetString("name"), "test");
}

TEST_F(JsonViewBasicTest, GetString_MissingFieldWithDefault) {
    JsonView view(simple_object);
    EXPECT_EQ(view.GetString("missing", "default_value"), "default_value");
}

TEST_F(JsonViewBasicTest, GetString_MissingFieldNoDefault) {
    JsonView view(simple_object);
    // Default is empty string
    EXPECT_EQ(view.GetString("missing"), "");
}

TEST_F(JsonViewBasicTest, GetString_NullValue) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_EQ(view.GetString("field", "default"), "default");
}

TEST_F(JsonViewBasicTest, GetString_EmptyString) {
    json obj = json::parse(R"({"field": ""})");
    JsonView view(obj);
    EXPECT_EQ(view.GetString("field"), "");
}

TEST_F(JsonViewBasicTest, GetString_UnicodeString) {
    json obj = json::parse(R"({"field": "Hello 世界 🌍"})");
    JsonView view(obj);
    EXPECT_EQ(view.GetString("field"), "Hello 世界 🌍");
}

TEST_F(JsonViewBasicTest, GetString_TypeMismatch_Number) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetString("count"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetString_TypeMismatch_Boolean) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetString("enabled"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetString_TypeMismatch_Array) {
    json obj = json::parse(R"({"field": [1, 2, 3]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetString("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetString_TypeMismatch_Object) {
    json obj = json::parse(R"({"field": {"nested": "value"}})");
    JsonView view(obj);
    EXPECT_THROW(view.GetString("field"), ConfigError);
}

// ============================================================================
// GetFloat() Tests (12 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetFloat_ValidFloat) {
    JsonView view(simple_object);
    EXPECT_FLOAT_EQ(view.GetFloat("ratio"), 3.14f);
}

TEST_F(JsonViewBasicTest, GetFloat_IntegerValue) {
    JsonView view(simple_object);
    // Integer should implicitly convert to float
    EXPECT_FLOAT_EQ(view.GetFloat("count"), 42.0f);
}

TEST_F(JsonViewBasicTest, GetFloat_MissingFieldWithDefault) {
    JsonView view(simple_object);
    EXPECT_FLOAT_EQ(view.GetFloat("missing", 99.5f), 99.5f);
}

TEST_F(JsonViewBasicTest, GetFloat_MissingFieldNoDefault_ThrowsError) {
    JsonView view(simple_object);
    // Missing field with default NaN should throw
    EXPECT_THROW(view.GetFloat("missing"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetFloat_NullValue) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_THROW(view.GetFloat("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetFloat_Zero) {
    json obj = json::parse(R"({"field": 0.0})");
    JsonView view(obj);
    EXPECT_FLOAT_EQ(view.GetFloat("field"), 0.0f);
}

TEST_F(JsonViewBasicTest, GetFloat_Negative) {
    json obj = json::parse(R"({"field": -42.5})");
    JsonView view(obj);
    EXPECT_FLOAT_EQ(view.GetFloat("field"), -42.5f);
}

TEST_F(JsonViewBasicTest, GetFloat_VeryLarge) {
    json obj = json::parse(R"({"field": 1e10})");
    JsonView view(obj);
    EXPECT_FLOAT_EQ(view.GetFloat("field"), 1e10f);
}

TEST_F(JsonViewBasicTest, GetFloat_VerySmall) {
    json obj = json::parse(R"({"field": 1e-10})");
    JsonView view(obj);
    EXPECT_FLOAT_EQ(view.GetFloat("field"), 1e-10f);
}

TEST_F(JsonViewBasicTest, GetFloat_TypeMismatch_String) {
    json obj = json::parse(R"({"field": "3.14"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetFloat("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetFloat_TypeMismatch_Boolean) {
    json obj = json::parse(R"({"field": true})");
    JsonView view(obj);
    EXPECT_THROW(view.GetFloat("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetFloat_TypeMismatch_Array) {
    json obj = json::parse(R"({"field": [1, 2, 3]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetFloat("field"), ConfigError);
}

// ============================================================================
// GetInt() Tests (12 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetInt_ValidInteger) {
    JsonView view(simple_object);
    EXPECT_EQ(view.GetInt("count"), 42);
}

TEST_F(JsonViewBasicTest, GetInt_MissingFieldWithDefault) {
    JsonView view(simple_object);
    EXPECT_EQ(view.GetInt("missing", 99), 99);
}

TEST_F(JsonViewBasicTest, GetInt_MissingFieldNoDefault_ThrowsError) {
    JsonView view(simple_object);
    // Missing field with default -1 should throw
    EXPECT_THROW(view.GetInt("missing"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_NullValue) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_Zero) {
    json obj = json::parse(R"({"field": 0})");
    JsonView view(obj);
    EXPECT_EQ(view.GetInt("field"), 0);
}

TEST_F(JsonViewBasicTest, GetInt_Negative) {
    json obj = json::parse(R"({"field": -42})");
    JsonView view(obj);
    EXPECT_EQ(view.GetInt("field"), -42);
}

TEST_F(JsonViewBasicTest, GetInt_FloatRejected) {
    json obj = json::parse(R"({"field": 3.14})");
    JsonView view(obj);
    // Floats should be rejected for integer fields
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_TypeMismatch_String) {
    json obj = json::parse(R"({"field": "42"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_TypeMismatch_Boolean) {
    json obj = json::parse(R"({"field": true})");
    JsonView view(obj);
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_TypeMismatch_Array) {
    json obj = json::parse(R"({"field": [1, 2, 3]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_TypeMismatch_Object) {
    json obj = json::parse(R"({"field": {"value": 42}})");
    JsonView view(obj);
    EXPECT_THROW(view.GetInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetInt_DefaultOfNegativeOne) {
    json obj = json::parse(R"({"exists": 100})");
    JsonView view(obj);
    // -1 is the default for "required" field
    // Missing field should still throw even with -1 as default
    EXPECT_THROW(view.GetInt("missing"), ConfigError);
}

// ============================================================================
// GetBool() Tests (8 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetBool_TrueValue) {
    JsonView view(simple_object);
    EXPECT_TRUE(view.GetBool("enabled"));
}

TEST_F(JsonViewBasicTest, GetBool_FalseValue) {
    json obj = json::parse(R"({"flag": false})");
    JsonView view(obj);
    EXPECT_FALSE(view.GetBool("flag"));
}

TEST_F(JsonViewBasicTest, GetBool_MissingFieldWithDefault) {
    JsonView view(simple_object);
    EXPECT_TRUE(view.GetBool("missing", true));
    EXPECT_FALSE(view.GetBool("missing", false));
}

TEST_F(JsonViewBasicTest, GetBool_MissingFieldNoDefault) {
    JsonView view(simple_object);
    // Default is false
    EXPECT_FALSE(view.GetBool("missing"));
}

TEST_F(JsonViewBasicTest, GetBool_NullValue) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_FALSE(view.GetBool("field", false));
}

TEST_F(JsonViewBasicTest, GetBool_TypeMismatch_Number) {
    json obj = json::parse(R"({"field": 1})");
    JsonView view(obj);
    EXPECT_THROW(view.GetBool("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetBool_TypeMismatch_String) {
    json obj = json::parse(R"({"field": "true"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetBool("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetBool_TypeMismatch_Array) {
    json obj = json::parse(R"({"field": [true, false]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetBool("field"), ConfigError);
}

// ============================================================================
// GetObject() Tests (8 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetObject_ValidNestedObject) {
    json obj = json::parse(R"({
        "config": {
            "name": "nested",
            "value": 123
        }
    })");
    JsonView view(obj);
    JsonView nested = view.GetObject("config");
    EXPECT_EQ(nested.GetString("name"), "nested");
    EXPECT_EQ(nested.GetInt("value"), 123);
}

TEST_F(JsonViewBasicTest, GetObject_EmptyNestedObject) {
    json obj = json::parse(R"({"config": {}})");
    JsonView view(obj);
    JsonView nested = view.GetObject("config");
    EXPECT_TRUE(nested.Raw().empty());
}

TEST_F(JsonViewBasicTest, GetObject_DeeplyNested) {
    json obj = json::parse(R"({
        "level1": {
            "level2": {
                "level3": {
                    "value": "deep"
                }
            }
        }
    })");
    JsonView view(obj);
    JsonView l1 = view.GetObject("level1");
    JsonView l2 = l1.GetObject("level2");
    JsonView l3 = l2.GetObject("level3");
    EXPECT_EQ(l3.GetString("value"), "deep");
}

TEST_F(JsonViewBasicTest, GetObject_MissingObject_Throws) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetObject("missing"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetObject_NullValue_Throws) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_THROW(view.GetObject("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetObject_TypeMismatch_String) {
    json obj = json::parse(R"({"field": "value"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetObject("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetObject_TypeMismatch_Array) {
    json obj = json::parse(R"({"field": [1, 2, 3]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetObject("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetObject_TypeMismatch_Number) {
    json obj = json::parse(R"({"field": 42})");
    JsonView view(obj);
    EXPECT_THROW(view.GetObject("field"), ConfigError);
}

// ============================================================================
// GetStringArray() Tests (8 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetStringArray_ValidArray) {
    json obj = json::parse(R"({"names": ["alice", "bob", "charlie"]})");
    JsonView view(obj);
    auto result = view.GetStringArray("names");
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "alice");
    EXPECT_EQ(result[1], "bob");
    EXPECT_EQ(result[2], "charlie");
}

TEST_F(JsonViewBasicTest, GetStringArray_EmptyArray) {
    json obj = json::parse(R"({"names": []})");
    JsonView view(obj);
    auto result = view.GetStringArray("names");
    EXPECT_EQ(result.size(), 0);
}

TEST_F(JsonViewBasicTest, GetStringArray_SingleElement) {
    json obj = json::parse(R"({"names": ["single"]})");
    JsonView view(obj);
    auto result = view.GetStringArray("names");
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "single");
}

TEST_F(JsonViewBasicTest, GetStringArray_UnicodeStrings) {
    json obj = json::parse(R"({"names": ["日本語", "العربية", "Русский"]})");
    JsonView view(obj);
    auto result = view.GetStringArray("names");
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "日本語");
}

TEST_F(JsonViewBasicTest, GetStringArray_MissingArray_Throws) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetStringArray("missing"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetStringArray_NullValue_Throws) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_THROW(view.GetStringArray("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetStringArray_MixedTypeArray_Throws) {
    json obj = json::parse(R"({"field": ["string", 123, true]})");
    JsonView view(obj);
    EXPECT_THROW(view.GetStringArray("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetStringArray_TypeMismatch_NotArray) {
    json obj = json::parse(R"({"field": "not_array"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetStringArray("field"), ConfigError);
}

// ============================================================================
// GetArray() Tests (9 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, GetArray_ArrayOfObjects) {
    json obj = json::parse(R"({
        "items": [
            {"id": 1, "name": "first"},
            {"id": 2, "name": "second"}
        ]
    })");
    JsonView view(obj);
    auto items = view.GetArray("items");
    EXPECT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].GetInt("id"), 1);
    EXPECT_EQ(items[1].GetInt("id"), 2);
}

TEST_F(JsonViewBasicTest, GetArray_EmptyArray) {
    json obj = json::parse(R"({"items": []})");
    JsonView view(obj);
    auto items = view.GetArray("items");
    EXPECT_EQ(items.size(), 0);
}

TEST_F(JsonViewBasicTest, GetArray_ArrayOfMixedTypes) {
    json obj = json::parse(R"({
        "mixed": [
            "string",
            42,
            3.14,
            true,
            {"nested": "object"},
            [1, 2, 3]
        ]
    })");
    JsonView view(obj);
    auto items = view.GetArray("mixed");
    EXPECT_EQ(items.size(), 6);
    // All items should be wrapped in JsonView, can access raw values
    EXPECT_TRUE(items[0].Raw().is_string());
    EXPECT_TRUE(items[1].Raw().is_number_integer());
    EXPECT_TRUE(items[4].Raw().is_object());
}

TEST_F(JsonViewBasicTest, GetArray_NestedArrays) {
    json obj = json::parse(R"({
        "matrix": [[1, 2], [3, 4], [5, 6]]
    })");
    JsonView view(obj);
    auto items = view.GetArray("matrix");
    EXPECT_EQ(items.size(), 3);
    // Each item is a JsonView wrapping an array
    EXPECT_TRUE(items[0].Raw().is_array());
    EXPECT_TRUE(items[1].Raw().is_array());
}

TEST_F(JsonViewBasicTest, GetArray_ArrayWithNullElements) {
    json obj = json::parse(R"({
        "items": [
            {"id": 1},
            null,
            {"id": 3}
        ]
    })");
    JsonView view(obj);
    auto items = view.GetArray("items");
    EXPECT_EQ(items.size(), 3);
    // Null elements should be wrapped but will fail on field access
    EXPECT_TRUE(items[1].Raw().is_null());
}

TEST_F(JsonViewBasicTest, GetArray_MissingArray_Throws) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetArray("missing"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetArray_NullValue_Throws) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    EXPECT_THROW(view.GetArray("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetArray_TypeMismatch_NotArray) {
    json obj = json::parse(R"({"field": {"not": "array"}})");
    JsonView view(obj);
    EXPECT_THROW(view.GetArray("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetArray_Chaining) {
    json obj = json::parse(R"({
        "configs": [
            {"name": "config1", "enabled": true},
            {"name": "config2", "enabled": false}
        ]
    })");
    JsonView view(obj);
    auto configs = view.GetArray("configs");
    EXPECT_EQ(configs[0].GetString("name"), "config1");
    EXPECT_TRUE(configs[0].GetBool("enabled"));
    EXPECT_EQ(configs[1].GetString("name"), "config2");
    EXPECT_FALSE(configs[1].GetBool("enabled"));
}

// ============================================================================
// Error Message Validation Tests (4 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, ErrorMessage_ContainsFieldName) {
    json obj = json::parse(R"({"field": 42})");
    JsonView view(obj);
    try {
        view.GetString("field");
        FAIL() << "Expected ConfigError to be thrown";
    } catch (const ConfigError& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("field") != std::string::npos);
    }
}

TEST_F(JsonViewBasicTest, ErrorMessage_ContainsExpectedType) {
    json obj = json::parse(R"({"field": 42})");
    JsonView view(obj);
    try {
        view.GetString("field");
        FAIL() << "Expected ConfigError to be thrown";
    } catch (const ConfigError& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("string") != std::string::npos);
    }
}

TEST_F(JsonViewBasicTest, ErrorMessage_ContainsActualType) {
    json obj = json::parse(R"({"field": 42})");
    JsonView view(obj);
    try {
        view.GetString("field");
        FAIL() << "Expected ConfigError to be thrown";
    } catch (const ConfigError& e) {
        std::string msg = e.what();
        // nlohmann::json reports integer as "number" in type_name()
        EXPECT_TRUE(msg.find("number") != std::string::npos);
    }
}

TEST_F(JsonViewBasicTest, ErrorMessage_MissingRequiredField) {
    JsonView view(simple_object);
    try {
        view.GetFloat("nonexistent");
        FAIL() << "Expected ConfigError to be thrown";
    } catch (const ConfigError& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("nonexistent") != std::string::npos);
        EXPECT_TRUE(msg.find("Missing") != std::string::npos);
    }
}

// ============================================================================
// Edge Cases and Integration Tests (7 tests)
// ============================================================================

TEST_F(JsonViewBasicTest, ComplexNestedStructure) {
    json obj = json::parse(R"({
        "database": {
            "host": "localhost",
            "port": 5432,
            "credentials": {
                "username": "admin",
                "password": "secret"
            },
            "replicas": [
                {"host": "replica1", "port": 5433},
                {"host": "replica2", "port": 5434}
            ]
        }
    })");
    
    JsonView view(obj);
    JsonView db = view.GetObject("database");
    EXPECT_EQ(db.GetString("host"), "localhost");
    EXPECT_EQ(db.GetInt("port"), 5432);
    
    JsonView creds = db.GetObject("credentials");
    EXPECT_EQ(creds.GetString("username"), "admin");
    
    auto replicas = db.GetArray("replicas");
    EXPECT_EQ(replicas.size(), 2);
    EXPECT_EQ(replicas[0].GetString("host"), "replica1");
    EXPECT_EQ(replicas[1].GetInt("port"), 5434);
}

TEST_F(JsonViewBasicTest, LargeStringValue) {
    std::string large_str(10000, 'x');
    json obj = json::parse(R"({"field": ")" + large_str + R"("})");
    JsonView view(obj);
    EXPECT_EQ(view.GetString("field").size(), 10000);
}

TEST_F(JsonViewBasicTest, MultipleArraysWithDefaults) {
    json obj = json::parse(R"({
        "required": ["a", "b"],
        "optional": ["c", "d"]
    })");
    
    JsonView view(obj);
    auto req = view.GetStringArray("required");
    auto opt = view.GetStringArray("optional");
    
    EXPECT_EQ(req.size(), 2);
    EXPECT_EQ(opt.size(), 2);
}

TEST_F(JsonViewBasicTest, AllDataTypesInSingleObject) {
    json obj = json::parse(R"({
        "string": "value",
        "integer": 42,
        "float": 3.14,
        "boolean": true,
        "object": {"nested": "value"},
        "array": [1, 2, 3],
        "null": null
    })");
    
    JsonView view(obj);
    EXPECT_EQ(view.GetString("string"), "value");
    EXPECT_EQ(view.GetInt("integer"), 42);
    EXPECT_FLOAT_EQ(view.GetFloat("float"), 3.14f);
    EXPECT_TRUE(view.GetBool("boolean"));
    EXPECT_EQ(view.GetObject("object").GetString("nested"), "value");
    EXPECT_EQ(view.GetArray("array").size(), 3);
    // Null values are treated as missing, so return default value
    EXPECT_EQ(view.GetString("null"), "");
}

TEST_F(JsonViewBasicTest, StringArrayWithUnicodeAndSpecialChars) {
    json obj = json::parse(R"({
        "strings": ["hello\nworld", "tab\there", "quote\"mark", "unicode: 你好"]
    })");
    JsonView view(obj);
    auto strings = view.GetStringArray("strings");
    EXPECT_EQ(strings.size(), 4);
    EXPECT_EQ(strings[2], "quote\"mark");
}

TEST_F(JsonViewBasicTest, GetObjectThenChainArrays) {
    json obj = json::parse(R"({
        "service": {
            "name": "myservice",
            "endpoints": [
                {"url": "http://a", "port": 8080},
                {"url": "http://b", "port": 8081}
            ]
        }
    })");
    
    JsonView view(obj);
    JsonView service = view.GetObject("service");
    auto endpoints = service.GetArray("endpoints");
    EXPECT_EQ(endpoints[0].GetString("url"), "http://a");
    EXPECT_EQ(endpoints[1].GetInt("port"), 8081);
}

TEST_F(JsonViewBasicTest, DefaultValuesPreventExceptions) {
    json obj = json::parse(R"({"existing": "value"})");
    JsonView view(obj);
    
    // All of these should NOT throw
    EXPECT_EQ(view.GetString("missing", "default"), "default");
    EXPECT_FLOAT_EQ(view.GetFloat("missing", 1.0f), 1.0f);
    EXPECT_EQ(view.GetInt("missing", 0), 0);
    EXPECT_FALSE(view.GetBool("missing", false));
}

// ============================================================================
// C++26 ENHANCED TESTS - std::expected-based methods
// ============================================================================

TEST_F(JsonViewBasicTest, TryGetString_Success) {
    JsonView view(simple_object);
    auto result = view.TryGetString("name");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test");
}

TEST_F(JsonViewBasicTest, TryGetString_TypeMismatch_ReturnsError) {
    JsonView view(simple_object);
    auto result = view.TryGetString("count");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().what() != nullptr);
}

TEST_F(JsonViewBasicTest, TryGetString_WithDefault_ReturnsDefault) {
    JsonView view(simple_object);
    auto result = view.TryGetString("missing", "default");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "default");
}

TEST_F(JsonViewBasicTest, TryGetFloat_Success) {
    JsonView view(simple_object);
    auto result = view.TryGetFloat("ratio");
    EXPECT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 3.14f);
}

TEST_F(JsonViewBasicTest, TryGetFloat_Missing_NoDefault_ReturnsError) {
    JsonView view(simple_object);
    auto result = view.TryGetFloat("missing");  // NaN default = required
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetFloat_TypeMismatch_ReturnsError) {
    json obj = json::parse(R"({"field": "not_a_number"})");
    JsonView view(obj);
    auto result = view.TryGetFloat("field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetInt_Success) {
    JsonView view(simple_object);
    auto result = view.TryGetInt("count");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(JsonViewBasicTest, TryGetInt_RejectsFloat) {
    json obj = json::parse(R"({"field": 3.14})");
    JsonView view(obj);
    auto result = view.TryGetInt("field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetBool_Success) {
    JsonView view(simple_object);
    auto result = view.TryGetBool("enabled");
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST_F(JsonViewBasicTest, TryGetBool_TypeMismatch_ReturnsError) {
    json obj = json::parse(R"({"field": 1})");
    JsonView view(obj);
    auto result = view.TryGetBool("field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetObject_Success) {
    json obj = json::parse(R"({
        "config": {"name": "test", "value": 42}
    })");
    JsonView view(obj);
    auto result = view.TryGetObject("config");
    EXPECT_TRUE(result.has_value());
    auto name = result.value().TryGetString("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "test");
}

TEST_F(JsonViewBasicTest, TryGetObject_Missing_ReturnsError) {
    JsonView view(simple_object);
    auto result = view.TryGetObject("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetStringArray_Success) {
    json obj = json::parse(R"({"names": ["a", "b", "c"]})");
    JsonView view(obj);
    auto result = view.TryGetStringArray("names");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

TEST_F(JsonViewBasicTest, TryGetStringArray_TypeMismatch_ReturnsError) {
    json obj = json::parse(R"({"field": "not_array"})");
    JsonView view(obj);
    auto result = view.TryGetStringArray("field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, TryGetArray_Success) {
    json obj = json::parse(R"({
        "items": [{"id": 1}, {"id": 2}]
    })");
    JsonView view(obj);
    auto result = view.TryGetArray("items");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2);
}

TEST_F(JsonViewBasicTest, TryGetArray_Missing_ReturnsError) {
    JsonView view(simple_object);
    auto result = view.TryGetArray("missing");
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// C++26 ENHANCED TESTS - std::optional-based methods
// ============================================================================

TEST_F(JsonViewBasicTest, GetOptionalString_ExistingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalString("name");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test");
}

TEST_F(JsonViewBasicTest, GetOptionalString_MissingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalString("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalString_NullField) {
    json obj = json::parse(R"({"field": null})");
    JsonView view(obj);
    auto result = view.GetOptionalString("field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalString_TypeMismatch_Throws) {
    JsonView view(simple_object);
    EXPECT_THROW(view.GetOptionalString("count"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetOptionalFloat_ExistingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalFloat("ratio");
    EXPECT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 3.14f);
}

TEST_F(JsonViewBasicTest, GetOptionalFloat_MissingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalFloat("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalFloat_TypeMismatch_Throws) {
    json obj = json::parse(R"({"field": "text"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetOptionalFloat("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetOptionalInt_ExistingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalInt("count");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(JsonViewBasicTest, GetOptionalInt_MissingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalInt("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalInt_RejectsFloat) {
    json obj = json::parse(R"({"field": 3.14})");
    JsonView view(obj);
    EXPECT_THROW(view.GetOptionalInt("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetOptionalBool_ExistingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalBool("enabled");
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST_F(JsonViewBasicTest, GetOptionalBool_MissingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalBool("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalBool_TypeMismatch_Throws) {
    json obj = json::parse(R"({"field": "true"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetOptionalBool("field"), ConfigError);
}

TEST_F(JsonViewBasicTest, GetOptionalObject_ExistingField) {
    json obj = json::parse(R"({
        "config": {"name": "test"}
    })");
    JsonView view(obj);
    auto result = view.GetOptionalObject("config");
    EXPECT_TRUE(result.has_value());
    auto name = result.value().TryGetString("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "test");
}

TEST_F(JsonViewBasicTest, GetOptionalObject_MissingField) {
    JsonView view(simple_object);
    auto result = view.GetOptionalObject("missing");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonViewBasicTest, GetOptionalObject_TypeMismatch_Throws) {
    json obj = json::parse(R"({"field": "not_object"})");
    JsonView view(obj);
    EXPECT_THROW(view.GetOptionalObject("field"), ConfigError);
}

// ============================================================================
// C++26 Pattern Usage Tests - Real-world error handling patterns
// ============================================================================

TEST_F(JsonViewBasicTest, ExpectedBasedErrorHandling_ChainedOperations) {
    json obj = json::parse(R"({
        "port": 8080,
        "timeout": 30.5,
        "name": "server"
    })");
    JsonView view(obj);
    
    // C++26 pattern: chain operations with expected
    auto port_result = view.TryGetInt("port");
    auto timeout_result = view.TryGetFloat("timeout");
    auto name_result = view.TryGetString("name");
    
    EXPECT_TRUE(port_result.has_value());
    EXPECT_TRUE(timeout_result.has_value());
    EXPECT_TRUE(name_result.has_value());
    
    if (port_result && timeout_result && name_result) {
        int port = port_result.value();
        float timeout = timeout_result.value();
        std::string name = name_result.value();
        
        EXPECT_EQ(port, 8080);
        EXPECT_FLOAT_EQ(timeout, 30.5f);
        EXPECT_EQ(name, "server");
    }
}

TEST_F(JsonViewBasicTest, OptionalBasedMissingFieldHandling) {
    json obj = json::parse(R"({
        "required": "value",
        "optional_int": 42
    })");
    JsonView view(obj);
    
    // C++26 pattern: use optional for graceful missing field handling
    auto required = view.GetOptionalString("required");
    auto optional = view.GetOptionalInt("missing_int");
    auto present_opt = view.GetOptionalInt("optional_int");
    
    EXPECT_TRUE(required.has_value());
    EXPECT_FALSE(optional.has_value());
    EXPECT_TRUE(present_opt.has_value());
    
    if (auto val = view.GetOptionalString("required")) {
        EXPECT_EQ(val.value(), "value");
    }
    
    if (auto val = view.GetOptionalInt("missing_int")) {
        FAIL() << "Should not have value";
    } else {
        // Gracefully handle missing field
        EXPECT_TRUE(true);
    }
}

TEST_F(JsonViewBasicTest, ExpectedWithErrorContextHandling) {
    json obj = json::parse(R"({
        "age": "not_a_number"
    })");
    JsonView view(obj);
    
    // C++26 pattern: extract and examine errors
    auto result = view.TryGetInt("age");
    
    if (!result) {
        ConfigError error = result.error();
        std::string error_msg = error.what();
        
        // Verify error contains useful context
        EXPECT_TRUE(error_msg.find("age") != std::string::npos);
        EXPECT_TRUE(error_msg.find("integer") != std::string::npos);
    } else {
        FAIL() << "Should have error";
    }
}

TEST_F(JsonViewBasicTest, OptionalChaining_NestedObjects) {
    json obj = json::parse(R"({
        "database": {
            "host": "localhost",
            "port": 5432
        }
    })");
    JsonView view(obj);
    
    // C++26 pattern: chain optional getters
    if (auto db = view.GetOptionalObject("database")) {
        if (auto host = db.value().GetOptionalString("host")) {
            if (auto port = db.value().GetOptionalInt("port")) {
                EXPECT_EQ(host.value(), "localhost");
                EXPECT_EQ(port.value(), 5432);
            }
        }
    }
}

TEST_F(JsonViewBasicTest, ExpectedAndOptionalComparison) {
    json obj = json::parse(R"({
        "exists": "value",
        "missing_field": null
    })");
    JsonView view(obj);
    
    // Compare expected and optional approaches
    auto expected_result = view.TryGetString("exists");
    auto optional_result = view.GetOptionalString("exists");
    
    EXPECT_TRUE(expected_result.has_value());
    EXPECT_TRUE(optional_result.has_value());
    EXPECT_EQ(expected_result.value(), optional_result.value());
    
    // Both handle missing fields gracefully but differently
    auto expected_missing = view.TryGetString("nonexistent", "default");
    auto optional_missing = view.GetOptionalString("nonexistent");
    
    // expected with default returns the default
    EXPECT_TRUE(expected_missing.has_value());
    EXPECT_EQ(expected_missing.value(), "default");
    
    // optional always returns nullopt for missing
    EXPECT_FALSE(optional_missing.has_value());
}
