#pragma once

#include <levibatone/sdk/Memory.hpp>
#include <levibatone/sdk/Offsets.hpp>

namespace levibatone::sdk {

class BlockSource;

class Dimension {
public:
    BlockSource *blockSource() const {
        return field<BlockSource *>(this, offsets::Dimension::mBlockSource);
    }
};

}
