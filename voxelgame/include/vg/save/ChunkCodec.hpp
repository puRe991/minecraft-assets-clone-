// vg/save/ChunkCodec.hpp
//
// (De)serialization for a single chunk, with run-length compression of the
// block ids. Terrain is highly repetitive (long runs of air/stone/water), so
// RLE shrinks a chunk dramatically while staying trivial and dependency-free.
// The byte format is portable (little-endian, explicit sizes).
#pragma once

#include <cstdint>
#include <vector>

#include "vg/world/Chunk.hpp"

namespace vg::save {

// Serialize a chunk to bytes: "VGC1" magic, chunk x/z, then RLE block runs.
std::vector<uint8_t> serializeChunk(const world::Chunk& chunk);

// Deserialize into `out` (whose position must match, or is overwritten). On
// success `out` holds the decoded blocks; returns false on a malformed buffer.
bool deserializeChunk(const std::vector<uint8_t>& bytes, world::Chunk& out);

}  // namespace vg::save
