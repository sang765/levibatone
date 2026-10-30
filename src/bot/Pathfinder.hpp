#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "levibatone/sdk/Types.hpp"

namespace levibatone::bot {

using sdk::BlockPos;
using sdk::Vec2;
using sdk::Vec3;

// Goal types matching Baritone's GoalBlock / GoalXZ / GoalNear.
struct Goal {
    enum class Type { Block, XZ, Near };

    Type type = Type::Block;
    int x = 0;
    int y = 0;
    int z = 0;
    int radius = 0;

    bool isInGoal(const BlockPos &pos) const;
    double heuristic(const BlockPos &pos) const;
};

// A block to place while executing the edge: target = where the new block
// should end up; click = the existing block to click and on which face.
struct PlaceTask {
    BlockPos target{};
    BlockPos click{};
    std::uint8_t face = 1; // 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
};

// A single movement from one block to the next, with the blocks that must be
// broken or placed first — the C++ analogue of Baritone's Movement classes.
struct PathEdge {
    enum class Kind { Walk, Diagonal, Ascend, Descend, Pillar };

    BlockPos dest{};
    Kind kind = Kind::Walk;
    double cost = 0.0;
    std::vector<BlockPos> toBreak;
    std::vector<PlaceTask> toPlace;
    bool jump = false;
    bool sneak = false;
};

struct Path {
    std::vector<PathEdge> edges;
    BlockPos start{};
    bool complete = false; // reached the goal (false = partial, budget exhausted)
    double cost = 0.0;

    std::size_t size() const { return edges.size(); }
};

// Baritone's A* over movements, with a node/time budget and best-so-far
// fallback so long-distance goals still produce a usable partial path.
class Pathfinder {
public:
    struct Budget {
        int maxNodes = 15000;
        int timeoutMs = 300;
        int failureTimeoutMs = 1500;
    };

    // Runs the search synchronously (call from the pathfinding worker thread).
    // The world is read through World::get().
    static std::shared_ptr<const Path> calculate(const BlockPos &start, const Goal &goal, const Budget &budget);

    // Filled in by the last calculate() call for HUD display.
    struct Stats {
        int nodesConsidered = 0;
        bool partial = false;
    };
    static Stats lastStats();
};

}
