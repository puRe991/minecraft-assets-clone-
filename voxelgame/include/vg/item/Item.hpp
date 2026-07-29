// vg/item/Item.hpp
//
// Item model: an ItemType describes a kind of item (stack size, tool class,
// durability, and whether it places a block), and an ItemStack is a runtime
// stack of a given item. Items are a separate id space from blocks; a block
// item references the block it places via `placesBlock`.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vg/world/BlockTypes.hpp"   // world::BlockId + block ids

namespace vg::item {

using ItemId = uint16_t;

enum class ToolClass : uint8_t { None, Pickaxe, Axe, Shovel, Sword };

// Well-known item ids (registerDefaultItems assigns them in this order).
namespace items {
enum : ItemId {
    None = 0,
    Dirt, Stone, Cobblestone, Sand, Glass, Log, Planks, IronOreItem,
    Stick, Coal, IronIngot, GoldIngot, Diamond,
    WoodPickaxe, StonePickaxe, IronPickaxe, WoodAxe, StoneAxe,
    Count
};
}  // namespace items

struct ItemType {
    ItemId      id{0};
    std::string name{"none"};
    int         maxStack{64};
    int         maxDurability{0};                 // 0 = not damageable
    ToolClass   toolClass{ToolClass::None};
    int         toolTier{0};
    world::BlockId placesBlock{world::blocks::Air};  // Air (0) = not a block
};

struct ItemStack {
    ItemId item{items::None};
    int    count{0};
    int    durability{0};   // remaining, for damageable items

    bool empty() const { return count <= 0 || item == items::None; }
    void clear() { item = items::None; count = 0; durability = 0; }
};

class ItemRegistry {
public:
    ItemId add(ItemType t);                      // dense ids, in registration order
    const ItemType& get(ItemId id) const;        // unknown -> the None type
    size_t count() const { return types_.size(); }

    // Make a fresh stack of `n`, initialising durability for tools.
    ItemStack make(ItemId id, int n = 1) const;

private:
    std::vector<ItemType> types_;
};

void registerDefaultItems(ItemRegistry& reg);

// Apply `amount` of wear to a damageable stack. Returns true if it broke
// (count is then decremented and durability reset for the next one).
bool damageItem(ItemStack& stack, const ItemRegistry& reg, int amount = 1);

}  // namespace vg::item
