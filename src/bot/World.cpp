#include "World.hpp"

#include <algorithm>
#include <string_view>

namespace levibatone::bot {

namespace {

// Names are matched against the fullName string (without the "minecraft:"
// namespace when present).

bool isOneOf(std::string_view name, std::initializer_list<std::string_view> set) {
    return std::find(set.begin(), set.end(), name) != set.end();
}

bool hasSuffix(std::string_view name, std::initializer_list<std::string_view> suffixes) {
    return std::any_of(suffixes.begin(), suffixes.end(),
                       [name](std::string_view suffix) { return name.ends_with(suffix); });
}

bool hasAnyWord(std::string_view name, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [name](std::string_view word) { return name.find(word) != std::string_view::npos; });
}

World::Class classifyName(std::string_view name) {
    if (name.empty()) return World::Class::Unknown;

    if (isOneOf(name, {"air", "cave_air", "void_air"})) return World::Class::Air;

    if (isOneOf(name, {"water", "flowing_water", "lava", "flowing_lava", "bubble_column"})) {
        return name.starts_with("lava") || name.starts_with("flowing_lava") ? World::Class::Dangerous : World::Class::Liquid;
    }

    if (isOneOf(name, {"bedrock", "barrier", "light", "end_portal", "end_portal_frame", "end_gateway",
                       "portal", "command_block", "chain_command_block", "repeating_command_block",
                       "structure_block", "structure_void", "jigsaw", "moving_piston", "obsidian_e",
                       "reinforced_deepslate", "border_block", "deny", "allow"})) {
        return World::Class::Unbreakable;
    }

    if (isOneOf(name, {"fire", "soul_fire", "cactus", "sweet_berry_bush", "wither_rose",
                       "pointed_dripstone", "magma", "powder_snow", "campfire", "soul_campfire"})) {
        return World::Class::Dangerous;
    }

    if (isOneOf(name, {"tall_grass", "short_grass", "grass", "fern", "large_fern", "dead_bush",
                       "seagrass", "tall_seagrass", "kelp", "kelp_plant", "vine", "glow_lichen",
                       "sugar_cane", "bamboo_sapling", "sapling", "spruce_sapling", "birch_sapling",
                       "jungle_sapling", "acacia_sapling", "dark_oak_sapling", "mangrove_propagule",
                       "cherry_sapling", "pale_oak_sapling", "red_mushroom", "brown_mushroom",
                       "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "oxeye_daisy",
                       "cornflower", "lily_of_the_valley", "torchflower", "pink_petals",
                       "sunflower", "lilac", "rose_bush", "peony", "torch", "soul_torch",
                       "redstone_torch", "redstone_wire", "repeater", "comparator", "lever",
                       "tripwire", "tripwire_hook", "string", "snow_layer", "ladder", "rail",
                       "powered_rail", "detector_rail", "activator_rail", "wheat", "carrots",
                       "potatoes", "beetroots", "nether_wart", "cobweb", "scaffolding", "sculk_vein",
                       "moss_carpet", "pale_moss_carpet", "big_dripleaf", "small_dripleaf",
                       "hanging_roots", "spore_blossom", "azalea", "flowering_azalea", "lily_pad",
                       "pitcher_plant", "pitcher_crop", "torchflower_crop", "frogspawn",
                       "sea_pickle", "turtle_egg", "sniffer_egg", "cake", "flower_pot", "skull",
                       "head", "sign", "wall_sign", "banner", "wall_banner", "amethyst_cluster",
                       "amethyst_cluster_large", "amethyst_cluster_medium", "amethyst_cluster_small",
                       "cave_vines", "cave_vines_plant", "glow_berries", "cocoa", "farmland",
                       "grass_path", "dirt_path", "end_rod", "lightning_rod", "chain",
                       "iron_bars", "glass_pane", "tinted_glass", "waterlily"})) {
        return World::Class::Passable;
    }

    if (hasSuffix(name, {"_torch", "_rail", "_sapling", "_coral_fan", "_coral", "_sign",
                         "_banner", "_button", "_pressure_plate", "_carpet", "_doors", "_door",
                         "_trapdoor", "_fence_gate", "_bed", "_candle", "_candle_cake",
                         "_stairs"})) {
        // Stairs, slabs, trapdoors, doors and fences all have partial collision;
        // they are solids for pathing purposes but harmless to break.
        if (hasSuffix(name, {"_stairs", "_slab", "_trapdoor", "_door", "_fence_gate", "_bed"})) {
            return World::Class::Solid;
        }
        return World::Class::Passable;
    }

    if (hasSuffix(name, {"_shulker_box"}) || hasAnyWord(name, {"chest"})) {
        return World::Class::Solid;
    }

    return World::Class::Solid;
}

// Baritone's blocksToAvoidBreaking default plus other valuable blocks.
bool isValuableName(std::string_view name) {
    return hasAnyWord(name, {"chest", "furnace", "crafting_table", "anvil", "beacon",
                             "enchanting_table", "jukebox", "note_block", "shulker_box",
                             "lodestone", "respawn_anchor", "hopper", "dropper", "dispenser",
                             "observer", "piston", "spawner", "bell", "cauldron", "loom",
                             "smoker", "blast_furnace", "cartography_table", "fletching_table",
                             "grindstone", "smithing_table", "stonecutter", "barrel", "lectern"}) ||
           hasSuffix(name, {"_sign", "_banner"});
}

} // namespace

World &World::get() {
    static World instance;
    return instance;
}

World::World() = default;

bool World::refresh(sdk::BlockSource *source) {
    mSource = source;
    return source != nullptr;
}

const void *World::getBlock(const BlockPos &pos) const {
    return mSource ? mSource->getBlock(pos) : nullptr;
}

World::Class World::classifyUncached(const sdk::Block &block) const {
    const std::string *name = block.fullName();
    if (!name) return World::Class::Unknown;
    std::string_view view(*name);
    const auto colon = view.rfind(':');
    if (colon != std::string_view::npos) view = view.substr(colon + 1);
    return classifyName(view);
}

World::Class World::classify(const void *block) const {
    if (!block) return World::Class::Unknown;
    {
        std::lock_guard lock(mCacheMutex);
        const auto it = mCache.find(block);
        if (it != mCache.end()) return it->second;
    }
    const Class klass = classifyUncached(*static_cast<const sdk::Block *>(block));
    std::lock_guard lock(mCacheMutex);
    mCache.emplace(block, klass);
    return klass;
}

World::Class World::classify(const BlockPos &pos) const {
    return classify(getBlock(pos));
}

bool World::canWalkOn(const BlockPos &pos) const {
    const Class below = classify(pos);
    return below == World::Class::Solid || below == World::Class::Unbreakable;
}

bool World::canWalkThrough(const BlockPos &pos) const {
    const Class klass = classify(pos);
    return klass == World::Class::Air || klass == World::Class::Liquid || klass == World::Class::Passable;
}

bool World::isSolid(const BlockPos &pos) const {
    const Class klass = classify(pos);
    return klass == World::Class::Solid || klass == World::Class::Unbreakable;
}

bool World::isBreakable(const BlockPos &pos) const {
    const Class klass = classify(pos);
    return klass == World::Class::Solid;
}

bool World::isValuable(const std::string &fullName) {
    std::string_view view(fullName);
    const auto colon = view.rfind(':');
    if (colon != std::string_view::npos) view = view.substr(colon + 1);
    return isValuableName(view);
}

bool World::nameMatches(const std::string &fullName, std::string_view query) {
    std::string_view view(fullName);
    const auto colon = view.rfind(':');
    if (colon != std::string_view::npos) view = view.substr(colon + 1);
    return view.find(query) != std::string_view::npos;
}

} // namespace
