// vg/entity/Ai.hpp
//
// Minimal behaviour layer: decide an AI state from what the creature perceives,
// and steer its velocity toward or away from a point. Deliberately small and
// pure so it is trivially testable; a fuller behaviour tree can build on it.
#pragma once

#include <cmath>

#include "vg/entity/Entity.hpp"

namespace vg::entity {

struct Perception {
    bool   playerVisible{false};
    Vec3d  playerPos{};
    double distance{1e9};
    bool   isDay{true};
};

// Choose an AI state for this tick based on category and perception.
inline AiState decideState(const EntityType& t, const Entity& e, const Perception& p) {
    switch (t.category) {
        case Category::Passive:
            return (p.playerVisible && p.distance < 6.0) ? AiState::Flee : AiState::Wander;
        case Category::Neutral:
            // fight back only once provoked (took damage)
            return (e.health < t.maxHealth && p.playerVisible && p.distance < t.sightRange)
                       ? AiState::Chase : AiState::Wander;
        case Category::Hostile:
            return (p.playerVisible && p.distance < t.sightRange) ? AiState::Chase : AiState::Wander;
    }
    return AiState::Idle;
}

// Set horizontal velocity toward `tgt` at `speed` (y velocity left to physics).
inline void steerToward(Entity& e, const Vec3d& tgt, double speed) {
    double dx = tgt.x - e.pos.x, dz = tgt.z - e.pos.z;
    double len = std::sqrt(dx * dx + dz * dz);
    if (len > 1e-6) { e.vel.x = dx / len * speed; e.vel.z = dz / len * speed; }
    else { e.vel.x = e.vel.z = 0; }
}

inline void steerAway(Entity& e, const Vec3d& threat, double speed) {
    steerToward(e, {2 * e.pos.x - threat.x, e.pos.y, 2 * e.pos.z - threat.z}, speed);
}

}  // namespace vg::entity
