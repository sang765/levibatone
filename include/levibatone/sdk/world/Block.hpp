#pragma once

#include <levibatone/sdk/Memory.hpp>
#include <levibatone/sdk/Offsets.hpp>
#include <string>

// Block identity through the verified offset chain:
// Block+0x68 -> BlockLegacy -> +0x88 NameInfo -> +0x40 HashedString -> +0x8 std::string
namespace levibatone::sdk {

class Block {
public:
    void *blockType() const { return field<void *>(this, offsets::Block::mBlockType); }

    const std::string *fullName() const {
        auto type = reinterpret_cast<std::uintptr_t>(blockType());
        if (!type) return nullptr;
        auto hashed = type + offsets::BlockType::mNameInfo + offsets::NameInfo::mFullName;
        return reinterpret_cast<const std::string *>(hashed + offsets::HashedString::mString);
    }
};

}
