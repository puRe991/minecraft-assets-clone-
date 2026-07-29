// Unit tests for A* pathfinding on the voxel grid (Module 8).
#include "TestFramework.hpp"
#include "vg/entity/Pathfinder.hpp"
#include "vg/world/ChunkGenerator.hpp"

using namespace vg::world;
using namespace vg::entity;

// A world with a solid floor at y=floorY and air above.
struct Flat {
    BlockRegistry reg;
    std::shared_ptr<IChunkGenerator> gen;
    World world;
    int floorY;
    Flat(int f = 32) : gen(std::make_shared<FlatChunkGenerator>(f)), world(gen), floorY(f) {
        registerDefaultBlocks(reg);
        world.updateStreaming(0, 0, 2);
    }
    void set(int x, int y, int z, BlockId b) { world.setBlock(x, y, z, b); }
    Vec3i stand(int x, int z) const { return {x, floorY + 1, z}; }   // feet on the floor
};

static bool contiguous(const std::vector<Vec3i>& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        Vec3i d = path[i] - path[i - 1];
        int man = std::abs(d.x) + std::abs(d.y) + std::abs(d.z);
        if (man == 0 || man > 3) return false;    // steps are small
        if (std::abs(d.x) + std::abs(d.z) > 1) return false;  // one horizontal axis at a time
    }
    return true;
}

VG_TEST(path_straight_line) {
    Flat f;
    Pathfinder pf(f.world, f.reg);
    auto path = pf.findPath(f.stand(0, 0), f.stand(0, 6));
    CHECK(!path.empty());
    CHECK(path.front() == f.stand(0, 0));
    CHECK(path.back() == f.stand(0, 6));
    CHECK(contiguous(path));
    CHECK((int)path.size() == 7);              // 6 steps
}

VG_TEST(path_around_wall) {
    Flat f;
    // a wall blocking the straight line from (0,0) to (0,6), with a gap at x=2
    for (int x = -1; x <= 1; ++x)
        for (int y = f.floorY + 1; y <= f.floorY + 2; ++y)
            f.set(x, y, 3, blocks::Stone);
    Pathfinder pf(f.world, f.reg);
    auto path = pf.findPath(f.stand(0, 0), f.stand(0, 6));
    CHECK(!path.empty());
    CHECK(contiguous(path));
    // it had to detour around x=1 (path longer than the 6-step straight shot)
    CHECK((int)path.size() > 7);
    // never walks through the wall cells
    for (auto& p : path) CHECK(!(p.z == 3 && p.x >= -1 && p.x <= 1));
}

VG_TEST(path_steps_up_a_block) {
    Flat f;
    // raise the floor by one from z>=3 (a step up)
    for (int x = -2; x <= 2; ++x)
        for (int z = 3; z <= 6; ++z)
            f.set(x, f.floorY + 1, z, blocks::Stone);
    Pathfinder pf(f.world, f.reg);
    auto start = f.stand(0, 0);
    auto goal = Vec3i{0, f.floorY + 2, 6};      // stand on the raised floor
    CHECK(pf.standable(goal));
    auto path = pf.findPath(start, goal);
    CHECK(!path.empty());
    CHECK(contiguous(path));
    CHECK(path.back() == goal);
}

VG_TEST(no_path_when_enclosed) {
    Flat f;
    // wall the goal in on all four sides (with headroom too)
    int gx = 5, gz = 5;
    for (int y = f.floorY + 1; y <= f.floorY + 2; ++y) {
        f.set(gx + 1, y, gz, blocks::Stone);
        f.set(gx - 1, y, gz, blocks::Stone);
        f.set(gx, y, gz + 1, blocks::Stone);
        f.set(gx, y, gz - 1, blocks::Stone);
    }
    Pathfinder pf(f.world, f.reg);
    auto path = pf.findPath(f.stand(0, 0), f.stand(gx, gz));
    CHECK(path.empty());
}

VG_TEST(headroom_required) {
    Flat f;
    // a 1-high tunnel (ceiling right above the floor) should block a walker
    for (int z = 2; z <= 6; ++z)
        for (int x = -1; x <= 1; ++x)
            f.set(x, f.floorY + 2, z, blocks::Stone);   // ceiling at head height
    Pathfinder pf(f.world, f.reg);
    auto path = pf.findPath(f.stand(0, 0), f.stand(0, 6));
    CHECK(path.empty());                         // can't squeeze through
}
