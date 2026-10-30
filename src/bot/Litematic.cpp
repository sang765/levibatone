#include "Litematic.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "Gzip.hpp"

namespace levibatone::bot::litematic {

namespace {

// Java names that differ on Bedrock. Everything else passes through with the
// namespace stripped (the builder matches by name substring anyway).
const std::unordered_map<std::string, std::string> kJavaToBedrock{
    {"grass_block", "grass"},
    {"short_grass", "short_grass"},
    {"piston_head", "piston_arm_collision"},
    {"sticky_piston_head", "sticky_piston_arm_collision"},
    {"piston", "piston"},
    {"comparator", "unpowered_comparator"},
    {"repeater", "unpowered_repeater"},
    {"redstone_lamp", "redstone_lamp"},
    {"iron_bars", "iron_bars"},
    {"end_rod", "end_rod"},
    {"cauldron", "cauldron"},
    {"snow", "snow"},
};

std::string stripNamespace(const std::string &fullName) {
    const auto colon = fullName.rfind(':');
    return colon == std::string::npos ? fullName : fullName.substr(colon + 1);
}

// LitematicaBitArray equivalent: entries packed LSB-first across 64-bit longs,
// spanning boundaries; longs are big-endian-decoded by the NBT reader.
struct BitArray {
    const std::vector<Value> *longs = nullptr;
    int bits = 2;

    std::int32_t get(std::size_t index) const {
        const std::size_t startBit = index * static_cast<std::size_t>(bits);
        std::uint64_t value = 0;
        for (int i = 0; i < bits; ++i) {
            const std::size_t bit = startBit + static_cast<std::size_t>(i);
            const std::size_t longIndex = bit / 64;
            if (longIndex >= longs->size()) return 0;
            const auto entry = (*longs)[longIndex].data;
            if (auto *v = std::get_if<std::int64_t>(&entry)) {
                const auto bits64 = static_cast<std::uint64_t>(*v);
                value |= ((bits64 >> (bit % 64)) & 1ULL) << i;
            }
        }
        return static_cast<std::int32_t>(value);
    }
};

std::int32_t compoundInt(const Value &value, const char *key, std::int32_t fallback = 0) {
    const auto *obj = value.asCompound();
    if (!obj) return fallback;
    const auto it = obj->find(key);
    if (it == obj->end()) return fallback;
    return it->second.asInt(fallback);
}

bool addRegion(const Value &region, BlockPos origin, std::vector<BlockCell> &cells) {
    const auto *obj = region.asCompound();
    if (!obj) return false;

    const auto posIt = obj->find("Position");
    const auto sizeIt = obj->find("Size");
    const auto paletteIt = obj->find("BlockStatePalette");
    const auto statesIt = obj->find("BlockStates");
    if (posIt == obj->end() || sizeIt == obj->end() || paletteIt == obj->end() ||
        statesIt == obj->end()) {
        return false;
    }

    const BlockPos regionPos{compoundInt(posIt->second, "x"), compoundInt(posIt->second, "y"),
                             compoundInt(posIt->second, "z")};
    const BlockPos size{std::abs(compoundInt(sizeIt->second, "x")),
                        std::abs(compoundInt(sizeIt->second, "y")),
                        std::abs(compoundInt(sizeIt->second, "z"))};
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) return false;
    const std::int64_t volume = static_cast<std::int64_t>(size.x) * size.y * size.z;
    if (volume > 200000) return false; // per-region cap

    const nbt::List *palette = paletteIt->second.asList();
    // BlockStates is a LongArray: the NBT reader stores it as a List of int64.
    const nbt::List *statesList = statesIt->second.asList();
    if (!palette || !statesList || statesList->empty()) return false;

    // Resolved palette names.
    std::vector<std::string> names;
    names.reserve(palette->size());
    for (const Value &entry : *palette) {
        std::string name = "minecraft:air";
        if (const auto *compound = entry.asCompound()) {
            const auto nameIt = compound->find("Name");
            if (nameIt != compound->end()) {
                if (const auto *str = nameIt->second.asString()) name = *str;
            }
        }
        names.push_back(toBedrockName(name));
    }

    BitArray bits;
    bits.longs = statesList;
    bits.bits = std::max(2, static_cast<int>(std::ceil(std::log2(static_cast<double>(palette->size())))));

    for (int y = 0; y < size.y; ++y) {
        for (int z = 0; z < size.z; ++z) {
            for (int x = 0; x < size.x; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * size.x * size.z +
                                          static_cast<std::size_t>(z) * size.x +
                                          static_cast<std::size_t>(x);
                const std::int32_t paletteIndex = bits.get(index);
                if (paletteIndex < 0 || paletteIndex >= static_cast<std::int32_t>(names.size())) continue;
                const BlockPos abs{origin.x + regionPos.x + x, origin.y + regionPos.y + y,
                                   origin.z + regionPos.z + z};
                cells.push_back({abs, names[static_cast<std::size_t>(paletteIndex)]});
            }
        }
    }
    return true;
}

} // namespace

std::string toBedrockName(const std::string &javaName) {
    const std::string stripped = stripNamespace(javaName);
    const auto it = kJavaToBedrock.find(stripped);
    return it != kJavaToBedrock.end() ? "minecraft:" + it->second
                                      : "minecraft:" + stripped;
}

bool parse(const std::uint8_t *data, std::size_t size, std::vector<BlockCell> &cells) {
    std::vector<std::uint8_t> inflated;
    if (!gzip::inflate(data, size, inflated)) return false;

    nbt::Value root;
    if (!nbt::parseNbt(inflated.data(), inflated.size(), true /* big-endian */, root)) return false;
    const auto *obj = root.asCompound();
    if (!obj) return false;

    const auto regionsIt = obj->find("Regions");
    const nbt::List *regions = regionsIt != obj->end() ? regionsIt->second.asList() : nullptr;
    if (!regions || regions->empty()) return false;

    bool any = false;
    for (const Value &region : *regions) {
        if (addRegion(region, BlockPos{0, 0, 0}, cells)) any = true;
    }
    if (!any || cells.size() > 200000) {
        cells.clear();
        return false;
    }

    // Normalize to the minimum corner so the builder anchors at the player.
    BlockPos min{cells.front().pos};
    for (const BlockCell &cell : cells) {
        min.x = std::min(min.x, cell.pos.x);
        min.y = std::min(min.y, cell.pos.y);
        min.z = std::min(min.z, cell.pos.z);
    }
    for (BlockCell &cell : cells) {
        cell.pos.x -= min.x;
        cell.pos.y -= min.y;
        cell.pos.z -= min.z;
    }

    // Bottom-up, nearest first (same order as the mcstructure builder).
    std::sort(cells.begin(), cells.end(), [](const BlockCell &a, const BlockCell &b) {
        if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
        if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
        return a.pos.z < b.pos.z;
    });
    return true;
}

}
