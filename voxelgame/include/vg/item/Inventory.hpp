// vg/item/Inventory.hpp
//
// A fixed-size collection of item slots with the operations gameplay needs:
// adding items (merging into existing stacks, then empty slots, respecting
// per-item max stack size), removing, counting, and moving/swapping stacks
// between slots. Used for the player inventory, hotbar, and containers.
#pragma once

#include <vector>

#include "vg/item/Item.hpp"

namespace vg::item {

class Inventory {
public:
    Inventory(int slots, const ItemRegistry& reg) : slots_(slots), reg_(reg) {}

    int size() const { return (int)slots_.size(); }
    ItemStack& at(int i) { return slots_[i]; }
    const ItemStack& at(int i) const { return slots_[i]; }

    // Add a stack; merges into matching stacks then fills empties. Returns the
    // number of items that did NOT fit (0 = everything was stored).
    int add(ItemStack stack);

    // Total count of a given item across all slots.
    int count(ItemId item) const;

    // Remove up to `n` of `item`; returns how many were actually removed.
    int remove(ItemId item, int n);

    // Move/merge/swap the stack in slot `from` onto slot `to`.
    void moveTo(int from, int to);

private:
    std::vector<ItemStack> slots_;
    const ItemRegistry& reg_;
};

}  // namespace vg::item
