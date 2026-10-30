// Assert-based self-check for the .mcstructure NBT parser (host-side test).
// Build & run: clang++ -std=c++20 src/bot/Nbt.cpp src/bot/test_nbt.cpp -Isrc -o nbt_test && ./nbt_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Nbt.hpp"

using namespace levibatone::bot::nbt;

static void put16(std::vector<std::uint8_t> &b, std::uint16_t v) {
    b.push_back(v & 0xFF);
    b.push_back(v >> 8);
}

static void put32(std::vector<std::uint8_t> &b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF);
}

static void putStr(std::vector<std::uint8_t> &b, const std::string &s) {
    put16(b, static_cast<std::uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// Structure 2x1x2: palette [stone, air], layer0 indices [0, 1, 0, 0],
// second layer all -1. Origin [10, 64, -20].
static std::vector<std::uint8_t> makeStructure() {
    std::vector<std::uint8_t> b;
    b.push_back(10); // TAG_Compound root
    putStr(b, "");
    // "size" IntArray [2,1,2]
    b.push_back(11);
    putStr(b, "size");
    put32(b, 3);
    put32(b, 2);
    put32(b, 1);
    put32(b, 2);
    // "structure_world_origin" IntArray [10,64,-20]
    b.push_back(11);
    putStr(b, "structure_world_origin");
    put32(b, 3);
    put32(b, 10);
    put32(b, 64);
    put32(b, static_cast<std::uint32_t>(-20));
    // "structure" compound
    b.push_back(10);
    putStr(b, "structure");
    // structure.block_indices compound
    b.push_back(10);
    putStr(b, "block_indices");
    // block_indices.data list of 2 int arrays
    b.push_back(9); // TAG_List
    putStr(b, "data");
    b.push_back(11); // element type: TAG_Int_Array
    put32(b, 2);
    // layer 0
    put32(b, 4);
    put32(b, 0);
    put32(b, 1);
    put32(b, 0);
    put32(b, 0);
    // layer 1 (all -1)
    put32(b, 4);
    for (int i = 0; i < 4; ++i) put32(b, static_cast<std::uint32_t>(-1));
    b.push_back(0); // end block_indices
    // structure.palette compound
    b.push_back(10);
    putStr(b, "palette");
    b.push_back(10);
    putStr(b, "default");
    b.push_back(9); // block_palette: list of compounds
    putStr(b, "block_palette");
    b.push_back(10); // element type compound
    put32(b, 2);
    for (const char *name : {"minecraft:stone", "minecraft:air"}) {
        b.push_back(8);
        putStr(b, "name");
        putStr(b, name);
        b.push_back(0); // end entry
    }
    b.push_back(0); // end default
    b.push_back(0); // end palette
    b.push_back(0); // end structure
    b.push_back(0); // end root
    return b;
}

int main() {
    const auto bytes = makeStructure();
    Value root;
    assert(parseMcstructure(bytes.data(), bytes.size(), root));
    assert(root.isCompound());

    const Compound *obj = root.asCompound();
    assert(obj->count("size") == 1);
    const List *size = obj->at("size").asList();
    assert(size && size->size() == 3);
    assert((*size)[0].asInt() == 2 && (*size)[1].asInt() == 1 && (*size)[2].asInt() == 2);

    const Compound *structure = obj->at("structure").asCompound();
    assert(structure);
    const List *layers = structure->at("block_indices").asCompound()->at("data").asList();
    assert(layers && layers->size() == 2);
    const List *layer0 = (*layers)[0].asList();
    assert(layer0 && layer0->size() == 4);
    assert((*layer0)[1].asInt() == 1);

    const List *palette =
        structure->at("palette").asCompound()->at("default").asCompound()->at("block_palette").asList();
    assert(palette && palette->size() == 2);
    const std::string *stoneName = (*palette)[0].asCompound()->at("name").asString();
    assert(stoneName && *stoneName == "minecraft:stone");

    // Decode like Bot::loadBuild does: [y][z][x], x fastest.
    const int sx = 2, sz = 2;
    for (std::size_t i = 0; i < layer0->size(); ++i) {
        const int x = static_cast<int>(i % sx);
        const int z = static_cast<int>((i / sx) % sz);
        const int y = static_cast<int>(i / (sx * sz));
        assert(x >= 0 && x < sx && z >= 0 && z < sz && y >= 0);
    }
    assert((*layer0)[3].asInt() == 0);

    // Truncated input must fail, not crash.
    Value broken;
    assert(!parseMcstructure(bytes.data(), bytes.size() / 2, broken));

    std::printf("nbt self-check passed\n");
    return 0;
}
