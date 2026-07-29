// vg/world/BlockTypes.hpp
//
// Data model for a block *type* (as opposed to a placed block instance). A
// BlockType is a plain description — its hardness, which tool mines it, how it
// interacts with light, whether it is a fluid or falls under gravity, and how
// it should be rendered. Behaviour that depends on these values (mining time,
// light propagation, collision) lives in systems that *read* BlockType, so the
// type itself stays a simple, single-responsibility value object.
#pragma once

#include <cstdint>
#include <string>

namespace vg::world {

using BlockId = uint16_t;

// Which tool class mines a block fastest / is required to harvest its drops.
enum class ToolType : uint8_t { None, Pickaxe, Axe, Shovel };

// Material tiers, shared by blocks (as a requirement) and tools (as a level).
enum class Tier : uint8_t { Hand = 0, Wood = 1, Stone = 2, Iron = 3, Diamond = 4 };

// How the mesher/renderer should treat a block's faces.
enum class RenderLayer : uint8_t {
    Opaque,       // stone, dirt — fully occludes neighbours
    Cutout,       // leaves, glass — has holes but no blending
    Transparent   // water — alpha-blended, sorted
};

struct BlockType {
    BlockId     id{0};
    std::string name{"air"};

    // Mining. `hardness` < 0 means unbreakable. Otherwise it is the base time
    // basis in seconds; the mining system scales it by tool correctness/tier.
    float    hardness{0.0f};
    ToolType tool{ToolType::None};   // preferred tool (fastest); None = no preference
    Tier     harvestTier{Tier::Hand};// minimum tool tier to get drops (if requiresTool)
    bool     requiresTool{false};    // if true, only the correct tool yields drops
                                     // (stone, ores). Dirt/wood drop by hand too.

    // Lighting. `lightOpacity` is how much skylight/blocklight a block absorbs
    // (0 = fully transparent, 15 = fully opaque). `lightEmission` is how much
    // light it gives off (0..15), e.g. lava or a torch.
    uint8_t lightOpacity{15};
    uint8_t lightEmission{0};

    // Physics / interaction flags.
    bool solid{true};        // participates in collision
    bool fluid{false};       // flows; non-solid; special rendering
    bool gravity{false};     // falls when unsupported (sand, gravel)
    bool replaceable{false}; // can be overwritten when placing (air, water, grass)

    RenderLayer layer{RenderLayer::Opaque};

    bool isAir() const { return id == 0; }
    bool opaque() const { return lightOpacity >= 15 && layer == RenderLayer::Opaque; }
};

// A tool the player is mining with.
struct Tool {
    ToolType type{ToolType::None};
    Tier     tier{Tier::Hand};
};

// Result of evaluating how a tool mines a block.
struct MiningResult {
    float seconds{0.0f};      // time to break (INFINITY if unbreakable)
    bool  harvestable{true};  // whether the block yields its normal drop
};

// Relative mining speed of a tool tier (Minecraft-like progression).
inline float toolSpeed(Tier t) {
    switch (t) {
        case Tier::Hand:    return 1.0f;
        case Tier::Wood:    return 2.0f;
        case Tier::Stone:   return 4.0f;
        case Tier::Iron:    return 6.0f;
        case Tier::Diamond: return 8.0f;
    }
    return 1.0f;
}

// Compute break time and whether the block will drop, given the tool used.
//  - Unbreakable blocks (hardness < 0) never break.
//  - Using the correct tool of a high-enough tier is fast and yields drops.
//  - Wrong tool / too-low tier is slow and (for tool-required blocks) dropless.
inline MiningResult computeMining(const BlockType& b, Tool tool) {
    MiningResult r;
    if (b.hardness < 0.0f) { r.seconds = 1e30f; r.harvestable = false; return r; }
    if (b.hardness == 0.0f) { r.seconds = 0.0f; r.harvestable = true; return r; }

    bool correctTool = (b.tool == ToolType::None) || (tool.type == b.tool);
    bool tierOk      = static_cast<int>(tool.tier) >= static_cast<int>(b.harvestTier);
    // Blocks that don't require a tool always drop; otherwise the right tool of
    // a sufficient tier is needed.
    bool canHarvest  = b.requiresTool ? (correctTool && tierOk) : true;

    // Speed bonus only applies when using the block's preferred tool.
    float speed  = correctTool ? toolSpeed(tool.tier) : 1.0f;
    float factor = canHarvest ? 1.5f : 5.0f;   // slower when it won't drop
    r.seconds = b.hardness * factor / speed;
    r.harvestable = canHarvest;
    return r;
}

}  // namespace vg::world
