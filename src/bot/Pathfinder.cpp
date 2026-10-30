#include "Pathfinder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <utility>

#include "Settings.hpp"
#include "World.hpp"

namespace levibatone::bot {

namespace {

// ActionCosts.java (baritone.api.pathing.movement), values verbatim.
constexpr double kWalkOneBlock = 20.0 / 4.317;
constexpr double kWalkOneInWater = 20.0 / 2.2;
constexpr double kLadderUpOne = 20.0 / 2.35;
constexpr double kLadderDownOne = 20.0 / 3.0;
constexpr double kWalkOffBlock = kWalkOneBlock * 0.8;
constexpr double kCenterAfterFall = kWalkOneBlock - kWalkOffBlock;
constexpr double kCostInf = 1000000.0;

double fallNTicks(double distance) {
    if (distance <= 0.0) return 0.0;
    double remaining = distance;
    int tick = 0;
    while (true) {
        const double velocity = (std::pow(0.98, tick) - 1.0) * -3.92;
        if (velocity <= 0.0) return tick + remaining / 1e-9;
        if (remaining <= velocity) return tick + remaining / velocity;
        remaining -= velocity;
        tick++;
    }
}

const double kJumpOneBlock = fallNTicks(1.25) - fallNTicks(0.25);

double fallCost(int blocks) { return fallNTicks(static_cast<double>(blocks)); }

std::uint64_t nodeKey(const BlockPos &p) {
    // 26 bits x, 26 bits z, 12 bits y (centered on ±2048) — Baritone-style packing.
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x) & 0x3FFFFFF) << 38)
         | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.z) & 0x3FFFFFF) << 12)
         | static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.y + 2048) & 0xFFF);
}

BlockPos keyPos(std::uint64_t key) {
    BlockPos p;
    p.x = static_cast<int>((key >> 38) & 0x3FFFFFF);
    if (p.x >= (1 << 25)) p.x -= (1 << 26);
    p.z = static_cast<int>((key >> 12) & 0x3FFFFFF);
    if (p.z >= (1 << 25)) p.z -= (1 << 26);
    p.y = static_cast<int>(key & 0xFFF) - 2048;
    return p;
}

struct NodeInfo {
    double g = 0.0;
    std::uint64_t parent = 0;
    PathEdge edge{};
    bool hasParent = false;
};

struct MovementCandidate {
    PathEdge edge;
    double cost = kCostInf;
};

const Settings &settings() { return Settings::get(); }

double breakCost(const World &world, const BlockPos &pos) {
    if (!settings().getBool("allowBreak")) return kCostInf;
    if (!world.isBreakable(pos)) return kCostInf;
    double cost = 5.0 + settings().getDouble("blockBreakAdditionalPenalty", 2.0);
    const void *block = world.getBlock(pos);
    if (block) {
        if (const std::string *name = static_cast<const sdk::Block *>(block)->fullName();
            name && World::isValuable(*name)) {
            cost *= 10.0; // avoidBreakingMultiplier: strongly prefer routes around these
        }
    }
    return cost;
}

double placeCost() {
    if (!settings().getBool("allowPlace")) return kCostInf;
    return settings().getDouble("blockPlacementPenalty", 20.0);
}

World::Class classAt(const World &world, const BlockPos &pos) { return world.classify(pos); }

// Cost to make `pos` passable: 0 when already passable, break cost when solid
// and breakable, COST_INF otherwise. Appends to toBreak when breaking.
double clearCost(const World &world, const BlockPos &pos, std::vector<BlockPos> &toBreak) {
    const World::Class klass = classAt(world, pos);
    if (klass == World::Class::Air || klass == World::Class::Liquid ||
        klass == World::Class::Passable || klass == World::Class::Climbable) {
        return 0.0;
    }
    if (klass == World::Class::Dangerous || klass == World::Class::Unknown) return kCostInf;
    const double cost = breakCost(world, pos);
    if (cost < kCostInf) toBreak.push_back(pos);
    return cost;
}

// Cost when `pos` must be a safe surface to stand on.
double standOnCost(const World &world, const BlockPos &pos) {
    const World::Class klass = classAt(world, pos);
    if (klass == World::Class::Solid || klass == World::Class::Unbreakable) return 0.0;
    return kCostInf;
}

bool dangerousAt(const World &world, const BlockPos &pos) {
    return classAt(world, pos) == World::Class::Dangerous;
}

MovementCandidate walkEdge(const World &world, const BlockPos &from, int dx, int dz, bool diagonal) {
    MovementCandidate candidate;
    const BlockPos dest{from.x + dx, from.y, from.z + dz};
    const BlockPos below{dest.x, dest.y - 1, dest.z};
    std::vector<BlockPos> toBreak;

    const double belowCost = standOnCost(world, below);
    if (belowCost >= kCostInf) {
        // Water below the destination means we would be swimming.
        if (classAt(world, below) != World::Class::Liquid) return candidate;
    }

    double cost = clearCost(world, dest, toBreak);
    if (cost >= kCostInf) return candidate;
    cost += clearCost(world, {dest.x, dest.y + 1, dest.z}, toBreak);
    if (cost >= kCostInf) return candidate;

    if (diagonal) {
        // Corner-cut prevention: both orthogonal neighbours must be passable
        // (breaking them is allowed, like Baritone's diagonal movement).
        cost += clearCost(world, {from.x + dx, from.y, from.z}, toBreak);
        if (cost >= kCostInf) return candidate;
        cost += clearCost(world, {from.x, from.y, from.z + dz}, toBreak);
        if (cost >= kCostInf) return candidate;
    }

    const World::Class feetClass = classAt(world, dest);
    const World::Class belowClass = classAt(world, below);
    if (feetClass == World::Class::Liquid) {
        cost += kWalkOneInWater;
        if (belowClass == World::Class::Liquid) {
            cost += settings().getDouble("walkOnWaterOnePenalty", 3.0);
        }
    } else if (belowClass == World::Class::Liquid) {
        return candidate; // cannot stand on a liquid surface without swimming into it
    } else {
        cost += diagonal ? kWalkOneBlock * std::sqrt(2.0) : kWalkOneBlock;
    }

    // Never step into a column that borders lava at foot level.
    if (dangerousAt(world, {dest.x + 1, dest.y, dest.z}) || dangerousAt(world, {dest.x - 1, dest.y, dest.z}) ||
        dangerousAt(world, {dest.x, dest.y, dest.z + 1}) || dangerousAt(world, {dest.x, dest.y, dest.z - 1})) {
        cost += 50.0;
    }

    candidate.edge.dest = dest;
    candidate.edge.kind = diagonal ? PathEdge::Kind::Diagonal : PathEdge::Kind::Walk;
    candidate.edge.toBreak = std::move(toBreak);
    candidate.edge.cost = cost;
    return candidate;
}

MovementCandidate ascendEdge(const World &world, const BlockPos &from, int dx, int dz) {
    MovementCandidate candidate;
    const BlockPos dest{from.x + dx, from.y + 1, from.z + dz};
    const BlockPos stepOn{dest.x, dest.y - 1, dest.z};
    std::vector<BlockPos> toBreak;

    if (standOnCost(world, stepOn) >= kCostInf) return candidate;

    double cost = clearCost(world, dest, toBreak);
    if (cost >= kCostInf) return candidate;
    cost += clearCost(world, {dest.x, dest.y + 1, dest.z}, toBreak);
    if (cost >= kCostInf) return candidate;
    // Jump arc needs headroom above the current head block.
    cost += clearCost(world, {from.x, from.y + 2, from.z}, toBreak);
    if (cost >= kCostInf) return candidate;

    cost += kWalkOneBlock + kJumpOneBlock + settings().getDouble("jumpPenalty", 2.0);

    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Ascend;
    candidate.edge.jump = true;
    candidate.edge.toBreak = std::move(toBreak);
    candidate.edge.cost = cost;
    return candidate;
}

MovementCandidate descendEdge(const World &world, const BlockPos &from, int dx, int dz, int maxFall) {
    MovementCandidate candidate;

    // Walk down the column: water catches falls from any height, solid ground
    // only within maxFallHeightNoWater (Baritone's rule).
    for (int n = 1; n <= 33; ++n) {
        const World::Class klass = classAt(world, {from.x + dx, from.y - n, from.z + dz});
        if (klass == World::Class::Unknown || klass == World::Class::Dangerous) return candidate;
        if (klass == World::Class::Liquid) {
            candidate.edge.dest = {from.x + dx, from.y - n, from.z + dz};
            candidate.edge.kind = PathEdge::Kind::Descend;
            candidate.edge.cost = kWalkOffBlock + fallCost(n) + kWalkOneInWater;
            return candidate;
        }
        if (klass == World::Class::Solid || klass == World::Class::Unbreakable) {
            const int landing = n - 1; // feet one above this block
            if (landing < 1 || landing > maxFall) return candidate;
            const BlockPos dest{from.x + dx, from.y - landing, from.z + dz};
            std::vector<BlockPos> toBreak;
            double cost = clearCost(world, dest, toBreak);
            if (cost >= kCostInf) return candidate;
            cost += clearCost(world, {dest.x, dest.y + 1, dest.z}, toBreak);
            if (cost >= kCostInf) return candidate;
            const World::Class ground = classAt(world, {dest.x, dest.y - 1, dest.z});
            if (ground != World::Class::Solid && ground != World::Class::Unbreakable) return candidate;
            candidate.edge.dest = dest;
            candidate.edge.kind = PathEdge::Kind::Descend;
            candidate.edge.toBreak = std::move(toBreak);
            candidate.edge.cost = kWalkOffBlock + fallCost(landing) + kCenterAfterFall;
            return candidate;
        }
        // Air / Passable / Climbable: keep falling.
    }
    return candidate;
}

MovementCandidate mineDownEdge(const World &world, const BlockPos &from) {
    MovementCandidate candidate;
    if (!settings().getBool("allowDownward")) return candidate;
    const BlockPos dest{from.x, from.y - 1, from.z};
    std::vector<BlockPos> toBreak;

    double cost = breakCost(world, dest);
    if (cost >= kCostInf) return candidate;
    toBreak.push_back(dest);

    const World::Class landing = classAt(world, {dest.x, dest.y - 1, dest.z});
    if (landing != World::Class::Solid && landing != World::Class::Unbreakable &&
        landing != World::Class::Liquid) {
        return candidate;
    }

    cost += fallCost(1) + kCenterAfterFall;
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Descend;
    candidate.edge.toBreak = std::move(toBreak);
    candidate.edge.cost = cost;
    return candidate;
}

MovementCandidate pillarEdge(const World &world, const BlockPos &from) {
    MovementCandidate candidate;
    const BlockPos dest{from.x, from.y + 1, from.z};
    std::vector<BlockPos> toBreak;

    double cost = clearCost(world, dest, toBreak);
    if (cost >= kCostInf) return candidate;
    cost += clearCost(world, {dest.x, dest.y + 1, dest.z}, toBreak);
    if (cost >= kCostInf) return candidate;

    const double place = placeCost();
    if (place >= kCostInf) return candidate;
    // The block we stand on becomes the pillar base; we place at the old feet.
    if (classAt(world, {from.x, from.y - 1, from.z}) != World::Class::Solid &&
        classAt(world, {from.x, from.y - 1, from.z}) != World::Class::Unbreakable) {
        return candidate;
    }

    cost += kJumpOneBlock + settings().getDouble("jumpPenalty", 2.0) + place;
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Pillar;
    candidate.edge.jump = true;
    candidate.edge.toBreak = std::move(toBreak);
    candidate.edge.toPlace.push_back(PlaceTask{from, {from.x, from.y - 1, from.z}, 1});
    candidate.edge.cost = cost;
    return candidate;
}

MovementCandidate ladderUpEdge(const World &world, const BlockPos &from) {
    MovementCandidate candidate;
    const BlockPos dest{from.x, from.y + 1, from.z};
    if (classAt(world, dest) != World::Class::Climbable) return candidate;
    std::vector<BlockPos> toBreak;
    double cost = clearCost(world, {dest.x, dest.y + 1, dest.z}, toBreak);
    if (cost >= kCostInf) return candidate;
    cost += kLadderUpOne;
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Ascend;
    candidate.edge.toBreak = std::move(toBreak);
    candidate.edge.cost = cost;
    return candidate;
}

MovementCandidate ladderDownEdge(const World &world, const BlockPos &from) {
    MovementCandidate candidate;
    const BlockPos dest{from.x, from.y - 1, from.z};
    if (classAt(world, dest) != World::Class::Climbable) return candidate;
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Descend;
    candidate.edge.cost = kLadderDownOne;
    return candidate;
}

MovementCandidate swimUpEdge(const World &world, const BlockPos &from) {
    MovementCandidate candidate;
    if (classAt(world, from) != World::Class::Liquid) return candidate;
    const BlockPos dest{from.x, from.y + 1, from.z};
    if (classAt(world, dest) != World::Class::Liquid) return candidate;
    const World::Class above = classAt(world, {dest.x, dest.y + 1, dest.z});
    if (above != World::Class::Liquid && above != World::Class::Air && above != World::Class::Passable) {
        return candidate;
    }
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Ascend;
    candidate.edge.cost = kWalkOneInWater * 1.5;
    return candidate;
}

MovementCandidate bridgeEdge(const World &world, const BlockPos &from, int dx, int dz) {
    MovementCandidate candidate;
    const double place = placeCost();
    if (place >= kCostInf) return candidate;
    const BlockPos dest{from.x + dx, from.y, from.z + dz};
    const BlockPos gap{dest.x, dest.y - 1, dest.z};
    if (classAt(world, gap) != World::Class::Air) return candidate;
    if (classAt(world, {from.x, from.y - 1, from.z}) != World::Class::Solid &&
        classAt(world, {from.x, from.y - 1, from.z}) != World::Class::Unbreakable) {
        return candidate;
    }
    // dest feet/head must already be passable (bridging is for open gaps).
    if (!world.canWalkThrough(dest)) return candidate;
    if (!world.canWalkThrough({dest.x, dest.y + 1, dest.z})) return candidate;
    // Worst case must be a survivable fall: first solid landing within 3.
    bool safeLanding = false;
    for (int d = 1; d <= 4; ++d) {
        const World::Class klass = classAt(world, {gap.x, gap.y - d, gap.z});
        if (klass == World::Class::Solid || klass == World::Class::Unbreakable) { safeLanding = true; break; }
        if (klass == World::Class::Unknown || klass == World::Class::Dangerous) break;
    }
    if (!safeLanding) return candidate;

    const std::uint8_t face = dx > 0 ? 5 : dx < 0 ? 4 : dz > 0 ? 3 : 2;
    candidate.edge.dest = dest;
    candidate.edge.kind = PathEdge::Kind::Walk;
    candidate.edge.sneak = true;
    candidate.edge.toPlace.push_back(PlaceTask{gap, {from.x, from.y - 1, from.z}, face});
    candidate.edge.cost = kWalkOneBlock + place;
    return candidate;
}

std::vector<MovementCandidate> successors(const World &world, const BlockPos &pos) {
    static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<MovementCandidate> result;
    result.reserve(13);

    const int maxFall = std::clamp(settings().getInt("maxFallHeightNoWater", 3), 0, 8);
    for (const auto &[dx, dz] : kDirs) {
        MovementCandidate candidate = walkEdge(world, pos, dx, dz, false);
        if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

        candidate = walkEdge(world, pos, dx, dz, true);
        if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

        candidate = ascendEdge(world, pos, dx, dz);
        if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

        candidate = descendEdge(world, pos, dx, dz, maxFall);
        if (candidate.cost < kCostInf) result.push_back(std::move(candidate));
    }

    MovementCandidate candidate = mineDownEdge(world, pos);
    if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

    candidate = pillarEdge(world, pos);
    if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

    candidate = ladderUpEdge(world, pos);
    if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

    candidate = ladderDownEdge(world, pos);
    if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

    candidate = swimUpEdge(world, pos);
    if (candidate.cost < kCostInf) result.push_back(std::move(candidate));

    for (const auto &[dx, dz] : kDirs) {
        candidate = bridgeEdge(world, pos, dx, dz);
        if (candidate.cost < kCostInf) result.push_back(std::move(candidate));
    }

    return result;
}

double goalHeuristic(const Goal &goal, const BlockPos &pos) {
    const double h = settings().getDouble("costHeuristic", 3.563);
    const int dx = std::abs(pos.x - goal.x);
    const int dz = std::abs(pos.z - goal.z);
    const int dy = pos.y - goal.y;
    if (goal.type == Goal::Type::Near) {
        return h * (std::max(0, dx - goal.radius) + std::max(0, dz - goal.radius) +
                    std::max(0, std::abs(dy) - goal.radius));
    }
    double cost = h * (dx + dz);
    if (goal.type != Goal::Type::XZ) {
        cost += dy < 0 ? std::min(fallCost(-dy), h * -dy) : h * dy;
    }
    return cost;
}

bool inGoal(const Goal &goal, const BlockPos &pos) {
    switch (goal.type) {
    case Goal::Type::Block:
        return pos.x == goal.x && pos.y == goal.y && pos.z == goal.z;
    case Goal::Type::XZ:
        return pos.x == goal.x && pos.z == goal.z;
    case Goal::Type::Near:
        return std::abs(pos.x - goal.x) <= goal.radius && std::abs(pos.z - goal.z) <= goal.radius &&
               std::abs(pos.y - goal.y) <= goal.radius;
    }
    return false;
}

} // namespace

bool Goal::isInGoal(const BlockPos &pos) const { return inGoal(*this, pos); }

double Goal::heuristic(const BlockPos &pos) const { return goalHeuristic(*this, pos); }

Pathfinder::Stats Pathfinder::lastStats() {
    static Stats stats;
    return stats;
}

std::shared_ptr<const Path> Pathfinder::calculate(const BlockPos &start, const Goal &goal, const Budget &budget) {
    const auto startTime = std::chrono::steady_clock::now();
    const auto elapsedMs = [&] {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - startTime)
                                    .count());
    };

    World &world = World::get();
    if (!world.getBlock(start)) return nullptr; // not in a loaded chunk

    std::unordered_map<std::uint64_t, NodeInfo> nodes;
    nodes.reserve(static_cast<std::size_t>(budget.maxNodes) + 16);

    using QueueEntry = std::pair<double, std::uint64_t>; // f, key
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;

    const std::uint64_t startKey = nodeKey(start);
    nodes[startKey] = NodeInfo{0.0, 0, PathEdge{}, false};
    open.emplace(goalHeuristic(goal, start), startKey);

    std::uint64_t bestKey = startKey;
    double bestH = goalHeuristic(goal, start);
    bool complete = false;
    int considered = 0;

    const int failureTimeout = std::max(budget.failureTimeoutMs, budget.timeoutMs);
    while (!open.empty()) {
        if (considered >= budget.maxNodes || elapsedMs() >= failureTimeout) {
            break;
        }
        if (inGoal(goal, keyPos(open.top().second))) {
            bestKey = open.top().second;
            complete = true;
            break;
        }

        const auto [f, key] = open.top();
        open.pop();
        const auto it = nodes.find(key);
        if (it == nodes.end()) continue;
        const NodeInfo &node = it->second;
        const BlockPos pos = keyPos(key);
        ++considered;

        const double h = goalHeuristic(goal, pos);
        if (h < bestH) {
            bestH = h;
            bestKey = key;
        }

        for (auto &candidate : successors(world, pos)) {
            const std::uint64_t nextKey = nodeKey(candidate.edge.dest);
            const double g = node.g + candidate.edge.cost;
            auto &next = nodes[nextKey];
            if (next.hasParent && next.g <= g) continue;
            const double h = goalHeuristic(goal, candidate.edge.dest);
            next.g = g;
            next.parent = key;
            next.edge = std::move(candidate.edge);
            next.hasParent = true;
            open.emplace(g + h, nextKey);
        }
    }

    auto path = std::make_shared<Path>();
    path->start = start;
    path->complete = complete;

    std::vector<std::uint64_t> chain;
    for (std::uint64_t key = bestKey; key != startKey;) {
        const auto it = nodes.find(key);
        if (it == nodes.end() || !it->second.hasParent) break;
        chain.push_back(key);
        key = it->second.parent;
    }
    std::reverse(chain.begin(), chain.end());
    double total = 0.0;
    for (const std::uint64_t key : chain) {
        const NodeInfo &node = nodes[key];
        path->edges.push_back(node.edge);
        total += node.edge.cost;
    }
    path->cost = total;

    lastStats() = Stats{considered, !complete};
    return path;
}

}
