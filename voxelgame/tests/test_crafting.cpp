// Unit tests for crafting recipes (Module 7).
#include "TestFramework.hpp"
#include "vg/item/Recipe.hpp"

using namespace vg::item;

struct Kit {
    ItemRegistry reg;
    RecipeBook book;
    Kit() { registerDefaultItems(reg); registerDefaultRecipes(book, reg); }
};

VG_TEST(shapeless_log_to_planks_any_slot) {
    Kit k;
    Grid3 g{};                              // all empty
    g[4] = items::Log;                      // log in the centre
    ItemStack out = k.book.match(g);
    CHECK(out.item == items::Planks);
    CHECK(out.count == 4);
    // same result with the log in a different slot (shapeless = position-free)
    Grid3 g2{}; g2[0] = items::Log;
    CHECK(k.book.match(g2).item == items::Planks);
}

VG_TEST(shaped_sticks) {
    Kit k;
    Grid3 g{};
    g[0] = items::Planks; g[3] = items::Planks;   // two stacked planks (col)
    ItemStack out = k.book.match(g);
    CHECK(out.item == items::Stick);
    CHECK(out.count == 4);
    // translation-invariant: the same shape shifted over still matches
    Grid3 g2{}; g2[1] = items::Planks; g2[4] = items::Planks;
    CHECK(k.book.match(g2).item == items::Stick);
    // but two planks side by side is NOT the stick recipe
    Grid3 g3{}; g3[0] = items::Planks; g3[1] = items::Planks;
    CHECK(k.book.match(g3).item == items::None);
}

VG_TEST(shaped_wood_pickaxe) {
    Kit k;
    Grid3 g{ items::Planks, items::Planks, items::Planks,
             items::None,   items::Stick,  items::None,
             items::None,   items::Stick,  items::None };
    ItemStack out = k.book.match(g);
    CHECK(out.item == items::WoodPickaxe);
    CHECK(out.count == 1);
    // wrong material in the corner breaks the shape
    Grid3 bad = g; bad[0] = items::Stick;
    CHECK(k.book.match(bad).item == items::None);
}

VG_TEST(stone_pickaxe_differs_from_wood) {
    Kit k;
    Grid3 g{ items::Cobblestone, items::Cobblestone, items::Cobblestone,
             items::None,        items::Stick,        items::None,
             items::None,        items::Stick,        items::None };
    CHECK(k.book.match(g).item == items::StonePickaxe);
}

VG_TEST(empty_grid_matches_nothing) {
    Kit k;
    Grid3 g{};
    CHECK(k.book.match(g).item == items::None);
}
