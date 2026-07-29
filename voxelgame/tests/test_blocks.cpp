// Unit tests for the block registry and mining rules.
#include "TestFramework.hpp"
#include "vg/world/BlockRegistry.hpp"

#include <cmath>

using namespace vg::world;

static BlockRegistry makeRegistry() {
    BlockRegistry reg;
    registerDefaultBlocks(reg);
    return reg;
}

VG_TEST(registry_registers_all_defaults) {
    auto reg = makeRegistry();
    CHECK(reg.count() == blocks::Count);
    // ids are dense and self-consistent
    for (BlockId id = 0; id < blocks::Count; ++id)
        CHECK(reg.get(id).id == id);
}

VG_TEST(registry_unknown_id_is_air) {
    auto reg = makeRegistry();
    const BlockType& b = reg.get(9999);
    CHECK(b.isAir());
    CHECK(reg.get(blocks::Air).isAir());
}

VG_TEST(block_properties) {
    auto reg = makeRegistry();
    CHECK(reg.get(blocks::Air).solid == false);
    CHECK(reg.get(blocks::Stone).solid == true);
    CHECK(reg.get(blocks::Sand).gravity == true);
    CHECK(reg.get(blocks::Gravel).gravity == true);
    CHECK(reg.get(blocks::Water).fluid == true);
    CHECK(reg.get(blocks::Water).solid == false);
    CHECK(reg.get(blocks::Water).replaceable == true);
    CHECK(reg.get(blocks::Glass).layer == RenderLayer::Cutout);
    CHECK(reg.get(blocks::Stone).opaque() == true);
    CHECK(reg.get(blocks::Glass).opaque() == false);   // transparent-ish
}

VG_TEST(light_properties) {
    auto reg = makeRegistry();
    CHECK(reg.get(blocks::Lava).lightEmission == 15);
    CHECK(reg.get(blocks::Torch).lightEmission == 14);
    CHECK(reg.get(blocks::Stone).lightEmission == 0);
    CHECK(reg.get(blocks::Glass).lightOpacity == 0);    // lets light through
    CHECK(reg.get(blocks::Stone).lightOpacity == 15);   // blocks light
}

VG_TEST(mining_unbreakable) {
    auto reg = makeRegistry();
    auto r = computeMining(reg.get(blocks::Bedrock), {ToolType::Pickaxe, Tier::Diamond});
    CHECK(std::isinf(r.seconds) || r.seconds > 1e29f);
    CHECK(r.harvestable == false);
}

VG_TEST(mining_correct_tool_is_faster) {
    auto reg = makeRegistry();
    const BlockType& stone = reg.get(blocks::Stone);
    auto byHand = computeMining(stone, {ToolType::None, Tier::Hand});
    auto byWood = computeMining(stone, {ToolType::Pickaxe, Tier::Wood});
    auto byDiamond = computeMining(stone, {ToolType::Pickaxe, Tier::Diamond});
    CHECK(byWood.seconds < byHand.seconds);          // right tool beats hand
    CHECK(byDiamond.seconds < byWood.seconds);       // better tier is faster
    CHECK(byHand.harvestable == false);              // stone needs a pickaxe to drop
    CHECK(byWood.harvestable == true);
}

VG_TEST(mining_tier_gating_for_ores) {
    auto reg = makeRegistry();
    // gold requires an iron pickaxe to harvest
    auto stonePick = computeMining(reg.get(blocks::GoldOre), {ToolType::Pickaxe, Tier::Stone});
    auto ironPick  = computeMining(reg.get(blocks::GoldOre), {ToolType::Pickaxe, Tier::Iron});
    CHECK(stonePick.harvestable == false);
    CHECK(ironPick.harvestable == true);
    // coal only needs wood
    CHECK(computeMining(reg.get(blocks::CoalOre), {ToolType::Pickaxe, Tier::Wood}).harvestable);
}

VG_TEST(mining_dirt_by_hand_drops) {
    auto reg = makeRegistry();
    // hand-mineable blocks always drop, just faster with a shovel
    auto hand  = computeMining(reg.get(blocks::Dirt), {ToolType::None, Tier::Hand});
    auto shovel = computeMining(reg.get(blocks::Dirt), {ToolType::Shovel, Tier::Iron});
    CHECK(hand.harvestable == true);
    CHECK(shovel.harvestable == true);
    CHECK(shovel.seconds < hand.seconds);
}
