// vg/item/Inventory.cpp — see Inventory.hpp for the contract.
#include "vg/item/Inventory.hpp"

namespace vg::item {

int Inventory::add(ItemStack stack) {
    if (stack.empty()) return 0;
    int maxStack = reg_.get(stack.item).maxStack;

    // 1) merge into existing non-full stacks of the same item
    for (auto& s : slots_) {
        if (stack.count <= 0) break;
        if (s.item == stack.item && s.count < maxStack) {
            int space = maxStack - s.count;
            int moved = std::min(space, stack.count);
            s.count += moved; stack.count -= moved;
        }
    }
    // 2) fill empty slots
    for (auto& s : slots_) {
        if (stack.count <= 0) break;
        if (s.empty()) {
            s = stack;
            s.count = std::min(maxStack, stack.count);
            stack.count -= s.count;
        }
    }
    return stack.count;   // leftover that didn't fit
}

int Inventory::count(ItemId item) const {
    int n = 0;
    for (const auto& s : slots_) if (s.item == item) n += s.count;
    return n;
}

int Inventory::remove(ItemId item, int n) {
    int removed = 0;
    for (auto& s : slots_) {
        if (removed >= n) break;
        if (s.item == item) {
            int take = std::min(s.count, n - removed);
            s.count -= take; removed += take;
            if (s.count <= 0) s.clear();
        }
    }
    return removed;
}

void Inventory::moveTo(int from, int to) {
    if (from == to) return;
    ItemStack& a = slots_[from];
    ItemStack& b = slots_[to];
    if (a.empty()) return;
    if (b.empty()) { b = a; a.clear(); return; }
    if (b.item == a.item) {                         // merge
        int maxStack = reg_.get(a.item).maxStack;
        int space = maxStack - b.count;
        int moved = std::min(space, a.count);
        b.count += moved; a.count -= moved;
        if (a.count <= 0) a.clear();
        return;
    }
    std::swap(a, b);                                // different items -> swap
}

}  // namespace vg::item
