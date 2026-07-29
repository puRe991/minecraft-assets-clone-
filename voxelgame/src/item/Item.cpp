// vg/item/Item.cpp — see Item.hpp for the contract.
#include "vg/item/Item.hpp"

#include <cassert>

namespace vg::item {

ItemId ItemRegistry::add(ItemType t) {
    assert(t.id == (ItemId)types_.size() && "item ids must be dense and in order");
    types_.push_back(std::move(t));
    return types_.back().id;
}

const ItemType& ItemRegistry::get(ItemId id) const {
    static const ItemType kNone{};
    return id < types_.size() ? types_[id] : kNone;
}

ItemStack ItemRegistry::make(ItemId id, int n) const {
    ItemStack s;
    s.item = id;
    s.count = n;
    s.durability = get(id).maxDurability;   // 0 for non-tools
    return s;
}

bool damageItem(ItemStack& s, const ItemRegistry& reg, int amount) {
    const ItemType& t = reg.get(s.item);
    if (t.maxDurability <= 0 || s.empty()) return false;   // indestructible
    s.durability -= amount;
    if (s.durability <= 0) {
        if (--s.count <= 0) { s.clear(); }
        else s.durability = t.maxDurability;                // next tool in the stack
        return true;
    }
    return false;
}

void registerDefaultItems(ItemRegistry& reg) {
    using namespace world;
    auto I = [](ItemId id, const char* name) { ItemType t; t.id = id; t.name = name; return t; };
    auto block = [&](ItemId id, const char* name, BlockId b) {
        ItemType t = I(id, name); t.placesBlock = b; return t;
    };
    auto tool = [&](ItemId id, const char* name, ToolClass c, int tier, int dur) {
        ItemType t = I(id, name); t.toolClass = c; t.toolTier = tier;
        t.maxDurability = dur; t.maxStack = 1; return t;
    };

    reg.add(I(items::None, "none"));

    // block items
    reg.add(block(items::Dirt, "dirt", blocks::Dirt));
    reg.add(block(items::Stone, "stone", blocks::Stone));
    reg.add(block(items::Cobblestone, "cobblestone", blocks::Cobblestone));
    reg.add(block(items::Sand, "sand", blocks::Sand));
    reg.add(block(items::Glass, "glass", blocks::Glass));
    reg.add(block(items::Log, "oak_log", blocks::OakLog));
    reg.add(block(items::Planks, "oak_planks", blocks::OakPlanks));
    reg.add(block(items::IronOreItem, "iron_ore", blocks::IronOre));

    // materials
    reg.add(I(items::Stick, "stick"));
    reg.add(I(items::Coal, "coal"));
    reg.add(I(items::IronIngot, "iron_ingot"));
    reg.add(I(items::GoldIngot, "gold_ingot"));
    reg.add(I(items::Diamond, "diamond"));

    // tools (durability roughly Minecraft-like)
    reg.add(tool(items::WoodPickaxe, "wooden_pickaxe", ToolClass::Pickaxe, 1, 59));
    reg.add(tool(items::StonePickaxe, "stone_pickaxe", ToolClass::Pickaxe, 2, 131));
    reg.add(tool(items::IronPickaxe, "iron_pickaxe", ToolClass::Pickaxe, 3, 250));
    reg.add(tool(items::WoodAxe, "wooden_axe", ToolClass::Axe, 1, 59));
    reg.add(tool(items::StoneAxe, "stone_axe", ToolClass::Axe, 2, 131));
}

}  // namespace vg::item
