// Unit tests for the player physics / controller (Module 5).
#include "TestFramework.hpp"
#include "vg/physics/PlayerController.hpp"
#include "vg/world/ChunkGenerator.hpp"
#include "vg/world/World.hpp"

#include <memory>

using namespace vg::world;
using namespace vg::physics;

// Build a world with a flat floor at y=ground (grass on top) and a registry.
struct Fixture {
    BlockRegistry reg;
    std::shared_ptr<IChunkGenerator> gen;
    World world;
    Fixture(int ground = 32)
        : gen(std::make_shared<FlatChunkGenerator>(ground)), world(gen) {
        registerDefaultBlocks(reg);
        world.updateStreaming(0, 0, 2);
    }
};

// Simulate `seconds` of updates at 60 Hz.
static void simulate(PlayerController& p, const PlayerInput& in, double seconds) {
    for (int i = 0; i < (int)(seconds * 60); ++i) p.update(in, 1.0 / 60.0);
}

VG_TEST(gravity_settles_on_ground) {
    Fixture f(32);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 50.0, 0.5});        // spawn high
    simulate(p, {}, 3.0);
    // floor top is y=33 (grass at y=32, its top face at 33) -> feet rest at 33
    CHECK(p.onGround());
    CHECK(std::fabs(p.position().y - 33.0) < 0.05);
}

VG_TEST(walk_moves_and_wall_stops) {
    Fixture f(32);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 33.0, 0.5});
    PlayerInput in; in.forward = true; in.yaw = 0;   // +z
    simulate(p, in, 1.0);
    CHECK(p.position().z > 4.0);                       // walked forward
    CHECK(std::fabs(p.position().y - 33.0) < 0.05);    // stayed on the floor

    // build a wall and check we stop against it
    int wz = (int)p.position().z + 1;
    for (int y = 33; y < 36; ++y) f.world.setBlock(0, y, wz, blocks::Stone);
    double before = p.position().z;
    simulate(p, in, 1.0);
    CHECK(p.position().z < wz);                        // did not pass the wall
    CHECK(p.position().z >= before - 0.01);
}

VG_TEST(sprint_faster_than_walk) {
    Fixture f(32);
    PlayerController walk(f.world, f.reg), run(f.world, f.reg);
    walk.setPosition({0.5, 33.0, 0.5});
    run.setPosition({0.5, 33.0, 0.5});
    PlayerInput w; w.forward = true;
    PlayerInput r = w; r.sprint = true;
    simulate(walk, w, 1.0);
    simulate(run, r, 1.0);
    CHECK(run.position().z > walk.position().z + 0.5);
}

VG_TEST(jump_leaves_ground_then_lands) {
    Fixture f(32);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 33.0, 0.5});
    simulate(p, {}, 0.2);                    // settle so onGround is established
    CHECK(p.onGround());
    PlayerInput jump; jump.jump = true;
    p.update(jump, 1.0 / 60.0);
    CHECK(!p.onGround());
    double peak = 0;
    for (int i = 0; i < 120; ++i) { p.update({}, 1.0 / 60.0); peak = std::max(peak, p.position().y); }
    CHECK(peak > 33.5);                    // actually rose
    CHECK(std::fabs(p.position().y - 33.0) < 0.05);   // and landed back
}

VG_TEST(swimming_slows_the_fall) {
    Fixture f(0);                          // no floor near spawn
    // fill a pool of water around the spawn column
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            for (int y = 20; y <= 40; ++y)
                f.world.setBlock(x, y, z, blocks::Water);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 38.0, 0.5});
    CHECK(!p.onGround());
    p.update({}, 1.0 / 60.0);
    CHECK(p.inWater());
    double y0 = p.position().y;
    simulate(p, {}, 1.0);
    // in water the player barely sinks (buoyancy) — far less than free-fall
    CHECK((y0 - p.position().y) < 3.0);
}

VG_TEST(climbing_a_ladder) {
    Fixture f(32);
    // a column of ladders above the floor
    for (int y = 33; y < 40; ++y) f.world.setBlock(0, y, 0, blocks::Ladder);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 33.0, 0.5});       // overlaps the ladder at x/z 0
    PlayerInput up; up.jump = true;
    double y0 = p.position().y;
    simulate(p, up, 1.0);
    CHECK(p.onClimbable());
    CHECK(p.position().y > y0 + 1.0);      // climbed upward
}

VG_TEST(sneak_does_not_walk_off_edge) {
    Fixture f(32);
    // carve away the floor beyond z>=2 so there's a ledge
    for (int x = -2; x <= 2; ++x)
        for (int z = 2; z <= 6; ++z)
            f.world.setBlock(x, 32, z, blocks::Air);
    PlayerController p(f.world, f.reg);
    p.setPosition({0.5, 33.0, 1.2});       // standing near the edge
    simulate(p, {}, 0.2);                   // settle onto the floor
    PlayerInput sneak; sneak.forward = true; sneak.crouch = true; sneak.yaw = 0;
    simulate(p, sneak, 2.0);
    CHECK(p.onGround());                    // never fell off (the key guarantee)
    CHECK(std::fabs(p.position().y - 33.0) < 0.05);
    CHECK(p.position().z < 2.4);            // leaned over the edge but didn't cross the gap
    // sanity: without sneak the player *would* fall into the gap
    PlayerController q(f.world, f.reg);
    q.setPosition({0.5, 33.0, 1.2});
    simulate(q, {}, 0.2);
    PlayerInput walk; walk.forward = true; walk.yaw = 0;
    simulate(q, walk, 2.0);
    CHECK(q.position().y < 33.0);           // fell
}
