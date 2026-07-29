// Unit tests for items, durability, and inventory (Module 7).
#include "TestFramework.hpp"
#include "vg/item/Inventory.hpp"

using namespace vg::item;

static ItemRegistry makeReg() { ItemRegistry r; registerDefaultItems(r); return r; }

VG_TEST(item_registry_and_make) {
    auto reg = makeReg();
    CHECK(reg.count() == items::Count);
    CHECK(reg.get(items::Stone).placesBlock == vg::world::blocks::Stone);
    ItemStack pick = reg.make(items::WoodPickaxe);
    CHECK(pick.count == 1);
    CHECK(pick.durability == 59);          // tools start with full durability
    CHECK(reg.get(items::WoodPickaxe).maxStack == 1);
}

VG_TEST(durability_breaks_tool) {
    auto reg = makeReg();
    ItemStack pick = reg.make(items::WoodPickaxe);
    for (int i = 0; i < 58; ++i) CHECK(!damageItem(pick, reg));   // survives 58 hits
    bool broke = damageItem(pick, reg);                            // 59th breaks it
    CHECK(broke);
    CHECK(pick.empty());
    // non-tools take no damage
    ItemStack dirt = reg.make(items::Dirt, 10);
    CHECK(!damageItem(dirt, reg));
    CHECK(dirt.count == 10);
}

VG_TEST(inventory_add_merges_and_respects_stack) {
    auto reg = makeReg();
    Inventory inv(4, reg);
    CHECK(inv.add(reg.make(items::Dirt, 40)) == 0);
    CHECK(inv.add(reg.make(items::Dirt, 40)) == 0);   // 80 total, spills into 2nd slot
    CHECK(inv.count(items::Dirt) == 80);
    CHECK(inv.at(0).count == 64);                      // first slot capped at maxStack
    CHECK(inv.at(1).count == 16);
}

VG_TEST(inventory_add_overflow_returns_leftover) {
    auto reg = makeReg();
    Inventory inv(1, reg);                              // one slot only
    int leftover = inv.add(reg.make(items::Cobblestone, 100));
    CHECK(inv.at(0).count == 64);
    CHECK(leftover == 36);
}

VG_TEST(inventory_remove) {
    auto reg = makeReg();
    Inventory inv(4, reg);
    inv.add(reg.make(items::Coal, 30));
    CHECK(inv.remove(items::Coal, 12) == 12);
    CHECK(inv.count(items::Coal) == 18);
    CHECK(inv.remove(items::Coal, 100) == 18);         // can't remove more than present
    CHECK(inv.count(items::Coal) == 0);
}

VG_TEST(inventory_move_merge_and_swap) {
    auto reg = makeReg();
    Inventory inv(3, reg);
    inv.at(0) = reg.make(items::Stone, 10);
    inv.at(1) = reg.make(items::Stone, 5);
    inv.moveTo(0, 1);                                  // merge onto slot 1
    CHECK(inv.at(1).count == 15);
    CHECK(inv.at(0).empty());
    inv.at(0) = reg.make(items::Sand, 3);
    inv.moveTo(0, 1);                                  // different items -> swap
    CHECK(inv.at(0).item == items::Stone);
    CHECK(inv.at(1).item == items::Sand);
}
