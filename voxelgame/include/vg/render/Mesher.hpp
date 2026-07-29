// vg/render/Mesher.hpp
//
// Greedy meshing: turn a chunk's blocks into a compact set of rectangular
// quads instead of one quad per block face. Only *exposed* faces are emitted
// (a face is skipped when the neighbouring block fully occludes it), and
// coplanar faces of the same block are merged into the largest rectangles
// possible. This is the standard performance foundation for a voxel renderer.
//
// The mesher reads block occlusion from the BlockRegistry (DIP) and treats
// anything outside the chunk as air (a neighbour-aware version can be layered
// on top later without changing the callers).
#pragma once

#include <cstdint>
#include <vector>

#include "vg/world/BlockRegistry.hpp"
#include "vg/world/Chunk.hpp"

namespace vg::render {

// One merged rectangle. (x,y,z) is the min corner of the face; the rectangle
// spans `w` x `h` along the two axes of its plane. `face` is 0..5 for
// +X,-X,+Y,-Y,+Z,-Z. `block` is the block whose face this is.
struct Quad {
    int x, y, z;
    int w, h;
    int face;
    world::BlockId block;
};

using Mesh = std::vector<Quad>;

class Mesher {
public:
    explicit Mesher(const world::BlockRegistry& reg) : reg_(reg) {}
    Mesh build(const world::Chunk& chunk) const;

private:
    const world::BlockRegistry& reg_;
};

}  // namespace vg::render
