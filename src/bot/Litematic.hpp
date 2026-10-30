#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "levibatone/sdk/Types.hpp"
#include "Nbt.hpp"

// .litematic (Litematica) support: gzip + big-endian NBT, per-region
// BlockStatePalette with a bit-packed BlockStates long array (LSB-first,
// entries span long boundaries — unlike vanilla 1.16+ chunk format).
namespace levibatone::bot::litematic {

using nbt::Value;
using sdk::BlockPos;

struct BlockCell {
    BlockPos pos;      // absolute position (build origin + region offset + voxel)
    std::string name;  // Bedrock block name incl. namespace
};

// Parses all regions into cells relative to (0,0,0) — the caller anchors them
// to the player position. Returns false (with a reason logged by the caller)
// when the file is malformed or beyond the size cap.
bool parse(const std::uint8_t *data, std::size_t size, std::vector<BlockCell> &cells);

// Java → Bedrock block name mapping (namespace stripped before the lookup).
std::string toBedrockName(const std::string &javaName);

}
