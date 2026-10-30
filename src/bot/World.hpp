#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "levibatone/sdk/Types.hpp"
#include "levibatone/sdk/world/Block.hpp"
#include "levibatone/sdk/world/BlockSource.hpp"

namespace levibatone::bot {

using sdk::BlockPos;

// Block classification and world queries over BlockSource::getBlock.
// Classification is name-based (the Bedrock block name string) and cached per
// Block* singleton, mirroring how Baritone keys its BlockState lookups.
class World {
public:
    enum class Class : std::uint8_t {
        Air,
        Liquid,
        Passable,
        Climbable,
        Solid,
        Unbreakable,
        Dangerous,
        Unknown,
    };

    static World &get();

    // Rebinds to the current dimension's block source. Returns false when
    // unavailable. Call from the game thread.
    bool refresh(sdk::BlockSource *source);

    const void *getBlock(const BlockPos &pos) const;
    Class classify(const void *block) const;
    Class classify(const BlockPos &pos) const;

    bool canWalkOn(const BlockPos &pos) const;
    bool canWalkThrough(const BlockPos &pos) const;
    bool isSolid(const BlockPos &pos) const;
    bool isBreakable(const BlockPos &pos) const;

    // True when the block name matches the query ("iron_ore" matches
    // "minecraft:deepslate_iron_ore" and "minecraft:iron_ore").
    static bool nameMatches(const std::string &fullName, std::string_view query);

    // Baritone's blocksToAvoidBreaking: breaking these is allowed but heavily
    // penalized in path cost.
    static bool isValuable(const std::string &fullName);

    // Farmable crops (harvested by the farm process).
    static bool isCropName(const std::string &fullName);

private:
    World();

    Class classifyUncached(const sdk::Block &block) const;

    sdk::BlockSource *mSource = nullptr;
    mutable std::mutex mCacheMutex;
    mutable std::unordered_map<const void *, Class> mCache;
};

}
