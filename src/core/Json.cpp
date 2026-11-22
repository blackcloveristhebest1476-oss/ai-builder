#include "Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale>
#include <sstream>

namespace fwa::json {

namespace {

Value const& nullValue() {
    static Value const v;
    return v;
}

Array const& emptyArray() {
    static Array const a;
    return a;
}

Object const& emptyObject() {
    static Object const o;
    return o;
}

} // namespace

int64_t Value::asInt(int64_t def) const {
    if (m_type != Type::Number) return def;
    if (m_isInt) return m_int;
    if (!std::isfinite(m_number)) return def;
    if (m_number >= 9.2e18 || m_number <= -9.2e18) return def;
    return static_cast<int64_t>(m_number);
}

std::string const& Value::asString() const {
    static std::string const empty;
    return m_type == Type::String ? m_string : empty;
}

Array const& Value::items() const {
    return m_type == Type::Array ? *m_array : emptyArray();
}

Array& Value::items() {
    if (m_type != Type::Array) {
        *this = Value(Array{});
    }
    return *m_array;
}

Object const& Value::members() const {
    return m_type == Type::Object ? *m_object : emptyObject();
}

Object& Value::members() {
    if (m_type != Type::Object) {
        *this = Value(Object{});
    }
    return *m_object;
}

size_t Value::size() const {
    if (m_type == Type::Array) return m_array->size();
    if (m_type == Type::Object) return m_object->size();
    return 0;
}

bool Value::contains(std::string_view key) const {
    if (m_type != Type::Object) return false;
    for (auto const& [k, v] : *m_object) {
        if (k == key) return true;
    }
    return false;
}

Value const& Value::operator[](std::string_view key) const {
    if (m_type != Type::Object) return nullValue();
    for (auto const& [k, v] : *m_object) {
        if (k == key) return v;
    }
    return nullValue();
}

Value const& Value::at(size_t index) const {
    if (m_type != Type::Array || index >= m_array->size()) return nullValue();
    return (*m_array)[index];
}

Value& Value::set(std::string_view key, Value v) {
    auto& obj = this->members();
    for (auto& [k, existing] : obj) {
        if (k == key) {
            existing = std::move(v);
            return existing;
        }
    }
    obj.emplace_back(std::string(key), std::move(v));
    return obj.back().second;
}

void Value::push(Value v) {
    this->items().push_back(std::move(v));
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
    Parser(std::string_view text, size_t maxDepth) : m_text(text), m_maxDepth(maxDepth) {}

    Expected<Value> run() {
        // Skip a UTF-8 BOM, which some Windows editors add.
        if (m_text.size() >= 3 && static_cast<unsigned char>(m_text[0]) == 0xEF &&
            static_cast<unsigned char>(m_text[1]) == 0xBB && static_cast<unsigned char>(m_text[2]) == 0xBF) {
            m_pos = 3;
        }
        Value v;
        if (!parseValue(v, 0)) return Expected<Value>::fail(m_error);
        skipWs();
        if (m_pos != m_text.size()) {
            return Expected<Value>::fail(errorAt("unexpected data after the end of the JSON document"));
        }
        return v;
    }

private:
    std::string_view m_text;
    size_t m_pos = 0;
    size_t m_maxDepth;
    std::string m_error;

    std::string errorAt(std::string const& msg) const {
        // Report line/column, which is what people can actually find in an editor.
        size_t line = 1, col = 1;
        for (size_t i = 0; i < m_pos && i < m_text.size(); i++) {
            if (m_text[i] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        return "JSON error at line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + msg;
    }

    bool fail(std::string const& msg) {
        if (m_error.empty()) m_error = errorAt(msg);
        return false;
    }

    void skipWs() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                m_pos++;
            } else {
                break;
            }
        }
    }

    bool literal(std::string_view word) {
        if (m_text.substr(m_pos, word.size()) == word) {
            m_pos += word.size();
            return true;
        }
        return false;
    }

    bool parseValue(Value& out, size_t depth) {
        if (depth > m_maxDepth) return fail("document is nested too deeply");
        skipWs();
        if (m_pos >= m_text.size()) return fail("unexpected end of input");
        char c = m_text[m_pos];
        switch (c) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (literal("true")) { out = Value(true); return true; }
                return fail("invalid literal");
            case 'f':
                if (literal("false")) { out = Value(false); return true; }
                return fail("invalid literal");
            case 'n':
                if (literal("null")) { out = Value(nullptr); return true; }
                return fail("invalid literal");
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
                return fail(std::string("unexpected character '") + c + "'");
        }
    }

    bool parseObject(Value& out, size_t depth) {
        m_pos++; // {
        Object obj;
        skipWs();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            m_pos++;
            out = Value(std::move(obj));
            return true;
        }
        while (true) {
            skipWs();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("expected a string key");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail("expected ':' after key");
            m_pos++;
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            // Duplicate keys: last one wins (same as JavaScript's JSON.parse).
            bool replaced = false;
            for (auto& [k, existing] : obj) {
                if (k == key) {
                    existing = std::move(v);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) obj.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (m_pos >= m_text.size()) return fail("unexpected end of input in object");
            if (m_text[m_pos] == ',') {
                m_pos++;
                continue;
            }
            if (m_text[m_pos] == '}') {
                m_pos++;
                break;
            }
            return fail("expected ',' or '}' in object");
        }
        out = Value(std::move(obj));
        return true;
    }

    bool parseArray(Value& out, size_t depth) {
        m_pos++; // [
        Array arr;
        skipWs();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            m_pos++;
            out = Value(std::move(arr));
            return true;
        }
        while (true) {
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (m_pos >= m_text.size()) return fail("unexpected end of input in array");
            if (m_text[m_pos] == ',') {
                m_pos++;
                continue;
            }
            if (m_text[m_pos] == ']') {
                m_pos++;
                break;
            }
            return fail("expected ',' or ']' in array");
        }
        out = Value(std::move(arr));
        return true;
    }

    static void appendUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(uint32_t& out) {
        if (m_pos + 4 > m_text.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; i++) {
            char c = m_text[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("invalid \\u escape");
        }
        return true;
    }

    bool parseString(std::string& out) {
        m_pos++; // opening quote
        while (true) {
            if (m_pos >= m_text.size()) return fail("unterminated string");
            char c = m_text[m_pos++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) return fail("unterminated escape");
            char e = m_text[m_pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // surrogate pair
                        if (m_pos + 2 <= m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                            m_pos += 2;
                            uint32_t lo;
                            if (!parseHex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape sequence");
            }
        }
    }

    bool parseNumber(Value& out) {
        size_t start = m_pos;
        bool isInt = true;
        if (m_text[m_pos] == '-') m_pos++;
        if (m_pos >= m_text.size()) return fail("invalid number");
        if (m_text[m_pos] == '0') {
            m_pos++;
        } else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9') {
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') m_pos++;
        } else {
            return fail("invalid number");
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            isInt = false;
            m_pos++;
            size_t digits = 0;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                m_pos++;
                digits++;
            }
            if (digits == 0) return fail("invalid number (no digits after '.')");
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            isInt = false;
            m_pos++;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) m_pos++;
            size_t digits = 0;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                m_pos++;
                digits++;
            }
            if (digits == 0) return fail("invalid number exponent");
        }
        std::string token(m_text.substr(start, m_pos - start));

        if (isInt && token.size() <= 18) {
            int64_t iv = 0;
            bool neg = token[0] == '-';
            for (size_t i = neg ? 1 : 0; i < token.size(); i++) iv = iv * 10 + (token[i] - '0');
            out = Value(neg ? -iv : iv);
            return true;
        }

        std::istringstream ss(token);
        ss.imbue(std::locale::classic());
        double d = 0.0;
        ss >> d;
        if (ss.fail()) {
            // Out-of-range literals (e.g. 1e999) -> infinity, mirroring JavaScript.
            d = token[0] == '-' ? -HUGE_VAL : HUGE_VAL;
            bool tiny = token.find("e-") != std::string::npos || token.find("E-") != std::string::npos;
            if (tiny) d = 0.0;
        }
        out = Value(d);
        return true;
    }
};

void escapeInto(std::string& out, std::string const& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void dumpInto(std::string& out, Value const& v, int indent, int level) {
    auto newline = [&](int lvl) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * lvl), ' ');
    };
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number:
            if (v.isInteger()) out += std::to_string(v.asInt());
            else out += formatNumber(v.asDouble());
            break;
        case Value::Type::String: escapeInto(out, v.asString()); break;
        case Value::Type::Array: {
            auto const& arr = v.items();
            if (arr.empty()) {
                out += "[]";
                break;
            }
            // Arrays of scalars stay on one line; it keeps exports readable.
            bool scalarOnly = true;
            for (auto const& item : arr) {
                if (item.isArray() || item.isObject()) {
                    scalarOnly = false;
                    break;
                }
            }
            out += '[';
            for (size_t i = 0; i < arr.size(); i++) {
                if (i) out += ',';
                if (!scalarOnly) newline(level + 1);
                else if (i && indent >= 0) out += ' ';
                dumpInto(out, arr[i], indent, level + 1);
            }
            if (!scalarOnly) newline(level);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            auto const& obj = v.members();
            if (obj.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            bool first = true;
            for (auto const& [k, item] : obj) {
                if (!first) out += ',';
                first = false;
                newline(level + 1);
                escapeInto(out, k);
                out += indent < 0 ? ":" : ": ";
                dumpInto(out, item, indent, level + 1);
            }
            newline(level);
            out += '}';
            break;
        }
    }
}

} // namespace

Expected<Value> parse(std::string_view text, size_t maxDepth) {
    return Parser(text, maxDepth).run();
}

std::string formatNumber(double v) {
    if (!std::isfinite(v)) return "null";
    if (v == 0.0) return "0";
    // Integral values within the exactly-representable range print without a fraction.
    if (std::fabs(v) < 9007199254740992.0 && v == std::floor(v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    // Shortest of %.15g / %.16g / %.17g that round-trips. snprintf always uses
    // '.' for the "C" locale, which is the default unless setlocale() is called;
    // we normalise ',' defensively anyway.
    char buf[40];
    for (int precision = 15; precision <= 17; precision++) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
        for (char* p = buf; *p; ++p) {
            if (*p == ',') *p = '.';
        }
        std::istringstream ss(buf);
        ss.imbue(std::locale::classic());
        double back = 0.0;
        ss >> back;
        if (back == v) break;
    }
    return buf;
}

std::string dump(Value const& value, int indent) {
    std::string out;
    dumpInto(out, value, indent, 0);
    return out;
}

} // namespace fwa::json
