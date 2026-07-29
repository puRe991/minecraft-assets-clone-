// vg/physics/PlayerController.cpp — see PlayerController.hpp for the contract.
#include "vg/physics/PlayerController.hpp"

#include <algorithm>
#include <cmath>

namespace vg::physics {

// True if the box overlaps any solid block cell.
bool PlayerController::collides(const AABB& b) const {
    const double eps = 1e-6;
    int x0 = (int)std::floor(b.min.x + eps), x1 = (int)std::floor(b.max.x - eps);
    int y0 = (int)std::floor(b.min.y + eps), y1 = (int)std::floor(b.max.y - eps);
    int z0 = (int)std::floor(b.min.z + eps), z1 = (int)std::floor(b.max.z - eps);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (isSolidCell(x, y, z)) return true;
    return false;
}

// Detect whether the player's box is in fluid and/or overlapping a climbable.
void PlayerController::scanEnvironment(double height) {
    AABB b = box(pos_, height);
    int x0 = (int)std::floor(b.min.x), x1 = (int)std::floor(b.max.x - 1e-6);
    int y0 = (int)std::floor(b.min.y), y1 = (int)std::floor(b.max.y - 1e-6);
    int z0 = (int)std::floor(b.min.z), z1 = (int)std::floor(b.max.z - 1e-6);
    inWater_ = onClimbable_ = false;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const auto& bt = reg_.get(world_.getBlock(x, y, z));
                if (bt.fluid) inWater_ = true;
                if (bt.climbable) onClimbable_ = true;
            }
}

// Move along one axis (0=x,1=y,2=z) with collide-and-stop resolution.
// `preventLedge` (sneaking on the ground) cancels a horizontal step that would
// leave the player standing over empty space.
void PlayerController::moveAxis(int axis, double amount, bool preventLedge) {
    if (amount == 0) return;
    Vec3d next = pos_;
    (&next.x)[axis] += amount;

    if (collides(box(next, curHeight_))) {
        if (axis == 1) { if (amount < 0) onGround_ = true; vel_.y = 0; }
        else (&vel_.x)[axis] = 0;
        return;
    }

    if (preventLedge && axis != 1) {
        // Is there still solid ground within a thin slab just below the feet?
        AABB b = box(next, curHeight_);
        AABB below{{b.min.x, next.y - 0.06, b.min.z}, {b.max.x, next.y, b.max.z}};
        if (!collides(below)) { (&vel_.x)[axis] = 0; return; }  // don't walk off
    }
    pos_ = next;
}

void PlayerController::update(const PlayerInput& in, double dt) {
    curHeight_ = in.crouch ? cfg_.crouchHeight : cfg_.height;
    curEye_    = in.crouch ? cfg_.eyeHeight - (cfg_.height - cfg_.crouchHeight) : cfg_.eyeHeight;

    scanEnvironment(curHeight_);

    // --- desired horizontal velocity from input, relative to yaw ---
    double s = std::sin(in.yaw), c = std::cos(in.yaw);
    double fx = s, fz = c;      // forward
    double rx = c, rz = -s;     // right
    double wishX = 0, wishZ = 0;
    if (in.forward) { wishX += fx; wishZ += fz; }
    if (in.back)    { wishX -= fx; wishZ -= fz; }
    if (in.right)   { wishX += rx; wishZ += rz; }
    if (in.left)    { wishX -= rx; wishZ -= rz; }
    double wl = std::sqrt(wishX * wishX + wishZ * wishZ);
    if (wl > 0) { wishX /= wl; wishZ /= wl; }

    double speed = inWater_ ? cfg_.swimSpeed
                 : in.crouch ? cfg_.crouchSpeed
                 : in.sprint ? cfg_.sprintSpeed
                 : cfg_.walkSpeed;
    vel_.x = wishX * speed;
    vel_.z = wishZ * speed;

    // --- vertical motion ---
    if (onClimbable_) {
        // Cling to the ladder: press up/jump to ascend, crouch to descend.
        if (in.jump || in.forward) vel_.y = cfg_.climbSpeed;
        else if (in.crouch)        vel_.y = -cfg_.climbSpeed;
        else                       vel_.y = -0.6;   // slow slide
    } else if (inWater_) {
        vel_.y -= cfg_.gravity * 0.30 * dt;         // buoyant: weak gravity
        vel_.y += 1.5 * dt;                          // slight upward buoyancy
        if (in.jump) vel_.y = cfg_.swimSpeed;        // swim up
        vel_.y = std::clamp(vel_.y, -cfg_.swimSpeed, cfg_.swimSpeed);
    } else {
        vel_.y -= cfg_.gravity * dt;                 // gravity
        if (vel_.y < -cfg_.terminalVelocity) vel_.y = -cfg_.terminalVelocity;
        if (in.jump && onGround_) vel_.y = cfg_.jumpSpeed;
    }

    bool wasOnGround = onGround_;
    onGround_ = false;

    // Sneak = crouching while supported: horizontal steps must keep ground below.
    bool sneak = in.crouch && wasOnGround && !inWater_ && !onClimbable_;

    // --- integrate with per-axis collision (x, z, then y) ---
    moveAxis(0, vel_.x * dt, sneak);
    moveAxis(2, vel_.z * dt, sneak);
    moveAxis(1, vel_.y * dt, false);
}

}  // namespace vg::physics
