#pragma once

#include <levibatone/sdk/Memory.hpp>
#include <levibatone/sdk/Types.hpp>
#include <levibatone/sdk/world/Dimension.hpp>

// BlockSource::getBlock is resolved through the signature scanner at runtime
// (see memory/Signatures.cpp); the resolved address is injected here at enable().
namespace levibatone::sdk {

using BlockSourceGetBlockFn = const void *(*)(const BlockSource *, const BlockPos &);

extern BlockSourceGetBlockFn blockSourceGetBlock;

class BlockSource {
public:
    // Returns the Block* at pos, or nullptr when the chunk is not loaded.
    const void *getBlock(const BlockPos &pos) const {
        if (!blockSourceGetBlock) return nullptr;
        return blockSourceGetBlock(this, pos);
    }
};

}
