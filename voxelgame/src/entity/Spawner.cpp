// vg/entity/Spawner.cpp — see Spawner.hpp for the contract.
#include "vg/entity/Spawner.hpp"

namespace vg::entity {

bool Spawner::canSpawnAt(const Vec3i& p, Category cat, int light) const {
    // physical room: feet + head clear, solid ground below
    if (!passable(p) || !passable({p.x, p.y + 1, p.z}) || !solid({p.x, p.y - 1, p.z}))
        return false;
    // light rules
    switch (cat) {
        case Category::Hostile: return light <= 7;    // monsters spawn in the dark
        case Category::Passive: return light >= 9;    // animals in the light
        case Category::Neutral: return true;          // any light
    }
    return false;
}

std::vector<Vec3i> Spawner::findSpawnable(const Vec3i& min, const Vec3i& max,
                                          Category cat, const LightFn& light,
                                          int maxResults) const {
    std::vector<Vec3i> out;
    for (int x = min.x; x <= max.x && (int)out.size() < maxResults; ++x)
        for (int z = min.z; z <= max.z && (int)out.size() < maxResults; ++z)
            for (int y = min.y; y <= max.y && (int)out.size() < maxResults; ++y) {
                Vec3i p{x, y, z};
                if (canSpawnAt(p, cat, light(p))) out.push_back(p);
            }
    return out;
}

}  // namespace vg::entity
