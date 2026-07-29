// vg/entity/Entity.cpp — see Entity.hpp for the contract.
#include "vg/entity/Entity.hpp"

#include <cassert>

namespace vg::entity {

EntityTypeId EntityRegistry::add(EntityType t) {
    assert(t.id == (EntityTypeId)types_.size() && "entity ids must be dense/in order");
    types_.push_back(std::move(t));
    return types_.back().id;
}

const EntityType& EntityRegistry::get(EntityTypeId id) const {
    static const EntityType kNone{};
    return id < types_.size() ? types_[id] : kNone;
}

Entity EntityRegistry::spawn(EntityTypeId id, const Vec3d& pos) const {
    Entity e;
    e.type = id;
    e.pos = pos;
    e.health = get(id).maxHealth;
    e.state = AiState::Wander;
    return e;
}

void registerDefaultEntities(EntityRegistry& reg) {
    auto E = [](EntityTypeId id, const char* n, Category c, int hp, double spd) {
        EntityType t; t.id = id; t.name = n; t.category = c; t.maxHealth = hp; t.moveSpeed = spd;
        return t;
    };
    reg.add(E(mobs::None, "none", Category::Passive, 0, 0));
    reg.add(E(mobs::Pig, "pig", Category::Passive, 10, 2.5));
    reg.add(E(mobs::Cow, "cow", Category::Passive, 10, 2.5));
    reg.add(E(mobs::Wolf, "wolf", Category::Neutral, 8, 4.0));
    reg.add(E(mobs::Zombie, "zombie", Category::Hostile, 20, 2.3));
    reg.add(E(mobs::Skeleton, "skeleton", Category::Hostile, 16, 2.5));
}

}  // namespace vg::entity
