#include "MsgPack.hpp"

#include <cstring>

namespace fwa::msgpack {

namespace {

class Decoder {
public:
    Decoder(std::span<uint8_t const> data, size_t maxDepth) : m_data(data), m_maxDepth(maxDepth) {}

    Expected<json::Value> run() {
        json::Value v;
        if (!decodeValue(v, 0)) return Expected<json::Value>::fail(m_error);
        if (m_pos != m_data.size()) {
            return Expected<json::Value>::fail(
                "MessagePack error: " + std::to_string(m_data.size() - m_pos) + " unexpected trailing byte(s) after the document"
            );
        }
        return v;
    }

private:
    std::span<uint8_t const> m_data;
    size_t m_pos = 0;
    size_t m_maxDepth;
    std::string m_error;

    bool fail(std::string const& msg) {
        if (m_error.empty()) m_error = "MessagePack error at byte " + std::to_string(m_pos) + ": " + msg;
        return false;
    }

    bool need(size_t n) {
        if (m_data.size() - m_pos < n) return fail("unexpected end of data");
        return true;
    }

    template <class T>
    bool readBE(T& out) {
        if (!need(sizeof(T))) return false;
        uint64_t v = 0;
        for (size_t i = 0; i < sizeof(T); i++) v = (v << 8) | m_data[m_pos + i];
        m_pos += sizeof(T);
        if constexpr (std::is_same_v<T, float>) {
            uint32_t bits = static_cast<uint32_t>(v);
            std::memcpy(&out, &bits, sizeof(float));
        } else if constexpr (std::is_same_v<T, double>) {
            std::memcpy(&out, &v, sizeof(double));
        } else {
            out = static_cast<T>(v);
        }
        return true;
    }

    bool readStr(size_t len, std::string& out) {
        if (!need(len)) return false;
        out.assign(reinterpret_cast<char const*>(m_data.data() + m_pos), len);
        m_pos += len;
        return true;
    }

    bool readArray(size_t count, json::Value& out, size_t depth) {
        // Every element needs at least one byte; reject impossible counts early
        // so a corrupted header can't make us reserve gigabytes.
        if (count > m_data.size() - m_pos) return fail("array length exceeds remaining data");
        json::Array arr;
        arr.reserve(count);
        for (size_t i = 0; i < count; i++) {
            json::Value v;
            if (!decodeValue(v, depth + 1)) return false;
            arr.push_back(std::move(v));
        }
        out = json::Value(std::move(arr));
        return true;
    }

    bool readMap(size_t count, json::Value& out, size_t depth) {
        if (count > (m_data.size() - m_pos) / 2) return fail("map length exceeds remaining data");
        json::Value obj = json::Value::object();
        for (size_t i = 0; i < count; i++) {
            json::Value key, value;
            if (!decodeValue(key, depth + 1)) return false;
            if (!decodeValue(value, depth + 1)) return false;
            std::string k;
            if (key.isString()) k = key.asString();
            else if (key.isNumber()) k = key.isInteger() ? std::to_string(key.asInt()) : json::formatNumber(key.asDouble());
            else if (key.isBool()) k = key.asBool() ? "true" : "false";
            else if (key.isNull()) k = "null";
            else return fail("unsupported map key type");
            obj.set(k, std::move(value));
        }
        out = std::move(obj);
        return true;
    }

    bool decodeValue(json::Value& out, size_t depth) {
        if (depth > m_maxDepth) return fail("document is nested too deeply");
        if (!need(1)) return false;
        uint8_t b = m_data[m_pos++];

        if (b <= 0x7f) { out = json::Value(static_cast<int64_t>(b)); return true; }
        if (b >= 0xe0) { out = json::Value(static_cast<int64_t>(static_cast<int8_t>(b))); return true; }
        if ((b & 0xf0) == 0x80) return readMap(b & 0x0f, out, depth);
        if ((b & 0xf0) == 0x90) return readArray(b & 0x0f, out, depth);
        if ((b & 0xe0) == 0xa0) {
            std::string s;
            if (!readStr(b & 0x1f, s)) return false;
            out = json::Value(std::move(s));
            return true;
        }

        switch (b) {
            case 0xc0: out = json::Value(nullptr); return true;
            case 0xc2: out = json::Value(false); return true;
            case 0xc3: out = json::Value(true); return true;
            case 0xc4: case 0xc5: case 0xc6: { // bin 8/16/32 -> kept as a raw string
                uint32_t len = 0;
                if (b == 0xc4) { uint8_t l; if (!readBE(l)) return false; len = l; }
                else if (b == 0xc5) { uint16_t l; if (!readBE(l)) return false; len = l; }
                else { if (!readBE(len)) return false; }
                std::string s;
                if (!readStr(len, s)) return false;
                out = json::Value(std::move(s));
                return true;
            }
            case 0xca: { float f; if (!readBE(f)) return false; out = json::Value(static_cast<double>(f)); return true; }
            case 0xcb: { double d; if (!readBE(d)) return false; out = json::Value(d); return true; }
            case 0xcc: { uint8_t v; if (!readBE(v)) return false; out = json::Value(static_cast<uint64_t>(v)); return true; }
            case 0xcd: { uint16_t v; if (!readBE(v)) return false; out = json::Value(static_cast<uint64_t>(v)); return true; }
            case 0xce: { uint32_t v; if (!readBE(v)) return false; out = json::Value(static_cast<uint64_t>(v)); return true; }
            case 0xcf: { uint64_t v; if (!readBE(v)) return false; out = json::Value(v); return true; }
            case 0xd0: { uint8_t v; if (!readBE(v)) return false; out = json::Value(static_cast<int64_t>(static_cast<int8_t>(v))); return true; }
            case 0xd1: { uint16_t v; if (!readBE(v)) return false; out = json::Value(static_cast<int64_t>(static_cast<int16_t>(v))); return true; }
            case 0xd2: { uint32_t v; if (!readBE(v)) return false; out = json::Value(static_cast<int64_t>(static_cast<int32_t>(v))); return true; }
            case 0xd3: { uint64_t v; if (!readBE(v)) return false; out = json::Value(static_cast<int64_t>(v)); return true; }
            case 0xd9: { uint8_t l; if (!readBE(l)) return false; std::string s; if (!readStr(l, s)) return false; out = json::Value(std::move(s)); return true; }
            case 0xda: { uint16_t l; if (!readBE(l)) return false; std::string s; if (!readStr(l, s)) return false; out = json::Value(std::move(s)); return true; }
            case 0xdb: { uint32_t l; if (!readBE(l)) return false; std::string s; if (!readStr(l, s)) return false; out = json::Value(std::move(s)); return true; }
            case 0xdc: { uint16_t n; if (!readBE(n)) return false; return readArray(n, out, depth); }
            case 0xdd: { uint32_t n; if (!readBE(n)) return false; return readArray(n, out, depth); }
            case 0xde: { uint16_t n; if (!readBE(n)) return false; return readMap(n, out, depth); }
            case 0xdf: { uint32_t n; if (!readBE(n)) return false; return readMap(n, out, depth); }
            default:
                // 0xc1 (never used), ext types 0xc7-0xc9 / 0xd4-0xd8: not used by GDR.
                m_pos--;
                return fail("unsupported MessagePack type byte 0x" + toHex(b));
        }
    }

    static std::string toHex(uint8_t b) {
        char const* digits = "0123456789abcdef";
        std::string s;
        s += digits[b >> 4];
        s += digits[b & 15];
        return s;
    }
};

} // namespace

Expected<json::Value> decode(std::span<uint8_t const> data, size_t maxDepth) {
    return Decoder(data, maxDepth).run();
}

bool looksLikeMap(std::span<uint8_t const> data) {
    if (data.size() < 2) return false;
    uint8_t b = data[0];
    size_t keyPos;
    if ((b & 0xf0) == 0x80) {
        if ((b & 0x0f) == 0) return data.size() == 1;
        keyPos = 1;
    } else if (b == 0xde) {
        keyPos = 3;
    } else if (b == 0xdf) {
        keyPos = 5;
    } else {
        return false;
    }
    // GDR documents are maps with string keys: the first key must be a string.
    if (keyPos >= data.size()) return false;
    uint8_t k = data[keyPos];
    return (k & 0xe0) == 0xa0 || k == 0xd9 || k == 0xda || k == 0xdb;
}

} // namespace fwa::msgpack
