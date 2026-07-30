// vg/save/WorldSaver.hpp
//
// Persists a world to a directory: one file per chunk plus a small level file
// (seed + player position). Only dirty chunks need re-saving. An AutoSaver
// timer triggers periodic saves. File I/O uses <fstream>/<filesystem> so it
// works identically on Windows and Linux.
#pragma once

#include <string>

#include "vg/world/World.hpp"

namespace vg::save {

struct WorldMeta {
    uint32_t seed{0};
    double px{0}, py{0}, pz{0};
};

class WorldSaver {
public:
    explicit WorldSaver(std::string dir);

    bool saveChunk(const world::Chunk& c) const;
    bool loadChunk(world::ChunkCoord pos, world::Chunk& out) const;
    bool hasChunk(world::ChunkCoord pos) const;

    bool saveMeta(const WorldMeta& m) const;
    bool loadMeta(WorldMeta& out) const;

    // Save every dirty chunk and clear its flag; returns how many were written.
    int saveDirty(world::World& world) const;
    // Save all loaded chunks (used for a full/initial save).
    int saveAll(const world::World& world) const;
    // Load any chunks that exist on disk into the world (replacing them).
    int loadInto(world::World& world, const std::vector<world::ChunkCoord>& coords) const;

    const std::string& dir() const { return dir_; }

private:
    std::string chunkPath(world::ChunkCoord pos) const;
    std::string dir_;
};

// Fires `true` from tick() once every `interval` seconds.
class AutoSaver {
public:
    explicit AutoSaver(double interval) : interval_(interval) {}
    bool tick(double dt) {
        timer_ += dt;
        if (timer_ >= interval_) { timer_ -= interval_; return true; }
        return false;
    }
private:
    double interval_;
    double timer_{0};
};

}  // namespace vg::save
