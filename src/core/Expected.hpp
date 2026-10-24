#pragma once

// Small result type used by the core library. The core is deliberately free of
// Geode/cocos dependencies so it can be unit-tested on any desktop compiler.

#include <optional>
#include <string>
#include <utility>

namespace fwa {

template <class T>
class Expected {
public:
    Expected(T value) : m_value(std::move(value)) {}

    static Expected fail(std::string message) {
        Expected e;
        e.m_error = std::move(message);
        return e;
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    T& value() & { return *m_value; }
    T const& value() const& { return *m_value; }
    T&& value() && { return std::move(*m_value); }

    std::string const& error() const { return m_error; }

private:
    Expected() = default;
    std::optional<T> m_value;
    std::string m_error;
};

} // namespace fwa
