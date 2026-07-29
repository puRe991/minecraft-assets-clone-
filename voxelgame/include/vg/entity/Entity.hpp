// vg/entity/Entity.hpp
//
// Entity model: an EntityType describes a kind of creature (category, health,
// speed) and an Entity is a live instance with position, velocity, health and
// an AI state. Behaviour (AI, pathfinding, spawning) lives in separate systems
// that read these values.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vg/math/Vec3.hpp"

namespace vg::entity {

using math::Vec3d;
using EntityTypeId = uint16_t;

// Passive flee, neutral fight back when provoked, hostile hunt the player.
enum class Category : uint8_t { Passive, Neutral, Hostile };

// High-level AI intent decided each tick.
enum class AiState : uint8_t { Idle, Wander, Flee, Chase };

namespace mobs {
enum : EntityTypeId { None = 0, Pig, Cow, Wolf, Zombie, Skeleton, Count };
}

struct EntityType {
    EntityTypeId id{0};
    std::string  name{"none"};
    Category     category{Category::Passive};
    int          maxHealth{10};
    double        moveSpeed{3.0};
    double        sightRange{16.0};
};

struct Entity {
    EntityTypeId type{mobs::None};
    Vec3d   pos{}, vel{};
    int     health{10};
    bool    alive{true};
    AiState state{AiState::Idle};
    Vec3d   target{};        // current AI target (e.g. player or wander point)
};

class EntityRegistry {
public:
    EntityTypeId add(EntityType t);
    const EntityType& get(EntityTypeId id) const;
    size_t count() const { return types_.size(); }
    Entity spawn(EntityTypeId id, const Vec3d& pos) const;   // health = maxHealth
private:
    std::vector<EntityType> types_;
};

void registerDefaultEntities(EntityRegistry& reg);

}  // namespace vg::entity
