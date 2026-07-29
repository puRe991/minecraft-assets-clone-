// vg/world/BlockRegistry.cpp — see BlockRegistry.hpp for the contract.
#include "vg/world/BlockRegistry.hpp"

#include <cassert>

namespace vg::world {

BlockId BlockRegistry::add(BlockType type) {
    // Ids are dense and equal to the vector index. Enforce that callers assign
    // them in order so lookup stays an O(1) array index.
    BlockId expected = static_cast<BlockId>(types_.size());
    assert(type.id == expected && "block ids must be dense and in order");
    (void)expected;
    types_.push_back(std::move(type));
    return types_.back().id;
}

const BlockType& BlockRegistry::get(BlockId id) const {
    static const BlockType kAir{};  // id 0, air defaults
    if (id >= types_.size()) return kAir;
    return types_[id];
}

void registerDefaultBlocks(BlockRegistry& reg) {
    using namespace blocks;

    // Helper to make definitions read like a table.
    auto B = [](BlockId id, const char* name) {
        BlockType t; t.id = id; t.name = name; return t;
    };

    // --- air ---
    { BlockType t = B(Air, "air");
      t.hardness = 0; t.solid = false; t.lightOpacity = 0; t.replaceable = true;
      t.layer = RenderLayer::Transparent; reg.add(t); }

    // --- terrain ---
    { BlockType t = B(Stone, "stone");
      t.hardness = 1.5f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Wood;
      t.requiresTool = true; reg.add(t); }
    { BlockType t = B(Dirt, "dirt");
      t.hardness = 0.5f; t.tool = ToolType::Shovel; reg.add(t); }
    { BlockType t = B(Grass, "grass_block");
      t.hardness = 0.6f; t.tool = ToolType::Shovel; reg.add(t); }
    { BlockType t = B(Sand, "sand");
      t.hardness = 0.5f; t.tool = ToolType::Shovel; t.gravity = true; reg.add(t); }
    { BlockType t = B(Gravel, "gravel");
      t.hardness = 0.6f; t.tool = ToolType::Shovel; t.gravity = true; reg.add(t); }
    { BlockType t = B(Bedrock, "bedrock");
      t.hardness = -1.0f; reg.add(t); }                       // unbreakable

    // --- wood ---
    { BlockType t = B(OakLog, "oak_log");
      t.hardness = 2.0f; t.tool = ToolType::Axe; reg.add(t); }
    { BlockType t = B(OakPlanks, "oak_planks");
      t.hardness = 2.0f; t.tool = ToolType::Axe; reg.add(t); }
    { BlockType t = B(OakLeaves, "oak_leaves");
      t.hardness = 0.2f; t.lightOpacity = 1; t.layer = RenderLayer::Cutout; reg.add(t); }
    { BlockType t = B(Glass, "glass");
      t.hardness = 0.3f; t.lightOpacity = 0; t.layer = RenderLayer::Cutout; reg.add(t); }

    // --- fluids ---
    { BlockType t = B(Water, "water");
      t.hardness = 100.0f; t.solid = false; t.fluid = true; t.replaceable = true;
      t.lightOpacity = 2; t.layer = RenderLayer::Transparent; reg.add(t); }
    { BlockType t = B(Lava, "lava");
      t.hardness = 100.0f; t.solid = false; t.fluid = true; t.replaceable = true;
      t.lightOpacity = 0; t.lightEmission = 15; t.layer = RenderLayer::Transparent; reg.add(t); }

    // --- ores (require progressively better pickaxes) ---
    { BlockType t = B(CoalOre, "coal_ore");
      t.hardness = 3.0f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Wood;
      t.requiresTool = true; reg.add(t); }
    { BlockType t = B(IronOre, "iron_ore");
      t.hardness = 3.0f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Stone;
      t.requiresTool = true; reg.add(t); }
    { BlockType t = B(GoldOre, "gold_ore");
      t.hardness = 3.0f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Iron;
      t.requiresTool = true; reg.add(t); }
    { BlockType t = B(DiamondOre, "diamond_ore");
      t.hardness = 3.0f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Iron;
      t.requiresTool = true; reg.add(t); }

    // --- light source ---
    { BlockType t = B(Torch, "torch");
      t.hardness = 0.0f; t.solid = false; t.lightOpacity = 0; t.lightEmission = 14;
      t.layer = RenderLayer::Cutout; reg.add(t); }

    // --- climbable ---
    { BlockType t = B(Ladder, "ladder");
      t.hardness = 0.4f; t.tool = ToolType::Axe; t.solid = false; t.lightOpacity = 0;
      t.climbable = true; t.layer = RenderLayer::Cutout; reg.add(t); }

    // --- cobblestone (what stone drops as; smelts back into stone) ---
    { BlockType t = B(Cobblestone, "cobblestone");
      t.hardness = 2.0f; t.tool = ToolType::Pickaxe; t.harvestTier = Tier::Wood;
      t.requiresTool = true; reg.add(t); }
}

}  // namespace vg::world
