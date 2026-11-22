#pragma once

// Minimal, dependency-free JSON value + parser + writer.
// - Objects keep insertion order (stable, diff-friendly exports).
// - Numbers are parsed with the classic "C" locale so a user's system locale
//   (e.g. "," as decimal separator) can never change how a file is read.
// - Nesting depth is limited to protect against stack exhaustion on hostile files.

#include "Expected.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fwa::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T v) : m_type(Type::Number), m_number(static_cast<double>(v)) {
        if constexpr (std::is_signed_v<T>) {
            m_isInt = true;
            m_int = static_cast<int64_t>(v);
        } else {
            m_isInt = static_cast<uint64_t>(v) <= static_cast<uint64_t>(INT64_MAX);
            m_int = static_cast<int64_t>(v);
        }
    }
    Value(double v) : m_type(Type::Number), m_number(v) {}
    Value(float v) : m_type(Type::Number), m_number(v) {}
    Value(char const* s) : m_type(Type::String), m_string(s) {}
    Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {}
    Value(std::string_view s) : m_type(Type::String), m_string(s) {}
    Value(Array a) : m_type(Type::Array), m_array(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : m_type(Type::Object), m_object(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool asBool(bool def = false) const { return m_type == Type::Bool ? m_bool : def; }
    double asDouble(double def = 0.0) const { return m_type == Type::Number ? m_number : def; }
    /// True when the number was written as an integer literal (no fraction/exponent)
    /// and fits in int64.
    bool isInteger() const { return m_type == Type::Number && m_isInt; }
    int64_t asInt(int64_t def = 0) const;
    std::string const& asString() const;

    Array const& items() const;
    Array& items();
    Object const& members() const;
    Object& members();

    size_t size() const;
    bool contains(std::string_view key) const;
    /// Returns a static null value when the key is missing or this is not an object.
    Value const& operator[](std::string_view key) const;
    Value const& at(size_t index) const;

    /// Object helpers (no-ops on non-objects except `set`, which converts null to object).
    Value& set(std::string_view key, Value v);
    void push(Value v);

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    bool m_isInt = false;
    int64_t m_int = 0;
    std::string m_string;
    std::shared_ptr<Array> m_array;
    std::shared_ptr<Object> m_object;
};

/// Parse a complete JSON document. Trailing non-whitespace is an error.
Expected<Value> parse(std::string_view text, size_t maxDepth = 256);

/// Serialize. indent < 0 => compact single line.
std::string dump(Value const& value, int indent = 2);

/// Format a double the way JSON wants it (shortest that round-trips, never
/// locale dependent, NaN/Inf become null).
std::string formatNumber(double v);

} // namespace fwa::json
