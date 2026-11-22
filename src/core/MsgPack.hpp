#pragma once

// MessagePack -> json::Value decoder. GDR 1 replays may be stored either as
// JSON text or as the MessagePack encoding of the same document, so both feed
// the same field-reading code.

#include "Json.hpp"

#include <cstdint>
#include <span>

namespace fwa::msgpack {

/// Decode exactly one MessagePack object that spans the whole buffer.
/// Map keys that are not strings are converted to their decimal/text form.
Expected<json::Value> decode(std::span<uint8_t const> data, size_t maxDepth = 128);

/// Cheap sniff: does the first byte look like the start of a MessagePack map?
bool looksLikeMap(std::span<uint8_t const> data);

} // namespace fwa::msgpack
