// vg/item/Furnace.hpp
//
// Smelting: a SmeltingRegistry maps an input item to an output + cook time, a
// FuelRegistry maps items to burn time, and a Furnace is a small state machine
// with input/fuel/output slots that consumes fuel to cook the input over time.
#pragma once

#include <vector>

#include "vg/item/Item.hpp"

namespace vg::item {

struct SmeltRecipe { ItemId input; ItemStack output; double cookTime; };

class SmeltingRegistry {
public:
    void add(ItemId input, ItemStack output, double cookTime) {
        recipes_.push_back({input, output, cookTime});
    }
    const SmeltRecipe* find(ItemId input) const {
        for (const auto& r : recipes_) if (r.input == input) return &r;
        return nullptr;
    }
private:
    std::vector<SmeltRecipe> recipes_;
};

class FuelRegistry {
public:
    void add(ItemId item, double burnTime) { fuels_.push_back({item, burnTime}); }
    double burnTime(ItemId item) const {
        for (const auto& f : fuels_) if (f.item == item) return f.time;
        return 0.0;
    }
private:
    struct Fuel { ItemId item; double time; };
    std::vector<Fuel> fuels_;
};

class Furnace {
public:
    Furnace(const SmeltingRegistry& smelt, const FuelRegistry& fuel, const ItemRegistry& reg)
        : smelt_(smelt), fuel_(fuel), reg_(reg) {}

    ItemStack input, fuelSlot, output;

    // Advance smelting by `dt` seconds.
    void tick(double dt);

    bool isBurning() const { return burnLeft_ > 0; }
    double cookProgress() const { return progress_; }

private:
    bool canSmelt() const;

    const SmeltingRegistry& smelt_;
    const FuelRegistry& fuel_;
    const ItemRegistry& reg_;
    double burnLeft_{0}, burnTotal_{0}, progress_{0};
};

// Sand->glass, cobble->stone, iron ore->ingot; coal/planks/log/stick as fuel.
void registerDefaultSmelting(SmeltingRegistry& smelt, FuelRegistry& fuel, const ItemRegistry& reg);

}  // namespace vg::item
