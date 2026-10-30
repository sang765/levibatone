#pragma once

#include <cstdint>

namespace levibatone::sdk {

struct Vec2 { float x; float y; };
struct Vec3 { float x; float y; float z; };
struct BlockPos { int x; int y; int z; };
struct AABB { Vec3 min; Vec3 max; };

inline bool operator==(const BlockPos &a, const BlockPos &b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline bool operator!=(const BlockPos &a, const BlockPos &b) { return !(a == b); }

struct BlockPosHash {
    std::uint64_t operator()(const BlockPos &p) const noexcept {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x)) << 32)
             ^ static_cast<std::uint32_t>(p.y * 73856093u)
             ^ static_cast<std::uint32_t>(p.z * 19349663u);
    }
};

inline Vec3 blockCenter(const BlockPos &p) {
    return {p.x + 0.5f, p.y + 0.5f, p.z + 0.5f};
}

}
