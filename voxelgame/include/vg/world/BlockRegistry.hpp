// vg/world/BlockRegistry.hpp
//
// Single source of truth for the set of block types in a world. Systems look
// up immutable BlockType definitions by id; they never hard-code block data.
// The registry is data-driven and extensible: new blocks (or a whole mod's
// worth) are added by registering more BlockTypes — no existing code changes
// (Open/Closed).
#pragma once

#include <vector>

#include "vg/world/BlockTypes.hpp"

namespace vg::world {

// Well-known ids for the built-in blocks (see registerDefaultBlocks).
namespace blocks {
enum : BlockId {
    Air = 0, Stone, Dirt, Grass, Sand, Gravel, Bedrock,
    OakLog, OakPlanks, OakLeaves, Glass,
    Water, Lava,
    CoalOre, IronOre, GoldOre, DiamondOre,
    Torch,
    Count
};
}  // namespace blocks

class BlockRegistry {
public:
    // Register a block type. Its id must equal the next free slot (ids are
    // dense and assigned in registration order) — this keeps id->type lookup a
    // direct array index. Returns the assigned id.
    BlockId add(BlockType type);

    // Look up a block type by id. Ids are validated; an unknown id returns the
    // air definition rather than throwing, so generation/render code is total.
    const BlockType& get(BlockId id) const;

    size_t count() const { return types_.size(); }

private:
    std::vector<BlockType> types_;
};

// Populate a registry with the standard block set. Kept as a free function so
// tests can build a fresh registry and games can extend it afterwards.
void registerDefaultBlocks(BlockRegistry& reg);

}  // namespace vg::world
