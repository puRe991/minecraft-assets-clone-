// vg/item/Recipe.hpp
//
// Crafting: a RecipeBook holds shaped and shapeless recipes and matches a 3x3
// crafting grid against them. Shaped recipes match by pattern (position-
// sensitive, but translation-invariant within the grid); shapeless recipes
// match by the multiset of ingredients regardless of arrangement.
#pragma once

#include <array>
#include <vector>

#include "vg/item/Item.hpp"

namespace vg::item {

using Grid3 = std::array<ItemId, 9>;   // 3x3, row-major, 0 = empty

struct Recipe {
    bool shapeless{false};
    int  w{0}, h{0};                   // shaped: trimmed pattern size
    std::array<ItemId, 9> pattern{};   // shaped: row-major w*h
    std::vector<ItemId> ingredients;   // shapeless
    ItemStack output;
};

class RecipeBook {
public:
    void addShaped(int w, int h, std::vector<ItemId> pattern, ItemStack out);
    void addShapeless(std::vector<ItemId> ingredients, ItemStack out);

    // Return the crafting result for a 3x3 grid, or an empty stack if nothing
    // matches.
    ItemStack match(const Grid3& grid) const;

    size_t size() const { return recipes_.size(); }

private:
    std::vector<Recipe> recipes_;
};

void registerDefaultRecipes(RecipeBook& book, const ItemRegistry& reg);

}  // namespace vg::item
