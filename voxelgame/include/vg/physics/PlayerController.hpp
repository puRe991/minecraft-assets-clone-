// vg/physics/PlayerController.hpp
//
// First-person player movement + collision against the voxel world. Handles
// walking, sprinting, crouching (with edge protection), jumping, swimming in
// fluids, and climbing climbable blocks. Depends only on the World for block
// data and the BlockRegistry for block properties (no rendering/input coupling).
#pragma once

#include "vg/math/Vec3.hpp"
#include "vg/physics/AABB.hpp"
#include "vg/world/BlockRegistry.hpp"
#include "vg/world/World.hpp"

namespace vg::physics {

using math::Vec3d;

// Per-frame intent, produced by the input layer (Module: controls/UI later).
struct PlayerInput {
    bool forward{false}, back{false}, left{false}, right{false};
    bool jump{false}, crouch{false}, sprint{false};
    double yaw{0.0};   // facing, radians (0 = +Z), for movement direction
};

struct PhysicsConfig {
    double gravity{28.0};
    double walkSpeed{4.3};
    double sprintSpeed{5.6};
    double crouchSpeed{1.8};
    double swimSpeed{3.0};
    double jumpSpeed{8.4};
    double climbSpeed{2.5};
    double width{0.6};
    double height{1.8};
    double crouchHeight{1.5};
    double eyeHeight{1.62};
    double terminalVelocity{40.0};
};

class PlayerController {
public:
    PlayerController(const world::World& world, const world::BlockRegistry& reg,
                     PhysicsConfig cfg = {})
        : world_(world), reg_(reg), cfg_(cfg) {}

    void setPosition(const Vec3d& feet) { pos_ = feet; vel_ = {}; }
    const Vec3d& position() const { return pos_; }
    Vec3d eye() const { return {pos_.x, pos_.y + curEye_, pos_.z}; }
    const Vec3d& velocity() const { return vel_; }

    bool onGround() const { return onGround_; }
    bool inWater() const { return inWater_; }
    bool onClimbable() const { return onClimbable_; }

    // Advance one step. `dt` is seconds.
    void update(const PlayerInput& in, double dt);

private:
    bool isSolidCell(int x, int y, int z) const {
        return reg_.get(world_.getBlock(x, y, z)).solid;
    }
    bool collides(const AABB& box) const;
    AABB box(const Vec3d& feet, double height) const { return AABB::fromFeet(feet, cfg_.width, height); }
    void scanEnvironment(double height);                    // sets inWater_/onClimbable_
    void moveAxis(int axis, double amount, bool preventLedge); // collide-and-resolve one axis

    const world::World& world_;
    const world::BlockRegistry& reg_;
    PhysicsConfig cfg_;

    Vec3d pos_{};   // feet
    Vec3d vel_{};
    bool onGround_{false}, inWater_{false}, onClimbable_{false};
    double curHeight_{1.8}, curEye_{1.62};
};

}  // namespace vg::physics
