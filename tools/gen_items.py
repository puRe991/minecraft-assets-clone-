#!/usr/bin/env python3
"""Generate src/item_data.h from data/items.txt.

Every entry in the catalogue gets, derived from its name:

  * a model      -- which box geometry it is built from (cube, slab, stairs,
                    cross, sword, bottle, ...);
  * a pattern    -- the recipe the procedural texture synthesiser uses;
  * colours      -- top / side / bottom base colour plus an accent;
  * animations   -- an idle clip, a use clip and a texture animation;
  * flags        -- placeable / solid / full cube / cut-out / emissive.

Nothing here is copied from Minecraft: the names come from the user's
collection checklist, and every visual is synthesised from them.

Usage:  python3 tools/gen_items.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "data", "items.txt")
DST = os.path.join(ROOT, "src", "item_data.h")

# --------------------------------------------------------------------------
# Enumerations. The C side mirrors these in src/items.h -- keep in sync.
# --------------------------------------------------------------------------

MODELS = [
    # block models
    "CUBE", "COLUMN", "SLAB", "STAIRS", "WALL", "FENCE", "GATE", "DOOR",
    "TRAPDOOR", "BUTTON", "PLATE", "CARPET", "PANE", "LADDER", "TORCH",
    "LANTERN", "CHAIN", "CROSS", "BUSH", "POT", "CHEST", "BED", "SIGN",
    "BANNER", "SHELF", "ANVIL", "HEAD", "CAMPFIRE", "RAIL", "CANDLE",
    "SMALLBOX", "FRAME",
    # item models
    "FLAT", "TOOL", "ROD", "ARMOR", "BOTTLE", "BOOK", "SHEET", "SHERD",
    "DISC", "BOAT", "MINECART", "FOOD", "BOW", "INGOT", "GEM", "DUST",
]

PATTERNS = [
    "NOISE", "GRAIN", "BARK", "RINGS", "SPECKLE", "BRICKS", "TILES",
    "SMOOTH", "CHISELED", "GLASS", "ORE", "FOLIAGE", "PLANT", "FLOWER",
    "CLOTH", "METAL", "GEMSTONE", "CRYSTAL", "LIQUID", "POWDER", "SAND",
    "CORAL", "SCULK", "GLOW", "WEB", "SHERD", "PAPER", "BOOK", "BOTTLE",
    "SWORD", "PICKAXE", "AXE", "SHOVEL", "HOE", "SPEAR", "BOW", "ROD",
    "ARROW", "HELMET", "CHESTPLATE", "LEGGINGS", "BOOTS", "INGOT",
    "NUGGET", "ROUND", "MEAT", "SEED", "DISC", "EGG", "BUCKET", "TRIM",
    "ENCHANT", "SKULL", "POT", "CART", "HULL", "HONEY", "SLIME", "MUSHROOM",
]

ANIMS = [
    "NONE", "BOB", "SWAY", "SPIN", "SWING", "STAB", "DRINK", "EAT",
    "SHOOT", "PLACE", "OPEN", "FLICKER", "PULSE",
]

TEXANIMS = ["NONE", "FLOW", "FIRE", "SWIRL", "GLINT", "PULSE", "SPARKLE"]

CATEGORIES = ["ENGINE", "ITEM", "POTION", "ENCHANT", "TEMPLATE", "POTTERY"]

# flag bits
F_PLACEABLE = 1 << 0
F_SOLID     = 1 << 1
F_FULLCUBE  = 1 << 2
F_CUTOUT    = 1 << 3
F_EMISSIVE  = 1 << 4
F_LIQUID    = 1 << 5
F_HIDDEN    = 1 << 6
F_TINTED    = 1 << 7

BLOCK_MODELS = set(MODELS[:MODELS.index("FLAT")])
SOLID_MODELS = {
    "CUBE", "COLUMN", "SLAB", "STAIRS", "WALL", "FENCE", "GATE", "DOOR",
    "TRAPDOOR", "PANE", "POT", "CHEST", "BED", "SHELF", "ANVIL", "HEAD",
    "CAMPFIRE", "SMALLBOX",
}
CUTOUT_MODELS = {"CROSS", "BUSH", "RAIL", "LADDER", "CHAIN", "TORCH", "CANDLE",
                 "SIGN", "BANNER", "FRAME"}

# --------------------------------------------------------------------------
# Colour vocabulary. Longest key wins, so "dark oak" beats "oak".
# --------------------------------------------------------------------------

COLORS = {
    # dye / wool colours
    "light blue": 0x3AAFD9, "light gray": 0x8E8E86, "white": 0xE9ECEC,
    "orange": 0xF07613, "magenta": 0xBD44B3, "yellow": 0xF8C627,
    "lime": 0x80C71F, "pink": 0xF38BAA, "gray": 0x545C63, "cyan": 0x169C9C,
    "purple": 0x8932B8, "blue": 0x3C44AA, "brown": 0x835432,
    "green": 0x5E7C16, "red": 0xB02E26, "black": 0x1D1C21,
    # woods
    "dark oak": 0x4A2F17, "pale oak": 0xE3D9CC, "oak": 0xB8945F,
    "spruce": 0x7A5B33, "birch": 0xD7C185, "jungle": 0xA9724A,
    "acacia": 0xBA6337, "mangrove": 0x773934, "cherry": 0xE3B7C4,
    "bamboo": 0xC3B24B, "crimson": 0x7B3A55, "warped": 0x2B6E66,
    # stone family
    "deepslate": 0x515151, "blackstone": 0x2B2229, "cobblestone": 0x7F7F7F,
    "andesite": 0x8E8E8B, "diorite": 0xCFCFD2, "granite": 0x9A6A56,
    "calcite": 0xDFDFDA, "basalt": 0x5B5A60, "tuff": 0x6B6B62,
    "obsidian": 0x15121F, "netherrack": 0x703434, "nether brick": 0x2D161B,
    "nether wart block": 0x7B0000, "nether": 0x552526, "end stone": 0xDFE5A5,
    "purpur": 0xAA7EAA, "prismarine": 0x63AC9E, "sculk": 0x0E1B1F,
    "terracotta": 0x985E43, "concrete": 0x8E8E8E, "stone": 0x7A7A7A,
    "red sand": 0xBE6C2F, "sandstone": 0xDCD3A0, "sand": 0xDBD3A0,
    "gravel": 0x837E7C, "clay": 0xA4A8B8, "mud": 0x3E322C, "dirt": 0x7A5A3A,
    "grass": 0x74B44A, "podzol": 0x6A4B29, "mycelium": 0x6F6265,
    "moss": 0x5B7A2E, "snow": 0xF0FAFA, "ice": 0xA5C6F0,
    "resin": 0xE97613, "sulfur": 0xD9CE3B, "cinnabar": 0xB0392E,
    "dripstone": 0x866B5D, "magma": 0x8E3C11, "soul": 0x51413B,
    "bone": 0xE1DDCA, "honey": 0xF9AA2C, "slime": 0x7DC375,
    "sponge": 0xC7C24A, "hay": 0xB5960C, "pumpkin": 0xC07615,
    "melon": 0x8CB733, "cactus": 0x5B8731, "kelp": 0x4F7F2F,
    # metals & gems
    "netherite": 0x45393E, "diamond": 0x4AEDD9, "emerald": 0x17DD62,
    "lapis lazuli": 0x1F5BB5, "lapis": 0x1F5BB5, "redstone": 0xD63A2F,
    "amethyst": 0x9A5CC6, "quartz": 0xEDE9E2, "coal": 0x2A2A2A,
    "gold": 0xF9D74E, "golden": 0xF9D74E, "iron": 0xD8D8D8,
    "chainmail": 0x9A9A9A, "leather": 0xA06540, "copper": 0xC56A38,
    "exposed": 0xA97C6B, "weathered": 0x6E9B76, "oxidized": 0x4CA07C,
    "glowstone": 0xF5C465, "glow": 0xE7C86A, "shroomlight": 0xE29B54,
    "froglight": 0xE7DFA8, "echo": 0x1C4C50, "nautilus": 0xE9DFCB,
    "phantom": 0xB0A5B6, "shulker": 0x9A5EA0, "chorus": 0x7B4E7B,
    "tinted glass": 0x2A2028, "glass": 0xB8DCE8, "ladder": 0xA3763F,
    "sugar cane": 0x8DB25B, "firework": 0xD9D9D9, "scaffold": 0xC5A24F,
    "wither": 0x2A2A2A, "creeper": 0x63B04D, "dragon": 0x2B1E2B,
    "blaze": 0xE9A22C, "breeze": 0x8AB6D6, "ghast": 0xE4E4E4,
    "totem": 0xD9B24C, "trident": 0x4C6E75, "elytra": 0x8C7F8C,
    "prismarine crystals": 0xD5F0E1, "turtle": 0x63A863,
    "wheat": 0xD9C05A, "sugar": 0xEFEFEF, "paper": 0xE6E6E6,
    "book": 0xA0522D, "map": 0xD8CBA2, "water": 0x3F76E4,
    "lava": 0xD45A12, "milk": 0xF7F7F7, "egg": 0xDCD3C8,
    "apple": 0xC9302C, "carrot": 0xE08A1B, "potato": 0xD3A857,
    "beetroot": 0x8B2E2E, "berries": 0xC33B2A, "cocoa": 0x7B3F1E,
    "flint": 0x4A4A4A, "feather": 0xEFEFEF, "string": 0xE8E8E8,
    "gunpowder": 0x6E6E6E, "spider eye": 0x6B2020, "rotten flesh": 0x8B5A3C,
    "beacon": 0x6FD5CF, "conduit": 0xB6A88A, "target": 0xC8B9A0,
    "scaffolding": 0xC5A24F, "barrel": 0x8B6A3F, "lectern": 0x9A7B4F,
    "loom": 0xB4936A, "anvil": 0x484848, "cauldron": 0x4A4A4A,
    "hopper": 0x4A4A4A, "furnace": 0x6E6E6E, "smoker": 0x6A5B4B,
    "piston": 0x9C7F4E, "observer": 0x5A5A5A, "note block": 0x7A5A3A,
    "jukebox": 0x6A4B33, "tnt": 0xB63A2E, "torch": 0xFFCB6B,
    "lantern": 0xE0B45E, "candle": 0xE7DFC8, "chain": 0x4B4E56,
    "rail": 0x8A8A8A, "bars": 0x9A9A9A, "cobweb": 0xE8E8E8,
    "vines": 0x4A7A2E, "lily": 0x3F7A2E, "seagrass": 0x3F8A3F,
    "coral": 0xD84C6F, "azalea": 0x6E9E4A, "flower": 0xD75B85,
    "mushroom": 0xC57C5A, "fungus": 0xB84C4C, "sapling": 0x5A8A3A,
    "leaves": 0x4C8C2E, "log": 0x8B6A3F, "wood": 0x8B6A3F,
    "planks": 0xB8945F, "shelf": 0xB8945F, "sherd": 0xB2705A,
    "pot": 0xB2705A, "brick": 0x976B62, "tile": 0x6E6E6E,
    "trim": 0xD8CBA2, "armor": 0x9A9A9A, "harness": 0x8A6A4A,
    "bundle": 0xA37C51, "saddle": 0x8B5A2B, "lead": 0xA98B6B,
    "name tag": 0xE6DFC8, "spyglass": 0x9A7B4F, "brush": 0xC9A87C,
    "compass": 0xB03A2E, "clock": 0xD9B24C, "shears": 0xB0B0B0,
    "shield": 0x8B5A2B, "bow": 0xA3743F, "arrow": 0xB0A99A,
    "firework": 0xD9D9D9, "banner": 0xC0C0C0, "sign": 0xB8945F,
    "bed": 0xC24C4C, "boat": 0xB8945F, "raft": 0xC3B24B,
    "minecart": 0x8A8A8A, "spawn": 0xC0C0C0, "trial": 0xD9C46A,
    "heavy core": 0x5A5A66, "mace": 0x4A4A55, "wind": 0xBFD8E8,
    "eyeblossom": 0xE0A868, "dandelion": 0xF7E43F, "poppy": 0xC63A2E,
    "tulip": 0xD84C6F, "orchid": 0x2FA0C0, "allium": 0xB07AD0,
    "cornflower": 0x5A7ED0, "peony": 0xD87AC0, "lilac": 0xB98AD0,
    "sunflower": 0xF5D33F, "petals": 0xEFA9C8, "torchflower": 0xE07A2E,
    "pitcher": 0x7A5AA0, "spore": 0xD87ABF, "sniffer": 0x9A7A5A,
    "armadillo": 0x8A6A4A, "rabbit": 0xA98B6B, "cod": 0xC0A98B,
    "salmon": 0xC0603A, "pufferfish": 0xD9B24C, "tropical fish": 0xE08A1B,
    "chicken": 0xD9A98B, "beef": 0xA33A2E, "porkchop": 0xE0A0A0,
    "mutton": 0xB03A3A, "steak": 0x8B3A2E, "stew": 0xA3763F,
    "soup": 0xB03A2E, "bread": 0xC0903F, "cookie": 0xA3703F,
    "cake": 0xE6DFC8, "pie": 0xC0903F, "charcoal": 0x3A3A3A,
    "ink sac": 0x1A1A1A, "prismarine shard": 0x9AD8C0, "scute": 0x6EA86E,
    "shell": 0xE9DFCB, "membrane": 0xB0A5B6, "star": 0xE6E6C8,
    "pearl": 0x2E7A6E, "eye": 0x3A8A6E, "tear": 0xE0E8F0,
    "cream": 0xD9A050, "powder": 0xD9C46A, "dust": 0xC03A2E,
    "ingot": 0xD8D8D8, "nugget": 0xD8D8D8, "scrap": 0x8B5A3C,
    "debris": 0x5A4038, "shard": 0xB0C8D8, "core": 0x5A5A66,
    "key": 0xD9C46A, "disc": 0x3A3A3A, "fragment": 0x4A4A4A,
    "horn": 0xD9C89A, "bell": 0xE0B45E, "bee": 0xE0B45E,
    "beehive": 0xB58A4A, "nest": 0x9A7B4F, "honeycomb": 0xE8A33D,
    "crafting": 0xA3763F, "table": 0x9A7B4F, "smithing": 0x4A4A4A,
    "stonecutter": 0x7A7A7A, "grindstone": 0x9A9A9A,
    "composter": 0x8B6A3F, "cartography": 0xB4936A, "fletching": 0xC0A070,
    "bookshelf": 0xA3763F, "enchanting": 0x8B3A5A, "respawn": 0x4A2E5A,
    "lodestone": 0x8A8A9A, "crying": 0x3A2E5A, "gilded": 0x8A6A2E,
    "ancient": 0x5A4038, "dried": 0xB08A5A, "sea": 0x9AD8C0,
    "sculk vein": 0x2A3A3A, "warped wart": 0x1A7A6E,
    "twisting": 0x2B9E9E, "weeping": 0xA02E2E, "roots": 0x8B5A3C,
    "nylium": 0x8A2E2E, "hyphae": 0x7B3A55, "stem": 0x7B3A55,
    "shrieker": 0x5A5A4A, "catalyst": 0x1A2A2A, "sensor": 0x1A4A5A,
    "creaking": 0x4A5A4A, "heart": 0xC03A4A, "statue": 0xC56A38,
    "golem": 0xC56A38, "bulb": 0xD9A050, "grate": 0xA35A38,
    "wolf": 0xB0A08A, "horse": 0xB08A5A, "wildflowers": 0xE0C05A,
    "firefly": 0xC8D06A, "leaf litter": 0xA3763F, "dry grass": 0xC0A85A,
    "bush": 0x4C8C2E, "dead": 0x8A8A82, "potent": 0xE8E04A,
    "spike": 0xD9CE3B, "cube": 0xD9CE3B, "axolotl": 0xE8A0C0,
    "tadpole": 0x6A5A4A, "explorer": 0xD8CBA2, "village": 0xD8CBA2,
    "treasure": 0xD8CBA2, "ominous": 0x5A4A6A, "decay": 0x6A8A4A,
    "infestation": 0x8A8A9A, "oozing": 0x7DC375, "weaving": 0xD0C0E0,
    "wind charging": 0xBFD8E8, "swiftness": 0x7CAFC6, "healing": 0xF82423,
    "harming": 0x430A09, "regeneration": 0xCD5CAB, "strength": 0x932423,
    "leaping": 0x22FF4C, "invisibility": 0x7F8392, "night vision": 0x1F1FA1,
    "water breathing": 0x2E5299, "fire resistance": 0xE49A3A,
    "slow falling": 0xF7F8E0, "slowness": 0x5A6C81, "poison": 0x4E9331,
    "weakness": 0x484D48, "turtle master": 0x4C6E75, "awkward": 0x3F76E4,
    "mundane": 0x3F76E4, "thick": 0x3F76E4, "splash": 0x3F76E4,
    "lingering": 0x3F76E4, "potion": 0x3F76E4,
}
COLOR_KEYS = sorted(COLORS, key=len, reverse=True)

# Light emission, keyed by the first matching substring.
LIGHT = [
    ("beacon", 15), ("conduit", 15), ("sea lantern", 15), ("glowstone", 15),
    ("shroomlight", 15), ("froglight", 15), ("jack o'lantern", 15),
    ("redstone lamp", 15), ("respawn anchor", 15), ("copper bulb", 15),
    ("lava", 15), ("campfire", 15), ("lantern", 15), ("end rod", 14),
    ("torch", 14), ("crying obsidian", 10), ("soul", 10), ("glow lichen", 7),
    ("sculk catalyst", 6), ("amethyst cluster", 5), ("magma", 3),
    ("brewing stand", 1), ("firefly", 2), ("open eyeblossom", 1),
]


def find_color(name):
    low = name.lower()
    for k in COLOR_KEYS:
        if k in low:
            return COLORS[k]
    # stable fallback: a muted colour derived from the name
    h = 2166136261
    for ch in name:
        h = ((h ^ ord(ch)) * 16777619) & 0xFFFFFFFF
    r = 90 + (h & 0x3F)
    g = 90 + ((h >> 8) & 0x3F)
    b = 90 + ((h >> 16) & 0x3F)
    return (r << 16) | (g << 8) | b


def scale(col, f):
    r = min(255, int(((col >> 16) & 0xFF) * f))
    g = min(255, int(((col >> 8) & 0xFF) * f))
    b = min(255, int((col & 0xFF) * f))
    return (r << 16) | (g << 8) | b


def light_of(name):
    low = name.lower()
    for key, lv in LIGHT:
        if key in low:
            # "soul sand"/"soul soil" are not light sources
            if key == "soul" and not any(w in low for w in
                                         ("torch", "lantern", "campfire", "fire")):
                continue
            return lv
    return 0


# --------------------------------------------------------------------------
# Model / pattern classification
# --------------------------------------------------------------------------

def ends(name, *suffixes):
    return any(name.endswith(s) for s in suffixes)


def has(name, *words):
    low = name.lower()
    return any(w in low for w in words)


# Words a name has to *end* with to be a plant. Suffix matching (rather than
# substring) keeps "Bamboo Mosaic" and "Mangrove Roots" the blocks they are.
PLANTS = (
    "sapling", "flower", "tulip", "orchid", "allium", "dandelion", "poppy",
    "daisy", "azure bluet", "cornflower", "lily of the valley", "wither rose",
    "fern", "grass", "bush", "roots", "fungus", "sprouts", "mushroom", "kelp",
    "seagrass", "cane", "bamboo", "vines", "lichen", "vein", "wart", "wheat",
    "eyeblossom", "propagule", "pickle", "cluster", "bud", "dripleaf",
    "blossom", "hanging moss", "azalea", "lilac", "peony", "spike", "plant",
    "dripstone", "coral", "fan", "sunflower", "torchflower", "cactus flower",
)
PLANT_EXCEPTIONS = {
    "Mangrove Roots", "Muddy Mangrove Roots", "Cactus", "Bush",
    "Nether Wart Block", "Warped Wart Block", "Sculk", "Fletching Table",
}

TALL_PLANTS = ("tall grass", "large fern", "sunflower", "lilac", "peony",
               "rose bush", "pitcher plant", "big dripleaf", "tall dry grass")


def plant_suffix(name):
    """True when the (parenthetical-stripped) name ends in a plant word."""
    if name in PLANT_EXCEPTIONS:
        return False
    low = re.sub(r"\s*\([^)]*\)\s*$", "", name).strip().lower()
    return any(low == p or low.endswith(" " + p) for p in PLANTS)


def classify(name, section):
    """Return (model, pattern, ctop, cside, cbot, caccent, idle, use, texanim)."""
    low = name.lower()
    base = find_color(name)
    model = pattern = None

    # ---- category-driven models -----------------------------------------
    if section == "potions":
        model, pattern = "BOTTLE", "BOTTLE"
        idle, use, ta = "BOB", "DRINK", "SWIRL"
        return model, pattern, base, base, scale(base, 0.7), 0xC8D8F0, idle, use, ta
    if section == "enchants":
        return "BOOK", "ENCHANT", 0x9B7BC8, 0x9B7BC8, 0x6B4B98, 0xE8D8FF, "BOB", "OPEN", "GLINT"
    if section == "templates":
        return "SHEET", "TRIM", 0xD8CBA2, 0xD8CBA2, 0xB0A585, base, "BOB", "NONE", "NONE"
    if section == "pottery":
        if low.endswith("decorated pot"):
            return "POT", "POT", 0xB2705A, 0xB2705A, 0x8E5A48, 0xE0C8A8, "BOB", "PLACE", "NONE"
        return "SHERD", "SHERD", 0xB2705A, 0xB2705A, 0x8E5A48, 0xE0C8A8, "BOB", "NONE", "NONE"

    # ---- block shapes (suffix driven) -----------------------------------
    if ends(name, "Slab"):
        model, pattern = "SLAB", None
    elif ends(name, "Stairs", "Stair"):
        model, pattern = "STAIRS", None
    elif ends(name, "Wall") and not has(name, "wall banner"):
        model, pattern = "WALL", None
    elif ends(name, "Fence Gate"):
        model, pattern = "GATE", None
    elif ends(name, "Fence"):
        model, pattern = "FENCE", None
    elif ends(name, "Door"):
        model, pattern = "DOOR", None
    elif ends(name, "Trapdoor"):
        model, pattern = "TRAPDOOR", None
    elif ends(name, "Button"):
        model, pattern = "BUTTON", None
    elif ends(name, "Pressure Plate"):
        model, pattern = "PLATE", None
    elif ends(name, "Carpet", "Petals", "Leaf Litter", "Wildflowers", "Lily Pad",
              "Moss Carpet", "Snow"):
        model, pattern = "CARPET", None
    elif ends(name, "Pane", "Bars"):
        model, pattern = "PANE", "GLASS" if "glass" in low else "METAL"
    elif ends(name, "Ladder", "Scaffolding"):
        model, pattern = "LADDER", "GRAIN"
    elif ends(name, "Lantern"):
        model, pattern = "LANTERN", "GLOW"
    elif ends(name, "Chain"):
        model, pattern = "CHAIN", "METAL"
    elif ends(name, "Torch", "End Rod", "Lightning Rod"):
        model, pattern = "TORCH", "GLOW" if "torch" in low else "METAL"
    elif ends(name, "Candle"):
        model, pattern = "CANDLE", "SMOOTH"
    elif ends(name, "Campfire"):
        model, pattern = "CAMPFIRE", "GRAIN"
    elif ends(name, "Rail"):
        model, pattern = "RAIL", "METAL"
    elif ends(name, "Bed"):
        model, pattern = "BED", "CLOTH"
    elif ends(name, "Hanging Sign", "Sign"):
        model, pattern = "SIGN", "GRAIN"
    elif ends(name, "Banner"):
        model, pattern = "BANNER", "CLOTH"
    elif ends(name, "Shelf"):
        model, pattern = "SHELF", "GRAIN"
    elif ends(name, "Anvil"):
        model, pattern = "ANVIL", "METAL"
    elif ends(name, "Head", "Skull"):
        model, pattern = "HEAD", "SKULL"
    elif ends(name, "Boat", "Raft", "Boat with Chest", "Raft with Chest"):
        model, pattern = "BOAT", "HULL"
    elif low.startswith("minecart"):
        model, pattern = "MINECART", "CART"
    elif ends(name, "Chest") and not has(name, "boat", "minecart"):
        model, pattern = "CHEST", "GRAIN"
    elif ends(name, "Shulker Box", "Barrel", "Furnace", "Smoker", "Dispenser",
              "Dropper", "Observer", "Crafter", "Jukebox", "Note Block",
              "Bookshelf", "Loom", "Composter", "Lectern", "Beehive",
              "Bee Nest", "Cartography Table", "Fletching Table",
              "Smithing Table", "Crafting Table", "Stonecutter", "Grindstone",
              "Enchanting Table", "Beacon", "Conduit", "Cauldron", "Hopper",
              "Brewing Stand", "Bell", "Flower Pot", "Piston", "Sticky Piston",
              "Armor Stand", "End Crystal", "Lodestone", "Respawn Anchor",
              "Daylight Detector", "Target", "Cake", "Decorated Pot",
              "Golem Statue", "Copper Golem Statue", "Dried Ghast",
              "Creaking Heart", "Calibrated Sculk Sensor", "Sculk Sensor",
              "Sculk Shrieker", "Sculk Catalyst", "Turtle Egg", "Dragon Egg",
              "Sniffer Egg", "Heavy Core", "Chiseled Bookshelf",
              "Redstone Comparator", "Redstone Repeater", "Lever",
              "Tripwire Hook", "Item Frame", "Glow Item Frame", "Painting",
              "Trapped Chest", "Ender Chest"):
        if ends(name, "Item Frame", "Painting"):
            model, pattern = "FRAME", "GRAIN"
        elif ends(name, "Cauldron", "Flower Pot", "Decorated Pot", "Brewing Stand"):
            model, pattern = "POT", "POT"
        elif ends(name, "Lever", "Tripwire Hook", "Redstone Comparator",
                  "Redstone Repeater", "Bell", "Heavy Core", "Turtle Egg",
                  "Dragon Egg", "Sniffer Egg", "End Crystal", "Armor Stand"):
            model, pattern = "SMALLBOX", None
        else:
            model, pattern = "CUBE", None

    # ---- item shapes -----------------------------------------------------
    if model is None:
        if ends(name, "Sword"):
            model, pattern = "TOOL", "SWORD"
        elif ends(name, "Pickaxe"):
            model, pattern = "TOOL", "PICKAXE"
        elif ends(name, "Axe"):
            model, pattern = "TOOL", "AXE"
        elif ends(name, "Shovel"):
            model, pattern = "TOOL", "SHOVEL"
        elif ends(name, "Hoe"):
            model, pattern = "TOOL", "HOE"
        elif ends(name, "Spear") or name == "Trident":
            model, pattern = "TOOL", "SPEAR"
        elif name in ("Mace",):
            model, pattern = "TOOL", "AXE"
        elif ends(name, "Bow", "Crossbow", "Fishing Rod", "Carrot on a Stick",
                  "Warped Fungus on a Stick"):
            model, pattern = "BOW", "BOW"
        elif ends(name, "Helmet", "Cap"):
            model, pattern = "ARMOR", "HELMET"
        elif ends(name, "Chestplate", "Tunic"):
            model, pattern = "ARMOR", "CHESTPLATE"
        elif ends(name, "Leggings", "Pants"):
            model, pattern = "ARMOR", "LEGGINGS"
        elif ends(name, "Boots"):
            model, pattern = "ARMOR", "BOOTS"
        elif ends(name, "Horse Armor", "Wolf Armor", "Nautilus Armor",
                  "Harness", "Elytra", "Shield", "Saddle"):
            model, pattern = "ARMOR", "CHESTPLATE"
        elif "arrow" in low:
            model, pattern = "ROD", "ARROW"
        elif low.startswith("firework rocket"):
            model, pattern = "ROD", "ROD"
        elif low.startswith("firework star"):
            model, pattern = "FLAT", "ROUND"
        elif ends(name, "Bundle"):
            model, pattern = "FLAT", "CLOTH"
        elif ends(name, "Rod", "Stick", "Bone", "Blaze Rod", "Breeze Rod",
                  "Lead", "Brush", "Spyglass", "Goat Horn"):
            model, pattern = "ROD", "ROD"
        elif "goat horn" in low:
            model, pattern = "ROD", "ROD"
        elif ends(name, "Bucket") or low.startswith("bucket"):
            model, pattern = "BOTTLE", "BUCKET"
        elif ends(name, "Bottle", "Bottle o' Enchanting", "Honey Bottle",
                  "Dragon's Breath"):
            model, pattern = "BOTTLE", "BOTTLE"
        elif ends(name, "Book", "Book and Quill", "Written Book"):
            model, pattern = "BOOK", "BOOK"
        elif ends(name, "Map", "Paper", "Banner Pattern", "Name Tag") or \
                low.startswith("banner pattern") or ends(name, "Empty Map"):
            model, pattern = "SHEET", "PAPER"
        elif low.startswith("music disc") or ends(name, "Disc Fragment"):
            model, pattern = "DISC", "DISC"
        elif ends(name, "Ingot"):
            model, pattern = "INGOT", "INGOT"
        elif ends(name, "Nugget", "Scrap"):
            model, pattern = "INGOT", "NUGGET"
        elif ends(name, "Dust", "Powder", "Bone Meal", "Sugar", "Gunpowder",
                  "Blaze Powder", "Glowstone Dust"):
            model, pattern = "DUST", "POWDER"
        elif name in ("Diamond", "Emerald", "Lapis Lazuli", "Amethyst Shard",
                      "Nether Quartz", "Echo Shard", "Prismarine Shard",
                      "Prismarine Crystals", "Heart of the Sea", "Nether Star",
                      "Ender Pearl", "Eye of Ender", "Ghast Tear",
                      "Netherite Ingot", "Coal", "Charcoal", "Flint",
                      "Raw Copper", "Raw Gold", "Raw Iron", "Copper Ingot",
                      "Gold Ingot", "Iron Ingot", "Clay Ball", "Brick",
                      "Nether Brick", "Resin Brick", "Slimeball", "Magma Cream",
                      "Shulker Shell", "Nautilus Shell", "Turtle Scute",
                      "Armadillo Scute", "Phantom Membrane", "Rabbit's Foot",
                      "Rabbit Hide", "Leather", "Feather", "String",
                      "Fermented Spider Eye", "Spider Eye", "Glow Ink Sac",
                      "Ink Sac", "Honeycomb", "Glistering Melon Slice",
                      "Totem of Undying", "Trial Key", "Ominous Trial Key",
                      "Wind Charge", "Fire Charge", "Disc Fragment",
                      "Breeze Rod", "Blaze Rod", "Bone"):
            model, pattern = "GEM", "GEMSTONE"
        elif ends(name, "Seeds", "Pod", "Cocoa Beans"):
            model, pattern = "FLAT", "SEED"
        elif ends(name, "Stew", "Soup"):
            model, pattern = "FOOD", "ROUND"
        elif has(name, "cooked", "raw ") or ends(name, "Steak", "Bread",
                                                 "Cookie", "Pie", "Apple",
                                                 "Carrot", "Potato", "Beetroot",
                                                 "Melon Slice", "Berries",
                                                 "Chorus Fruit", "Dried Kelp",
                                                 "Rotten Flesh", "Pufferfish",
                                                 "Tropical Fish", "Egg"):
            model, pattern = "FOOD", "MEAT" if has(name, "cooked", "raw ", "steak",
                                                   "flesh") else "ROUND"

    # ---- plants and remaining blocks -------------------------------------
    if model is None:
        if low.startswith("block of"):
            model = "COLUMN" if "bamboo" in low else "CUBE"
        elif any(p in low for p in TALL_PLANTS):
            model, pattern = "BUSH", "PLANT"
        elif plant_suffix(name) and not ends(name, "Block", "Bricks"):
            if ends(name, "Coral", "Coral Fan", "Fan"):
                model, pattern = "CROSS", "CORAL"
            elif has(name, "flower", "tulip", "orchid", "allium", "dandelion",
                     "poppy", "daisy", "bluet", "cornflower",
                     "lily of the valley", "rose", "eyeblossom", "blossom"):
                model, pattern = "CROSS", "FLOWER"
            elif has(name, "mushroom", "fungus"):
                model, pattern = "CROSS", "MUSHROOM"
            else:
                model, pattern = "CROSS", "PLANT"
        elif ends(name, "Log", "Stem", "Hyphae", "Pillar", "Hay Bale",
                  "Bone Block", "Basalt", "Wood", "Cactus", "Bamboo"):
            model, pattern = "COLUMN", "BARK"
        elif ends(name, "Grass Block", "Mycelium", "Podzol", "Dirt Path [BE]",
                  "Crimson Nylium", "Warped Nylium"):
            model, pattern = "COLUMN", "NOISE"
        else:
            model = "CUBE"

    # ---- pattern for blocks that only got a shape ------------------------
    if pattern == "PLANT" and has(name, "amethyst", "dripstone", "spike"):
        pattern = "CRYSTAL"
    if pattern is None and model == "COLUMN":
        pattern = "BARK"
    if pattern is None:
        if has(name, "planks", "mosaic", "shelf", "sign", "barrel", "bookshelf",
               "crafting", "lectern", "loom", "composter", "scaffolding",
               "jukebox", "note block", "beehive", "bee nest", "campfire"):
            pattern = "GRAIN"
        elif has(name, "leaves", "azalea"):
            pattern = "FOLIAGE"
        elif has(name, "glass"):
            pattern = "GLASS"
        elif has(name, "ore"):
            pattern = "ORE"
        elif has(name, "bricks", "brick"):
            pattern = "BRICKS"
        elif has(name, "tiles", "tile", "glazed"):
            pattern = "TILES"
        elif has(name, "chiseled"):
            pattern = "CHISELED"
        elif has(name, "cobble", "gravel", "cracked", "mossy", "netherrack",
                 "blackstone", "deepslate", "tuff", "basalt", "andesite",
                 "granite", "diorite", "dripstone", "calcite"):
            pattern = "SPECKLE"
        elif has(name, "wool", "carpet", "banner", "bed", "harness"):
            pattern = "CLOTH"
        elif has(name, "concrete", "terracotta", "smooth", "polished",
                 "packed", "quartz", "purpur", "prismarine", "end stone",
                 "obsidian", "clay"):
            pattern = "SMOOTH"
        elif has(name, "sand", "soul", "mud"):
            pattern = "SAND"
        elif has(name, "block of", "iron", "gold", "copper", "netherite",
                 "anvil", "bars", "chain", "rail", "hopper", "cauldron"):
            pattern = "METAL"
        elif has(name, "sculk"):
            pattern = "SCULK"
        elif has(name, "amethyst", "diamond", "emerald", "crystal"):
            pattern = "CRYSTAL"
        elif has(name, "ice", "snow"):
            pattern = "SMOOTH"
        elif has(name, "honey"):
            pattern = "HONEY"
        elif has(name, "slime"):
            pattern = "SLIME"
        elif has(name, "cobweb"):
            pattern = "WEB"
        elif has(name, "glowstone", "lamp", "froglight", "shroomlight",
                 "sea lantern", "magma"):
            pattern = "GLOW"
        else:
            pattern = "NOISE"

    # ---- animations ------------------------------------------------------
    if model in ("TOOL",):
        idle, use = "BOB", ("STAB" if pattern == "SPEAR" else "SWING")
    elif model == "BOW":
        idle, use = "BOB", "SHOOT"
    elif model == "FOOD":
        idle, use = "BOB", "EAT"
    elif model == "BOTTLE":
        idle, use = "BOB", "DRINK"
    elif model in ("DOOR", "TRAPDOOR", "CHEST", "SHELF", "BOOK"):
        idle, use = "BOB", "OPEN"
    elif model in ("CROSS", "BUSH"):
        idle, use = "SWAY", "PLACE"
    elif model in ("TORCH", "LANTERN", "CAMPFIRE", "CANDLE"):
        idle, use = "FLICKER", "PLACE"
    elif model in BLOCK_MODELS:
        idle, use = "BOB", "PLACE"
    else:
        idle, use = "BOB", "SWING"

    # ---- texture animation ----------------------------------------------
    ta = "NONE"
    if has(name, "water", "lava", "milk"):
        ta = "FLOW"
    elif has(name, "torch", "campfire", "fire charge", "magma", "lantern"):
        ta = "FIRE"
    elif has(name, "beacon", "conduit", "respawn anchor", "sculk", "lamp",
             "bulb", "glowstone", "froglight", "sea lantern", "shroomlight"):
        ta = "PULSE"
    elif has(name, "amethyst", "diamond", "prismarine", "nether star",
             "totem", "echo", "heart of the sea", "enchanted golden apple",
             "dragon's breath", "end crystal"):
        ta = "SPARKLE"
    elif has(name, "enchanted", "ominous"):
        ta = "GLINT"

    ctop = cside = base
    cbot = scale(base, 0.78)
    accent = scale(base, 1.35)

    if model == "COLUMN":
        ctop = scale(base, 1.3)
    if "grass block" in low:
        ctop, cside, cbot, accent = 0x74B44A, 0x7A5A3A, 0x7A5A3A, 0x74B44A
    if pattern == "ORE":
        cside = ctop = 0x7A7A7A if "deepslate" not in low else 0x515151
        accent = base
    if pattern == "GLOW":
        accent = 0xFFF2C0
    return model, pattern, ctop, cside, cbot, accent, idle, use, ta


# --------------------------------------------------------------------------
# Emit
# --------------------------------------------------------------------------

def slug(name):
    s = name.upper()
    s = s.replace("+", " PLUS ").replace("'", "")
    s = re.sub(r"[^A-Z0-9]+", "_", s).strip("_")
    if s and s[0].isdigit():
        s = "N" + s
    return s


def main():
    sections, cur = {}, None
    order = []
    for raw in open(SRC, encoding="utf-8"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            cur = line[1:-1]
            sections[cur] = []
            order.append(cur)
            continue
        if cur is None:
            sys.exit("entry before any [section]: " + line)
        sections[cur].append(line)

    # engine blocks come first so air stays id 0
    entries = [("Air", "engine"), ("Water", "engine"), ("Lava", "engine")]
    for sec in order:
        entries += [(n, sec) for n in sections[sec]]

    seen, defs, rows, names = {}, [], [], []
    for idx, (name, sec) in enumerate(entries):
        s = slug(name)
        if s in seen:
            s = "%s_%d" % (s, idx)
        seen[s] = idx

        if sec == "engine":
            if name == "Air":
                row = ("CUBE", "NOISE", 0, 0, 0, 0, "NONE", "NONE", "NONE")
            elif name == "Water":
                row = ("CUBE", "LIQUID", 0x3F76E4, 0x3F76E4, 0x2F5FC0,
                       0x8FB6F0, "NONE", "NONE", "FLOW")
            else:
                row = ("CUBE", "LIQUID", 0xD45A12, 0xD45A12, 0xA03A08,
                       0xFFC060, "NONE", "NONE", "FLOW")
            cat = "ENGINE"
        else:
            row = classify(name, sec)
            cat = {"items": "ITEM", "potions": "POTION", "enchants": "ENCHANT",
                   "templates": "TEMPLATE", "pottery": "POTTERY"}[sec]

        model, pattern, ctop, cside, cbot, accent, idle, use, ta = row
        light = 0 if sec == "engine" else light_of(name)
        if name == "Lava":
            light = 15

        flags = 0
        if model in BLOCK_MODELS:
            flags |= F_PLACEABLE
        if model in SOLID_MODELS:
            flags |= F_SOLID
        if model in ("CUBE", "COLUMN"):
            flags |= F_FULLCUBE
        if model in CUTOUT_MODELS or model not in BLOCK_MODELS:
            flags |= F_CUTOUT
        if light:
            flags |= F_EMISSIVE
        if name in ("Water", "Lava"):
            flags = F_PLACEABLE | F_LIQUID | (F_EMISSIVE if light else 0)
        if name == "Air":
            flags = 0
        if sec == "engine":
            flags |= F_HIDDEN

        defs.append("#define IT_%-38s %4d" % (s, idx))
        rows.append("    { CAT_%s, MODEL_%s, PAT_%s, %3d, ANIM_%s, ANIM_%s, "
                    "TA_%s, %2d, 0x%06X, 0x%06X, 0x%06X, 0x%06X },"
                    % (cat, model, pattern, flags, idle, use, ta, light,
                       ctop, cside, cbot, accent))
        names.append('    "%s",' % name.replace('"', '\\"'))

    counts = {c: 0 for c in CATEGORIES}
    for name, sec in entries:
        counts[{"engine": "ENGINE", "items": "ITEM", "potions": "POTION",
                "enchants": "ENCHANT", "templates": "TEMPLATE",
                "pottery": "POTTERY"}[sec]] += 1

    out = []
    w = out.append
    w("/* Generated by tools/gen_items.py from data/items.txt -- do not edit.")
    w(" *")
    w(" * %d entries: %s." % (len(entries),
                              ", ".join("%d %s" % (counts[c], c.lower())
                                        for c in CATEGORIES)))
    w(" * Each row is an ItemDef (see src/items.h): model, texture pattern,")
    w(" * flags, idle/use animation, texture animation, light level and the")
    w(" * four base colours the texture synthesiser paints with.")
    w(" */")
    w("#ifndef ITEM_DATA_H")
    w("#define ITEM_DATA_H")
    w("")
    w("#define ITEM_COUNT %d" % len(entries))
    for c in CATEGORIES:
        w("#define ITEM_COUNT_%-9s %4d" % (c, counts[c]))
    w("")
    w("/* Symbolic ids, so engine code can name the blocks it needs. */")
    out.extend(defs)
    w("")
    w("static const char *const g_item_names[ITEM_COUNT] = {")
    out.extend(names)
    w("};")
    w("")
    w("static const ItemDef g_items[ITEM_COUNT] = {")
    out.extend(rows)
    w("};")
    w("")
    w("#endif /* ITEM_DATA_H */")

    with open(DST, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")

    print("wrote %s: %d items (%s)" %
          (os.path.relpath(DST, ROOT), len(entries),
           ", ".join("%d %s" % (counts[c], c.lower()) for c in CATEGORIES)))
    # a quick model histogram helps spot classification mistakes
    hist = {}
    for name, sec in entries:
        if sec == "engine":
            continue
        m = classify(name, sec)[0]
        hist[m] = hist.get(m, 0) + 1
    print("models: " + ", ".join("%s=%d" % kv for kv in
                                 sorted(hist.items(), key=lambda kv: -kv[1])))


if __name__ == "__main__":
    main()
