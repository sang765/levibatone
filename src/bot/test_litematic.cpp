// Assert-based self-check for the .litematic parser (host-side test).
// Build & run: clang++ -std=c++20 -Isrc/bot src/bot/Nbt.cpp src/bot/Gzip.cpp src/bot/Litematic.cpp src/bot/test_litematic.cpp -lz -o litematic_test && ./litematic_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

#include "Litematic.hpp"

using levibatone::bot::litematic::BlockCell;
using levibatone::sdk::BlockPos;

namespace {

void put16be(std::vector<std::uint8_t> &b, std::uint16_t v) {
    b.push_back(v >> 8);
    b.push_back(v & 0xFF);
}

void put32be(std::vector<std::uint8_t> &b, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xFF);
}

void put64be(std::vector<std::uint8_t> &b, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xFF);
}

void putStr(std::vector<std::uint8_t> &b, const std::string &s) {
    put16be(b, static_cast<std::uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// Raw big-endian NBT: one 2x1x2 region, palette [stone, air], bits=2,
// entries (order [y][z][x]): 0, 1, 0, 0 → long 0 = 0b0100 = 0x04.
std::vector<std::uint8_t> makeRawNbt() {
    std::vector<std::uint8_t> nbt;
    nbt.push_back(10);
    putStr(nbt, "Litematica");
    nbt.push_back(9);
    putStr(nbt, "Regions");
    nbt.push_back(10);
    put32be(nbt, 1);
    nbt.push_back(10);
    putStr(nbt, "Position");
    for (const char *k : {"x", "y", "z"}) {
        nbt.push_back(3);
        putStr(nbt, k);
        put32be(nbt, 0);
    }
    nbt.push_back(0);
    nbt.push_back(10);
    putStr(nbt, "Size");
    for (const char *k : {"x", "y", "z"}) {
        nbt.push_back(3);
        putStr(nbt, k);
        put32be(nbt, k[0] == 'x' ? 2 : k[0] == 'y' ? 1 : 2);
    }
    nbt.push_back(0);
    nbt.push_back(9);
    putStr(nbt, "BlockStatePalette");
    nbt.push_back(10);
    put32be(nbt, 2);
    for (const char *name : {"minecraft:stone", "minecraft:air"}) {
        nbt.push_back(8);
        putStr(nbt, "Name");
        putStr(nbt, name);
        nbt.push_back(0);
    }
    nbt.push_back(12); // TAG_Long_Array
    putStr(nbt, "BlockStates");
    put32be(nbt, 1);
    put64be(nbt, 0x04ULL);
    nbt.push_back(0); // end region
    nbt.push_back(0); // end root
    return nbt;
}

std::vector<std::uint8_t> gzipWrap(const std::vector<std::uint8_t> &raw) {
    std::vector<std::uint8_t> gz{0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03};
    z_stream stream{};
    deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<std::uint8_t> out(deflateBound(&stream, static_cast<uLong>(raw.size())));
    stream.next_in = const_cast<std::uint8_t *>(raw.data());
    stream.avail_in = static_cast<uInt>(raw.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    deflateEnd(&stream);
    gz.insert(gz.end(), out.data(), out.data() + (out.size() - stream.avail_out));
    const std::uint32_t crc = crc32(0, raw.data(), static_cast<uInt>(raw.size()));
    for (int i = 0; i < 4; ++i) gz.push_back((crc >> (8 * i)) & 0xFF);
    for (int i = 0; i < 4; ++i) {
        gz.push_back((static_cast<std::uint32_t>(raw.size()) >> (8 * i)) & 0xFF);
    }
    return gz;
}

} // namespace

int main() {
    const std::vector<std::uint8_t> gz = gzipWrap(makeRawNbt());

    std::vector<BlockCell> cells;
    assert(levibatone::bot::litematic::parse(gz.data(), gz.size(), cells));
    assert(cells.size() == 4);

    // Sorted bottom-up, nearest first. Entries [0,1,0,0] → stone,air,stone,stone.
    assert(cells[0].name == "minecraft:stone");
    assert(cells[0].pos == (BlockPos{0, 0, 0}));
    assert(cells[1].pos == (BlockPos{0, 0, 1}));
    assert(cells[1].name == "minecraft:stone");
    assert(cells[2].pos == (BlockPos{1, 0, 0}));
    assert(cells[2].name == "minecraft:air");
    assert(cells[3].pos == (BlockPos{1, 0, 1}));
    assert(cells[3].name == "minecraft:stone");

    // Java→Bedrock mapping.
    assert(levibatone::bot::litematic::toBedrockName("minecraft:grass_block") == "minecraft:grass");
    assert(levibatone::bot::litematic::toBedrockName("stone") == "minecraft:stone");

    // Truncated input must fail cleanly.
    std::vector<BlockCell> broken;
    assert(!levibatone::bot::litematic::parse(gz.data(), gz.size() / 3, broken));

    std::printf("litematic self-check passed\n");
    return 0;
}
