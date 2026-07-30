// vg/save/ChunkCodec.cpp — see ChunkCodec.hpp for the contract.
#include "vg/save/ChunkCodec.hpp"

#include <cstring>

namespace vg::save {

using world::BlockId;
using world::Chunk;
using world::CHUNK_VOL;

namespace {
void putU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); }
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
    b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff);
}
void putI32(std::vector<uint8_t>& b, int32_t v) { putU32(b, (uint32_t)v); }

bool getU16(const std::vector<uint8_t>& b, size_t& i, uint16_t& out) {
    if (i + 2 > b.size()) return false;
    out = (uint16_t)(b[i] | (b[i + 1] << 8)); i += 2; return true;
}
bool getU32(const std::vector<uint8_t>& b, size_t& i, uint32_t& out) {
    if (i + 4 > b.size()) return false;
    out = (uint32_t)b[i] | ((uint32_t)b[i + 1] << 8) | ((uint32_t)b[i + 2] << 16) | ((uint32_t)b[i + 3] << 24);
    i += 4; return true;
}
}  // namespace

std::vector<uint8_t> serializeChunk(const Chunk& chunk) {
    std::vector<uint8_t> out;
    out.push_back('V'); out.push_back('G'); out.push_back('C'); out.push_back('1');
    putI32(out, chunk.pos().x);
    putI32(out, chunk.pos().z);

    const std::vector<BlockId>& blk = chunk.data();
    size_t i = 0;
    while (i < blk.size()) {
        BlockId v = blk[i];
        uint32_t run = 1;
        while (i + run < blk.size() && blk[i + run] == v) ++run;
        putU16(out, v);
        putU32(out, run);
        i += run;
    }
    return out;
}

bool deserializeChunk(const std::vector<uint8_t>& b, Chunk& out) {
    size_t i = 0;
    if (b.size() < 12 || b[0] != 'V' || b[1] != 'G' || b[2] != 'C' || b[3] != '1') return false;
    i = 4;
    uint32_t cx, cz;
    if (!getU32(b, i, cx) || !getU32(b, i, cz)) return false;

    std::vector<BlockId>& blk = out.data();
    size_t w = 0;
    while (w < blk.size()) {
        uint16_t v; uint32_t run;
        if (!getU16(b, i, v) || !getU32(b, i, run)) return false;
        while (run-- && w < blk.size()) blk[w++] = v;
    }
    out.markGenerated();
    out.clearDirty();
    return w == blk.size();
}

}  // namespace vg::save
