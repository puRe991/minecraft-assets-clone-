// vg/entity/Spawner.hpp
//
// Mob spawning rules: a creature category can spawn on a block position when
// the feet/head are clear, there is solid ground below, and the light level
// suits the category (hostiles in the dark, passives in the light). The
// spawner scans a region for valid positions; the caller decides which to use.
#pragma once

#include <functional>
#include <vector>

#include "vg/entity/Entity.hpp"
#include "vg/math/Vec3.hpp"
#include "vg/world/BlockRegistry.hpp"
#include "vg/world/World.hpp"

namespace vg::entity {

using math::Vec3i;

// Provides the light level (0..15) at a world block position.
using LightFn = std::function<int(const Vec3i&)>;

class Spawner {
public:
    Spawner(const world::World& world, const world::BlockRegistry& reg)
        : world_(world), reg_(reg) {}

    // Can a creature of `cat` spawn with its feet at `p` given `light`?
    bool canSpawnAt(const Vec3i& p, Category cat, int light) const;

    // All positions in [min,max] where `cat` may spawn (up to `maxResults`).
    std::vector<Vec3i> findSpawnable(const Vec3i& min, const Vec3i& max,
                                     Category cat, const LightFn& light,
                                     int maxResults = 64) const;

private:
    bool passable(const Vec3i& p) const { return !reg_.get(world_.getBlock(p.x, p.y, p.z)).solid; }
    bool solid(const Vec3i& p) const { return reg_.get(world_.getBlock(p.x, p.y, p.z)).solid; }

    const world::World& world_;
    const world::BlockRegistry& reg_;
};

}  // namespace vg::entity
