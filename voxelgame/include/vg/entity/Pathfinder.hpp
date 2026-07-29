// vg/entity/Pathfinder.hpp
//
// A* pathfinding for a walking creature on the voxel grid. A cell is a block
// position the creature's feet occupy; it is "standable" when the feet and
// head cells are passable and the block below is solid. Neighbours include
// walking on the level, stepping up one block, and dropping down a few.
//
// Depends only on the World (blocks) and BlockRegistry (solidity) — no
// rendering/entity coupling — so it is deterministic and unit-testable.
#pragma once

#include <vector>

#include "vg/math/Vec3.hpp"
#include "vg/world/BlockRegistry.hpp"
#include "vg/world/World.hpp"

namespace vg::entity {

using math::Vec3i;

class Pathfinder {
public:
    struct Config {
        int maxDrop{3};       // how far a creature will step/drop down
        int maxNodes{20000};  // safety cap on the search
    };

    Pathfinder(const world::World& world, const world::BlockRegistry& reg)
        : world_(world), reg_(reg), cfg_() {}
    Pathfinder(const world::World& world, const world::BlockRegistry& reg, Config cfg)
        : world_(world), reg_(reg), cfg_(cfg) {}

    // Whether the creature can stand with its feet at `p`.
    bool standable(const Vec3i& p) const;

    // Find a path from `start` to `goal` (both standable). Returns the list of
    // cells from start to goal inclusive, or empty if unreachable / invalid.
    std::vector<Vec3i> findPath(const Vec3i& start, const Vec3i& goal) const;

private:
    bool passable(const Vec3i& p) const { return !reg_.get(world_.getBlock(p.x, p.y, p.z)).solid; }
    bool solid(const Vec3i& p) const { return reg_.get(world_.getBlock(p.x, p.y, p.z)).solid; }

    const world::World& world_;
    const world::BlockRegistry& reg_;
    Config cfg_;
};

}  // namespace vg::entity
