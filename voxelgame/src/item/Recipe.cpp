// vg/item/Recipe.cpp — see Recipe.hpp for the contract.
#include "vg/item/Recipe.hpp"

#include <algorithm>

namespace vg::item {

void RecipeBook::addShaped(int w, int h, std::vector<ItemId> pattern, ItemStack out) {
    Recipe r; r.shapeless = false; r.w = w; r.h = h; r.output = out;
    for (int i = 0; i < w * h && i < 9; ++i) r.pattern[i] = pattern[i];
    recipes_.push_back(std::move(r));
}

void RecipeBook::addShapeless(std::vector<ItemId> ingredients, ItemStack out) {
    Recipe r; r.shapeless = true; r.ingredients = std::move(ingredients); r.output = out;
    std::sort(r.ingredients.begin(), r.ingredients.end());
    recipes_.push_back(std::move(r));
}

namespace {
// Trim the 3x3 grid to the bounding box of its non-empty cells.
// Returns false if the grid is entirely empty.
bool trim(const Grid3& g, int& ox, int& oy, int& w, int& h) {
    int minx = 3, miny = 3, maxx = -1, maxy = -1;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if (g[y * 3 + x] != items::None) {
                minx = std::min(minx, x); maxx = std::max(maxx, x);
                miny = std::min(miny, y); maxy = std::max(maxy, y);
            }
    if (maxx < 0) return false;
    ox = minx; oy = miny; w = maxx - minx + 1; h = maxy - miny + 1;
    return true;
}
}  // namespace

ItemStack RecipeBook::match(const Grid3& grid) const {
    int ox, oy, gw, gh;
    bool any = trim(grid, ox, oy, gw, gh);
    if (!any) return {};

    // multiset of the grid's ingredients (for shapeless matching)
    std::vector<ItemId> gitems;
    for (ItemId id : grid) if (id != items::None) gitems.push_back(id);
    std::sort(gitems.begin(), gitems.end());

    for (const auto& r : recipes_) {
        if (r.shapeless) {
            if (gitems == r.ingredients) return r.output;
        } else {
            if (r.w != gw || r.h != gh) continue;
            bool ok = true;
            for (int y = 0; y < gh && ok; ++y)
                for (int x = 0; x < gw; ++x)
                    if (grid[(oy + y) * 3 + (ox + x)] != r.pattern[y * r.w + x]) { ok = false; break; }
            if (ok) return r.output;
        }
    }
    return {};
}

void registerDefaultRecipes(RecipeBook& b, const ItemRegistry& reg) {
    using namespace items;
    // planks from a log (shapeless), x4
    b.addShapeless({Log}, reg.make(Planks, 4));
    // sticks from two stacked planks, x4
    b.addShaped(1, 2, {Planks, Planks}, reg.make(Stick, 4));
    // pickaxes: 3 material on top, 2 sticks down the middle
    b.addShaped(3, 3, {Planks, Planks, Planks, None, Stick, None, None, Stick, None},
                reg.make(WoodPickaxe));
    b.addShaped(3, 3, {Cobblestone, Cobblestone, Cobblestone, None, Stick, None, None, Stick, None},
                reg.make(StonePickaxe));
    b.addShaped(3, 3, {IronIngot, IronIngot, IronIngot, None, Stick, None, None, Stick, None},
                reg.make(IronPickaxe));
    // axe: 2x3 (two material top, material+stick, stick)
    b.addShaped(2, 3, {Planks, Planks, Planks, Stick, None, Stick}, reg.make(WoodAxe));
}

}  // namespace vg::item
