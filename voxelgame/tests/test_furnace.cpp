// Unit tests for furnace smelting (Module 7).
#include "TestFramework.hpp"
#include "vg/item/Furnace.hpp"

using namespace vg::item;

struct Kit {
    ItemRegistry reg;
    SmeltingRegistry smelt;
    FuelRegistry fuel;
    Kit() { registerDefaultItems(reg); registerDefaultSmelting(smelt, fuel, reg); }
};

VG_TEST(smelts_sand_to_glass_using_coal) {
    Kit k;
    Furnace f(k.smelt, k.fuel, k.reg);
    f.input = k.reg.make(items::Sand, 3);
    f.fuelSlot = k.reg.make(items::Coal, 1);        // 80s burn, 10s per item

    // run 11 seconds: one item should be smelted
    for (int i = 0; i < 110; ++i) f.tick(0.1);
    CHECK(f.output.item == items::Glass);
    CHECK(f.output.count == 1);
    CHECK(f.input.count == 2);
    CHECK(f.isBurning());                            // plenty of fuel left
    CHECK(k.reg.get(items::Glass).placesBlock == vg::world::blocks::Glass);
}

VG_TEST(no_fuel_no_smelt) {
    Kit k;
    Furnace f(k.smelt, k.fuel, k.reg);
    f.input = k.reg.make(items::Sand, 3);            // no fuel
    for (int i = 0; i < 200; ++i) f.tick(0.1);
    CHECK(f.output.empty());
    CHECK(f.input.count == 3);
    CHECK(!f.isBurning());
}

VG_TEST(non_smeltable_input_does_nothing) {
    Kit k;
    Furnace f(k.smelt, k.fuel, k.reg);
    f.input = k.reg.make(items::Diamond, 1);        // not smeltable
    f.fuelSlot = k.reg.make(items::Coal, 1);
    for (int i = 0; i < 200; ++i) f.tick(0.1);
    CHECK(f.output.empty());
    CHECK(f.fuelSlot.count == 1);                    // fuel not wasted
}

VG_TEST(fuel_is_consumed_over_many_smelts) {
    Kit k;
    Furnace f(k.smelt, k.fuel, k.reg);
    f.input = k.reg.make(items::IronOreItem, 5);
    f.fuelSlot = k.reg.make(items::Coal, 1);         // 80s = enough for 8 items
    for (int i = 0; i < 600; ++i) f.tick(0.1);       // 60s -> 5 ingots
    CHECK(f.output.item == items::IronIngot);
    CHECK(f.output.count == 5);
    CHECK(f.input.empty());
}
