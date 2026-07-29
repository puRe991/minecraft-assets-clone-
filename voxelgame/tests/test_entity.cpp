// Unit tests for entities, AI decisions, steering and spawning (Module 8).
#include "TestFramework.hpp"
#include "vg/entity/Ai.hpp"
#include "vg/entity/Spawner.hpp"
#include "vg/world/ChunkGenerator.hpp"

using namespace vg::world;
using namespace vg::entity;

VG_TEST(entity_registry_and_spawn) {
    EntityRegistry reg; registerDefaultEntities(reg);
    CHECK(reg.count() == mobs::Count);
    CHECK(reg.get(mobs::Zombie).category == Category::Hostile);
    CHECK(reg.get(mobs::Pig).category == Category::Passive);
    Entity z = reg.spawn(mobs::Zombie, {1, 2, 3});
    CHECK(z.health == reg.get(mobs::Zombie).maxHealth);
    CHECK(z.pos == vg::math::Vec3d(1, 2, 3));
}

VG_TEST(ai_state_decisions) {
    EntityRegistry reg; registerDefaultEntities(reg);
    Entity zombie = reg.spawn(mobs::Zombie, {0, 0, 0});
    Entity pig = reg.spawn(mobs::Pig, {0, 0, 0});
    Entity wolf = reg.spawn(mobs::Wolf, {0, 0, 0});

    Perception near{true, {3, 0, 0}, 3.0, false};
    Perception far{true, {40, 0, 0}, 40.0, false};

    CHECK(decideState(reg.get(mobs::Zombie), zombie, near) == AiState::Chase);
    CHECK(decideState(reg.get(mobs::Zombie), zombie, far) == AiState::Wander);
    CHECK(decideState(reg.get(mobs::Pig), pig, near) == AiState::Flee);
    // wolf (neutral) ignores the player until provoked
    CHECK(decideState(reg.get(mobs::Wolf), wolf, near) == AiState::Wander);
    wolf.health = 3;   // took damage
    CHECK(decideState(reg.get(mobs::Wolf), wolf, near) == AiState::Chase);
}

VG_TEST(steering_moves_toward_and_away) {
    EntityRegistry reg; registerDefaultEntities(reg);
    Entity e = reg.spawn(mobs::Zombie, {0, 10, 0});
    steerToward(e, {10, 10, 0}, 2.0);
    CHECK(e.vel.x > 0);                     // moves +x toward target
    CHECK(std::fabs(e.vel.z) < 1e-9);
    steerAway(e, {10, 10, 0}, 2.0);
    CHECK(e.vel.x < 0);                     // flees -x
}

VG_TEST(spawn_rules_light_and_room) {
    BlockRegistry breg; registerDefaultBlocks(breg);
    auto gen = std::make_shared<FlatChunkGenerator>(32);
    World world(gen); world.updateStreaming(0, 0, 1);
    Spawner sp(world, breg);

    Vec3i feet{0, 33, 0};                    // on the grass, head clear
    CHECK(sp.canSpawnAt(feet, Category::Hostile, 3));   // dark -> monster ok
    CHECK(!sp.canSpawnAt(feet, Category::Hostile, 12)); // lit -> no monster
    CHECK(sp.canSpawnAt(feet, Category::Passive, 12));  // lit -> animal ok
    CHECK(!sp.canSpawnAt(feet, Category::Passive, 3));  // dark -> no animal

    // no headroom -> never spawnable
    world.setBlock(0, 34, 0, blocks::Stone);
    CHECK(!sp.canSpawnAt(feet, Category::Neutral, 15));
}

VG_TEST(find_spawnable_region) {
    BlockRegistry breg; registerDefaultBlocks(breg);
    auto gen = std::make_shared<FlatChunkGenerator>(32);
    World world(gen); world.updateStreaming(0, 0, 1);
    Spawner sp(world, breg);

    auto darkEverywhere = [](const Vec3i&) { return 0; };
    auto spots = sp.findSpawnable({0, 33, 0}, {3, 33, 3}, Category::Hostile, darkEverywhere, 100);
    CHECK(spots.size() == 16);              // 4x4 grid of valid ground cells
    for (auto& p : spots) CHECK(p.y == 33);
}
