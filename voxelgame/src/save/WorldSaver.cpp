// vg/save/WorldSaver.cpp — see WorldSaver.hpp for the contract.
#include "vg/save/WorldSaver.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

#include "vg/save/ChunkCodec.hpp"

namespace vg::save {

namespace fs = std::filesystem;
using world::Chunk;
using world::ChunkCoord;

WorldSaver::WorldSaver(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

std::string WorldSaver::chunkPath(ChunkCoord p) const {
    return dir_ + "/c." + std::to_string(p.x) + "." + std::to_string(p.z) + ".chunk";
}

bool WorldSaver::hasChunk(ChunkCoord p) const {
    return fs::exists(chunkPath(p));
}

bool WorldSaver::saveChunk(const Chunk& c) const {
    std::vector<uint8_t> bytes = serializeChunk(c);
    std::ofstream f(chunkPath(c.pos()), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return (bool)f;
}

bool WorldSaver::loadChunk(ChunkCoord pos, Chunk& out) const {
    std::ifstream f(chunkPath(pos), std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return deserializeChunk(bytes, out);
}

bool WorldSaver::saveMeta(const WorldMeta& m) const {
    std::ofstream f(dir_ + "/level.dat", std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write("VGL1", 4);
    f.write(reinterpret_cast<const char*>(&m.seed), sizeof m.seed);
    f.write(reinterpret_cast<const char*>(&m.px), sizeof m.px);
    f.write(reinterpret_cast<const char*>(&m.py), sizeof m.py);
    f.write(reinterpret_cast<const char*>(&m.pz), sizeof m.pz);
    return (bool)f;
}

bool WorldSaver::loadMeta(WorldMeta& out) const {
    std::ifstream f(dir_ + "/level.dat", std::ios::binary);
    if (!f) return false;
    char magic[4];
    f.read(magic, 4);
    if (std::string(magic, 4) != "VGL1") return false;
    f.read(reinterpret_cast<char*>(&out.seed), sizeof out.seed);
    f.read(reinterpret_cast<char*>(&out.px), sizeof out.px);
    f.read(reinterpret_cast<char*>(&out.py), sizeof out.py);
    f.read(reinterpret_cast<char*>(&out.pz), sizeof out.pz);
    return (bool)f;
}

int WorldSaver::saveDirty(world::World& world) const {
    int n = 0;
    for (ChunkCoord c : world.loadedCoords()) {
        Chunk* ch = world.mutableChunk(c);
        if (ch && ch->dirty()) {
            if (saveChunk(*ch)) { ch->clearDirty(); ++n; }
        }
    }
    return n;
}

int WorldSaver::saveAll(const world::World& world) const {
    int n = 0;
    for (ChunkCoord c : world.loadedCoords())
        if (const Chunk* ch = world.find(c)) { if (saveChunk(*ch)) ++n; }
    return n;
}

int WorldSaver::loadInto(world::World& world, const std::vector<ChunkCoord>& coords) const {
    int n = 0;
    for (ChunkCoord c : coords) {
        if (!hasChunk(c)) continue;
        auto chunk = std::make_unique<Chunk>(c);
        if (loadChunk(c, *chunk)) { world.putChunk(std::move(chunk)); ++n; }
    }
    return n;
}

}  // namespace vg::save
