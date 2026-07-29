// vg/item/Furnace.cpp — see Furnace.hpp for the contract.
#include "vg/item/Furnace.hpp"

#include <algorithm>

namespace vg::item {

bool Furnace::canSmelt() const {
    if (input.empty()) return false;
    const SmeltRecipe* r = smelt_.find(input.item);
    if (!r) return false;
    if (output.empty()) return true;                 // empty output accepts anything
    if (output.item != r->output.item) return false; // wrong result already there
    int maxStack = reg_.get(output.item).maxStack;
    return output.count + r->output.count <= maxStack;
}

void Furnace::tick(double dt) {
    // (Re)light the furnace if we have work to do and fuel available.
    if (burnLeft_ <= 0 && canSmelt() && !fuelSlot.empty()) {
        double bt = fuel_.burnTime(fuelSlot.item);
        if (bt > 0) {
            burnTotal_ = burnLeft_ = bt;
            if (--fuelSlot.count <= 0) fuelSlot.clear();
        }
    }

    if (burnLeft_ > 0) {
        burnLeft_ = std::max(0.0, burnLeft_ - dt);
        if (canSmelt()) {
            const SmeltRecipe* r = smelt_.find(input.item);
            progress_ += dt;
            if (progress_ >= r->cookTime) {
                progress_ -= r->cookTime;
                // produce output
                if (output.empty()) output = r->output;
                else output.count += r->output.count;
                // consume one input
                if (--input.count <= 0) input.clear();
            }
        } else {
            progress_ = 0;
        }
    } else {
        progress_ = 0;   // fire went out: reset progress (Minecraft-like)
    }
}

void registerDefaultSmelting(SmeltingRegistry& s, FuelRegistry& f, const ItemRegistry& reg) {
    using namespace items;
    s.add(Sand, reg.make(Glass, 1), 10.0);
    s.add(Cobblestone, reg.make(Stone, 1), 10.0);
    s.add(IronOreItem, reg.make(IronIngot, 1), 10.0);

    f.add(Coal, 80.0);     // smelts 8 items
    f.add(Log, 15.0);
    f.add(Planks, 15.0);
    f.add(Stick, 5.0);
}

}  // namespace vg::item
