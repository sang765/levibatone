#include "Bot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <fstream>

#include "../core/GameActions.hpp"
#include "Settings.hpp"
#include "World.hpp"
#include "levibatone/sdk/client/ClientInstance.hpp"
#include "levibatone/sdk/world/Actors.hpp"
#include "Nbt.hpp"
#include "Litematic.hpp"

namespace levibatone::bot {

namespace {

constexpr float kEyeHeight = 1.62f;
constexpr double kBlockReach = 4.5;

std::vector<std::string> splitArgs(const std::string &line) {
    std::vector<std::string> parts;
    std::istringstream stream(line);
    std::string part;
    while (stream >> part) {
        part.erase(std::remove_if(part.begin(), part.end(),
                                  [](unsigned char c) { return c == '\r' || c == '\n'; }),
                   part.end());
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Strip Minecraft section-sign color codes (0xC2 0xA7 prefix in UTF-8).
std::string stripColorCodes(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (static_cast<unsigned char>(input[i]) == 0xC2 &&
            i + 1 < input.size() &&
            static_cast<unsigned char>(input[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out.push_back(input[i]);
    }
    return out;
}

BlockPos feetBlock(const sdk::Vec3 &pos) {
    return {static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
            static_cast<int>(std::floor(pos.z))};
}

double distSq(const BlockPos &a, const BlockPos &b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

Bot &Bot::get() {
    static Bot instance;
    return instance;
}

void Bot::log(std::string text) {
    std::lock_guard lock(mMessageMutex);
    mMessages.push_back({std::move(text), 200});
    if (mMessages.size() > 6) mMessages.erase(mMessages.begin());
}

std::vector<Bot::Message> Bot::messages() {
    std::lock_guard lock(mMessageMutex);
    return mMessages;
}

std::string Bot::modeLabel() const {
    switch (mMode) {
    case Mode::Idle:
        return "idle";
    case Mode::Goto:
        return "goto";
    case Mode::GotoBlock:
        return "getto " + mMineTarget;
    case Mode::Mine:
        return "mine " + mMineTarget;
    case Mode::Follow:
        return "follow";
    case Mode::Build:
        return "build " + mBuildFile + " (" + std::to_string(mBuildCursor) + "/" +
               std::to_string(mBuildCells.size()) + ")";
    }
    return "?";
}

std::string Bot::miningTarget() const { return mMineTarget; }

void Bot::stop() {
    if (mMode == Mode::Idle && !mExecutor.hasPath()) return;
    mMode = Mode::Idle;
    mGoal.reset();
    mMineTarget.clear();
    mMineRemaining = 0;
    mMinedCount = 0;
    mBlacklist.clear();
    mDirectBreakTarget.reset();
    mFollowTarget = nullptr;
    mBuildCells.clear();
    mBuildCursor = 0;
    mDirectPlaceTarget.reset();
    mPendingPath.reset();
    mPendingScan.reset();
    mComputing = false;
    mScanComputing = false;
    ++mGeneration;
    mExecutor.reset();
}

void Bot::onWorldLeft() {
    mMode = Mode::Idle;
    mGoal.reset();
    mBlacklist.clear();
    mDirectBreakTarget.reset();
    mDirectPlaceTarget.reset();
    mFollowTarget = nullptr;
    mBuildCells.clear();
    mBuildCursor = 0;
    mPendingPath.reset();
    mPendingScan.reset();
    mComputing = false;
    mScanComputing = false;
    ++mGeneration;
    mExecutor.reset();
    mLevel = nullptr;
    World::get().refresh(nullptr);
    std::lock_guard lock(mMessageMutex);
    mMessages.clear();
}

std::vector<std::string> Bot::executeCommand(const std::string &line) {
    const std::vector<std::string> args = splitArgs(line);
    if (args.empty()) return {};

    const std::string command = toLower(args[0]);
    std::vector<std::string> feedback;

    if (command == "stop" || command == "cancel") {
        const bool wasActive = mMode != Mode::Idle;
        stop();
        return {wasActive ? "cancelled" : "nothing to cancel"};
    }

    if (command == "goto") {
        if (args.size() < 2) return {"usage: goto <x> [y] <z>"};
        Goal goal;
        goal.type = Goal::Type::Block;
        try {
            if (args.size() >= 4) {
                goal.x = std::stoi(args[1]);
                goal.y = std::stoi(args[2]);
                goal.z = std::stoi(args[3]);
            } else if (args.size() == 3) {
                goal.x = std::stoi(args[1]);
                goal.z = std::stoi(args[2]);
                goal.type = Goal::Type::XZ;
            } else {
                return {"usage: goto <x> [y] <z>"};
            }
        } catch (const std::exception &) {
            return {"invalid coordinates"};
        }
        ++mGeneration;
        mPendingPath.reset();
        mComputing = false;
        mMode = Mode::Goto;
        mGoal = goal;
        mMineTarget.clear();
        mMineRemaining = 0;
        mBlacklist.clear();
        mDirectBreakTarget.reset();
        mExecutor.reset();
        log("goto " + std::to_string(goal.x) + " " + std::to_string(goal.y) + " " + std::to_string(goal.z));
        return {"pathing to " + std::to_string(goal.x) + " " + std::to_string(goal.y) + " " +
                std::to_string(goal.z)};
    }

    if (command == "mine") {
        if (args.size() < 2) return {"usage: mine <block> [count]"};
        int count = 0;
        if (args.size() >= 3) {
            try {
                count = std::stoi(args[2]);
            } catch (const std::exception &) {
                return {"invalid count"};
            }
        }
        ++mGeneration;
        mPendingPath.reset();
        mComputing = false;
        mMode = Mode::Mine;
        mMineTarget = toLower(args[1]);
        mMineRemaining = count;
        mMinedCount = 0;
        mMineScanCooldown = 0;
        mExploreTries = 0;
        mGoal.reset();
        mBlacklist.clear();
        mDirectBreakTarget.reset();
        mExecutor.reset();
        log("mining " + mMineTarget);
        return {"mining " + mMineTarget};
    }

    if (command == "getto" || command == "getToBlock") {
        if (args.size() < 2) return {"usage: getto <block>"};
        ++mGeneration;
        mPendingPath.reset();
        mPendingScan.reset();
        mComputing = false;
        mScanComputing = false;
        mMode = Mode::GotoBlock;
        mMineTarget = toLower(args[1]);
        mMineScanCooldown = 0;
        mGoal.reset();
        mBlacklist.clear();
        mDirectBreakTarget.reset();
        mFollowTarget = nullptr;
        mExecutor.reset();
        log("getting to " + mMineTarget);
        return {"pathing to nearest " + mMineTarget};
    }

    if (command == "follow") {
        ++mGeneration;
        mPendingPath.reset();
        mPendingScan.reset();
        mComputing = false;
        mScanComputing = false;
        mMode = Mode::Follow;
        mFollowTarget = nullptr;
        mGoal.reset();
        mBlacklist.clear();
        mDirectBreakTarget.reset();
        mExecutor.reset();
        mFollowRefreshCooldown = 0;
        mFollowName = args.size() >= 2 ? toLower(args[1]) : "";
        log(mFollowName.empty() ? "following nearest player" : "following " + mFollowName);
        return {mFollowName.empty() ? "following nearest player" : "following " + mFollowName};
    }

    if (command == "build") {
        if (args.size() < 2) return {"usage: build <name.mcstructure>"};
        std::string file = args[1];
        if (file.find(".mcstructure") == std::string::npos &&
            file.find(".litematic") == std::string::npos) {
            file += ".litematic";
        }
        BlockPos origin = feetBlock(mLastTickPosition);
        ++mGeneration;
        mPendingPath.reset();
        mPendingScan.reset();
        mComputing = false;
        mScanComputing = false;
        mDirectBreakTarget.reset();
        mDirectPlaceTarget.reset();
        mBlacklist.clear();
        mExecutor.reset();
        if (!loadBuild(file, origin)) {
            mMode = Mode::Idle;
            return {"failed to load " + file + " (put it in the mod data folder)"};
        }
        mMode = Mode::Build;
        log("building " + file + ": " + std::to_string(mBuildCells.size()) + " cells");
        return {"building " + file};
    }

    if (command == "status") {
        feedback.push_back("mode: " + modeLabel());
        if (mGoal) {
            feedback.push_back("goal: " + std::to_string(mGoal->x) + " " + std::to_string(mGoal->y) + " " +
                               std::to_string(mGoal->z));
        }
        if (mExecutor.hasPath()) {
            const auto snap = mExecutor.snapshot();
            feedback.push_back("path: " + std::to_string(snap.edgeIndex) + "/" +
                               std::to_string(snap.edgeCount));
        }
        if (mMode == Mode::Mine) {
            feedback.push_back("mined: " + std::to_string(mMinedCount) +
                               (mMineRemaining > 0 ? " / " + std::to_string(mMineRemaining) : ""));
        }
        return feedback;
    }

    if (command == "set") {
        if (args.size() != 3) return {"usage: set <key> <value>"};
        return Settings::get().set(args[1], args[2]);
    }

    if (command == "settings") {
        return Settings::get().list();
    }

    if (command == "help") {
        return {
            "goto <x> [y] <z> - path to position",
            "mine <block> [count] - find and mine blocks",
            "stop - cancel the bot",
            "set <key> <value> - change a setting",
            "settings - list settings",
        };
    }

    return {"unknown command: " + command + " (try help)"};
}

bool Bot::blacklisted(const BlockPos &pos) const {
    return std::find(mBlacklist.begin(), mBlacklist.end(), pos) != mBlacklist.end();
}

void Bot::adoptPendingPath() {
    std::shared_ptr<const Path> pending;
    {
        std::lock_guard lock(mPendingMutex);
        pending = mPendingPath;
        mPendingPath.reset();
    }
    if (!pending) return;

    if (!pending->edges.empty()) {
        mExecutor.setPath(pending);
        log(pending->complete
                ? "path found (" + std::to_string(pending->edges.size()) + " moves)"
                : "partial path (" + std::to_string(pending->edges.size()) + " moves)");
        return;
    }

    if ((mMode == Mode::Mine || mMode == Mode::GotoBlock) && mGoal) {
        // blacklistClosestOnFailure: mark this target impossible, try the next.
        mBlacklist.push_back({mGoal->x, mGoal->y, mGoal->z});
        mGoal.reset();
        mMineScanCooldown = 0;
        log("target unreachable, trying next");
    } else if (mMode == Mode::Follow) {
        mGoal.reset();
        mFollowRefreshCooldown = 0;
        log("path to player failed, retrying");
    } else {
        log("no path found");
        stop();
    }
}

void Bot::requestPath(sdk::Player *player) {
    if (!player || !mGoal || mComputing.load()) return;

    const BlockPos start = feetBlock(player->position());
    if (!World::get().getBlock(start)) return; // not in loaded chunks yet

    const Goal goal = *mGoal;
    Pathfinder::Budget budget;
    budget.maxNodes = Settings::get().getInt("maxNodes", 15000);
    budget.timeoutMs = Settings::get().getInt("primaryTimeoutMs", 300);
    budget.failureTimeoutMs = Settings::get().getInt("failureTimeoutMs", 1500);

    const std::uint32_t generation = mGeneration;
    mComputing = true;
    std::thread([this, start, goal, budget, generation]() {
        auto path = Pathfinder::calculate(start, goal, budget);
        if (generation != mGeneration.load()) {
            mComputing = false;
            return;
        }
        {
            std::lock_guard lock(mPendingMutex);
            mPendingPath = std::move(path);
        }
        mComputing = false;
    }).detach();
}

void Bot::requestScan(const BlockPos &feet) {
    if (mScanComputing.load()) return;

    const std::string target = mMineTarget;
    std::vector<BlockPos> blacklist = mBlacklist;
    const int radius = std::clamp(Settings::get().getInt("mineScanRadius", 24), 4, 64);
    const int blockBudget = 16000; // hard cap on getBlock calls per scan
    const std::uint32_t generation = mGeneration;

    mScanComputing = true;
    std::thread([this, feet, target, blacklist, radius, blockBudget, generation]() {
        ScanResult result;
        const World &world = World::get();
        int checked = 0;
        for (int r = 1; r <= radius && checked < blockBudget; ++r) {
            for (int dx = -r; dx <= r && checked < blockBudget; ++dx) {
                for (int dz = -r; dz <= r && checked < blockBudget; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue; // shell surface only
                    for (int dy = -r; dy <= r && checked < blockBudget; ++dy) {
                        ++checked;
                        const BlockPos pos{feet.x + dx, feet.y + dy, feet.z + dz};
                        const void *block = world.getBlock(pos);
                        if (!block) continue;
                        if (world.classify(block) != World::Class::Solid) continue;
                        const std::string *name = static_cast<const sdk::Block *>(block)->fullName();
                        if (!name || !World::nameMatches(*name, target)) continue;
                        if (std::find(blacklist.begin(), blacklist.end(), pos) != blacklist.end()) continue;
                        result.matches.push_back(pos);
                    }
                }
            }
        }
        std::sort(result.matches.begin(), result.matches.end(),
                  [&feet](const BlockPos &a, const BlockPos &b) { return distSq(a, feet) < distSq(b, feet); });
        result.valid = true;
        result.exhausted = checked < blockBudget;
        if (generation != mGeneration.load()) {
            mScanComputing = false;
            return;
        }
        {
            std::lock_guard lock(mScanMutex);
            mPendingScan = std::move(result);
        }
        mScanComputing = false;
    }).detach();
}

void Bot::adoptPendingScan(const BlockPos &feet) {
    std::optional<ScanResult> pending;
    {
        std::lock_guard lock(mScanMutex);
        pending = std::move(mPendingScan);
        mPendingScan.reset();
    }
    if (!pending || !pending->valid) return;
    mMineScanCooldown = Settings::get().getInt("mineGoalUpdateInterval", 5);
    if (pending->matches.empty()) {
        if (mMode == Mode::Mine && !mGoal &&
            Settings::get().getBool("exploreForBlocks", true)) {
            // exploreForBlocks: head for unexplored ground instead of giving up.
            if (++mExploreTries > 12) {
                log("no " + mMineTarget + " found after exploring; stopping");
                stop();
                return;
            }
            static constexpr int kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1},
                                                {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
            const int dist = std::clamp(Settings::get().getInt("mineScanRadius", 24), 4, 64) + 8;
            const auto &[edx, edz] = kDirs[mExploreTries % 8];
            Goal goal;
            goal.type = Goal::Type::XZ;
            goal.x = feet.x + edx * dist;
            goal.z = feet.z + edz * dist;
            mGoal = goal;
            mExecutor.reset();
            log("exploring for " + mMineTarget + " (try " + std::to_string(mExploreTries) + ")");
            return;
        }
        if (!mGoal) {
            log("no " + mMineTarget + " found within " +
                std::to_string(std::clamp(Settings::get().getInt("mineScanRadius", 24), 4, 64)) + " blocks");
            stop();
        }
        return;
    }
    mExploreTries = 0;
    pickScanGoal(pending->matches, feet);
}

void Bot::pickScanGoal(const std::vector<BlockPos> &matches, const BlockPos &feet) {
    for (const BlockPos &pos : matches) {
        Goal goal;
        goal.type = Goal::Type::Near;
        goal.x = pos.x;
        goal.y = pos.y;
        goal.z = pos.z;
        goal.radius = 1;
        if (mGoal && mGoal->type == goal.type && mGoal->x == goal.x && mGoal->y == goal.y &&
            mGoal->z == goal.z) {
            return; // already pathing to this target
        }
        (void)feet;
        mGoal = goal;
        mExecutor.reset();
        return;
    }
}

void Bot::updateFollowGoal(sdk::Player *player) {
    auto fetch = sdk::fetchNearbyActors();
    if (!fetch || !player) return;

    const int radius = std::clamp(Settings::get().getInt("followRadius", 3), 0, 16);
    const sdk::Vec3 extent{static_cast<float>(Settings::get().getInt("followRange", 30)),
                           static_cast<float>(Settings::get().getInt("followRange", 30)),
                           static_cast<float>(Settings::get().getInt("followRange", 30))};
    const sdk::ActorVec actors = fetch(player, &extent, sdk::kMobActorTypeFilter);
    if (!actors.begin || !actors.end) {
        if (!mGoal && mFollowTarget == nullptr) {
            log("no matching player nearby to follow");
            mFollowRefreshCooldown = 40;
        }
        return;
    }

    auto isPlayer = sdk::actorIsPlayer();
    void *chosen = nullptr;
    for (auto it = actors.begin; it < actors.end; ++it) {
        if (!it->mActor || it->mActor == player) continue;
        if (isPlayer && !isPlayer(it->mActor)) continue;
        if (!mFollowName.empty()) {
            const std::string &raw =
                reinterpret_cast<sdk::Player *>(it->mActor)->name();
            const std::string name = toLower(stripColorCodes(raw));
            if (name.find(mFollowName) == std::string::npos) continue;
        }
        chosen = it->mActor;
        break; // already distance-sorted: nearest match first
    }
    if (!chosen) {
        if (!mGoal && mFollowTarget == nullptr) {
            log(mFollowName.empty() ? "no matching player nearby to follow"
                                    : "player '" + mFollowName + "' not in range");
            mFollowRefreshCooldown = 40;
        }
        return;
    }

    if (chosen != mFollowTarget || !mGoal) {
        mFollowTarget = chosen;
        const BlockPos targetPos = feetBlock(reinterpret_cast<sdk::Actor *>(chosen)->position());
        Goal goal;
        goal.type = Goal::Type::Near;
        goal.x = targetPos.x;
        goal.y = targetPos.y;
        goal.z = targetPos.z;
        goal.radius = radius;
        mGoal = goal;
        mExecutor.reset();
        log("following player at " + std::to_string(targetPos.x) + " " + std::to_string(targetPos.y) +
            " " + std::to_string(targetPos.z));
    } else {
        // Keep the goal box around the moving target.
        const BlockPos targetPos = feetBlock(reinterpret_cast<sdk::Actor *>(chosen)->position());
        if (mGoal && (std::abs(targetPos.x - mGoal->x) > radius || std::abs(targetPos.z - mGoal->z) > radius ||
                      std::abs(targetPos.y - mGoal->y) > radius)) {
            mGoal->x = targetPos.x;
            mGoal->y = targetPos.y;
            mGoal->z = targetPos.z;
            mExecutor.reset();
        }
    }
}

bool Bot::loadBuild(const std::string &file, const BlockPos &origin) {
    namespace fs = std::filesystem;
    mBuildCells.clear();
    mBuildCursor = 0;
    mBuildPlaced = 0;
    mBuildBroken = 0;
    mBuildSkipped = 0;
    mBuildCooldown = 0;
    mBuildFile = file;

    std::error_code ec;
    const fs::path path = mBuildDir / file;
    if (!fs::exists(path, ec)) {
        log("file not found: " + path.string());
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());

    if (file.find(".litematic") != std::string::npos) {
        std::vector<litematic::BlockCell> cells;
        if (!litematic::parse(bytes.data(), bytes.size(), cells)) {
            log("invalid .litematic (gzip/NBT/regions)");
            return false;
        }
        mBuildCells.reserve(cells.size());
        for (auto &cell : cells) {
            mBuildCells.push_back({BlockPos{origin.x + cell.pos.x, origin.y + cell.pos.y,
                                            origin.z + cell.pos.z},
                                   cell.name});
        }
        return !mBuildCells.empty();
    }

    nbt::Value root;
    if (!nbt::parseMcstructure(bytes.data(), bytes.size(), root)) {
        log("invalid .mcstructure NBT");
        return false;
    }
    const nbt::Compound *rootObj = root.asCompound();
    const auto sizeIt = rootObj ? rootObj->find("size") : rootObj->end();
    const nbt::List *size = sizeIt != rootObj->end() ? sizeIt->second.asList() : nullptr;
    if (!rootObj || !size || size->size() != 3) {
        log("missing size in structure");
        return false;
    }
    const int sx = (*size)[0].asInt(), sy = (*size)[1].asInt(), sz = (*size)[2].asInt();
    if (sx <= 0 || sy <= 0 || sz <= 0 || static_cast<std::int64_t>(sx) * sy * sz > 100000) {
        log("structure too large or invalid size");
        return false;
    }

    const auto structureIt = rootObj->find("structure");
    const nbt::Compound *structure = structureIt != rootObj->end() ? structureIt->second.asCompound() : nullptr;
    const auto indicesIt = structure ? structure->find("block_indices") : structure->end();
    const nbt::Compound *indices = indicesIt != structure->end() ? indicesIt->second.asCompound() : nullptr;
    const auto dataIt = indices ? indices->find("data") : indices->end();
    const nbt::List *layers = dataIt != indices->end() ? dataIt->second.asList() : nullptr;
    const auto paletteIt = structure ? structure->find("palette") : structure->end();
    const nbt::Compound *paletteRoot = paletteIt != structure->end() ? paletteIt->second.asCompound() : nullptr;
    const auto defaultIt = paletteRoot ? paletteRoot->find("default") : paletteRoot->end();
    const nbt::Compound *def = defaultIt != paletteRoot->end() ? defaultIt->second.asCompound() : nullptr;
    const auto blockPaletteIt = def ? def->find("block_palette") : def->end();
    const nbt::List *palette = blockPaletteIt != def->end() ? blockPaletteIt->second.asList() : nullptr;
    if (!layers || layers->empty() || !palette) {
        log("missing block_indices or block_palette");
        return false;
    }
    const nbt::List *layer0 = (*layers)[0].asList();
    if (!layer0) {
        log("invalid block_indices layer");
        return false;
    }

    mBuildCells.reserve(layer0->size());
    for (std::size_t i = 0; i < layer0->size(); ++i) {
        const std::int32_t index = (*layer0)[i].asInt(-1);
        if (index < 0 || index >= static_cast<std::int32_t>(palette->size())) continue;
        const nbt::Compound *entry = (*palette)[static_cast<std::size_t>(index)].asCompound();
        const auto nameIt = entry ? entry->find("name") : entry->end();
        const std::string *name = nameIt != entry->end() ? nameIt->second.asString() : nullptr;
        if (!name || name->empty()) continue;
        const int x = static_cast<int>(i % static_cast<std::size_t>(sx));
        const int z = static_cast<int>((i / static_cast<std::size_t>(sx)) % static_cast<std::size_t>(sz));
        const int y = static_cast<int>(i / (static_cast<std::size_t>(sx) * static_cast<std::size_t>(sz)));
        mBuildCells.push_back({BlockPos{origin.x + x, origin.y + y, origin.z + z}, *name});
    }

    // Bottom-up, nearest first (Baritone buildInLayers, layerOrder=false).
    std::sort(mBuildCells.begin(), mBuildCells.end(),
              [](const BuildCell &a, const BuildCell &b) {
                  if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
                  if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
                  return a.pos.z < b.pos.z;
              });
    return !mBuildCells.empty();
}

void Bot::beginDirectPlace(const PlaceTask &task) {
    mDirectPlaceTarget = task;
    mDirectPlaceTicks = 0;
}

bool Bot::tickDirectPlace(sdk::Player *player) {
    if (!mDirectPlaceTarget) return false;
    const PlaceTask task = *mDirectPlaceTarget;
    const World &world = World::get();
    if (world.isSolid(task.target)) {
        ++mBuildPlaced;
        ++mBuildCursor;
        mDirectPlaceTarget.reset();
        return true;
    }
    if (++mDirectPlaceTicks > Settings::get().getInt("movementTimeoutTicks", 100)) {
        ++mBuildSkipped;
        ++mBuildCursor;
        mDirectPlaceTarget.reset();
        log("cell skipped (place failed)");
        return true;
    }
    if (mBuildCooldown > 0) --mBuildCooldown;
    void *mode = core::actions.mode();
    auto startBuild = core::actions.startBuild.load(std::memory_order_acquire);
    if (mExecutor.lookBlock(player, task.click) && mode && startBuild && mBuildCooldown <= 0) {
        startBuild(mode, &task.click, task.face, 0);
        mBuildCooldown = 4;
    }
    mExecutor.tick(player);
    return true;
}

void Bot::tickBuild(sdk::Player *player) {
    if (mBuildCursor >= mBuildCells.size()) {
        log("build complete: " + std::to_string(mBuildPlaced) + " placed, " +
            std::to_string(mBuildBroken) + " broken, " + std::to_string(mBuildSkipped) + " skipped");
        stop();
        return;
    }

    if (mBuildCooldown > 0) --mBuildCooldown;
    if (tickDirectPlace(player)) return;

    const BuildCell &cell = mBuildCells[mBuildCursor];
    const World &world = World::get();
    const void *current = world.getBlock(cell.pos);
    const World::Class klass = world.classify(current);
    const bool wantsAir = World::nameMatches(cell.name, "air");

    const sdk::Vec3 eye{player->position().x, player->position().y + 1.62f, player->position().z};
    const sdk::Vec3 center = sdk::blockCenter(cell.pos);
    const float ddx = center.x - eye.x, ddy = center.y - eye.y, ddz = center.z - eye.z;
    const float reach = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);

    if (reach > static_cast<float>(kBlockReach)) {
        if (!mExecutor.hasPath() && !mGoal && !mComputing.load()) {
            Goal goal;
            goal.type = Goal::Type::Near;
            goal.x = cell.pos.x;
            goal.y = cell.pos.y;
            goal.z = cell.pos.z;
            goal.radius = 1;
            mGoal = goal;
        }
        return; // requestPath runs in onTick
    }

    if (wantsAir) {
        if (klass == World::Class::Solid) {
            if (!Settings::get().getBool("allowBreak")) {
                ++mBuildSkipped;
                ++mBuildCursor;
                return;
            }
            beginDirectBreak(cell.pos);
            return;
        }
        ++mBuildCursor; // already open
        return;
    }

    if (klass == World::Class::Solid || klass == World::Class::Unbreakable) {
        const void *block = current;
        const std::string *name = block ? static_cast<const sdk::Block *>(block)->fullName() : nullptr;
        if (name && World::nameMatches(*name, cell.name)) {
            ++mBuildCursor; // already correct
            return;
        }
        if (!Settings::get().getBool("allowBreak")) {
            ++mBuildSkipped;
            ++mBuildCursor;
            return;
        }
        beginDirectBreak(cell.pos);
        return;
    }

    // Empty cell: place the held item against a solid neighbour.
    std::optional<PlaceTask> task;
    const BlockPos below{cell.pos.x, cell.pos.y - 1, cell.pos.z};
    if (world.isSolid(below)) {
        task = PlaceTask{cell.pos, below, 1};
    } else {
        static const int kSides[4][3] = {{1, 0, 5}, {-1, 0, 4}, {0, 1, 3}, {0, -1, 2}};
        for (const auto &[sx, sz, face] : kSides) {
            const BlockPos side{cell.pos.x + sx, cell.pos.y, cell.pos.z + sz};
            if (world.isSolid(side)) {
                task = PlaceTask{cell.pos, side, static_cast<std::uint8_t>(face)};
                break;
            }
        }
    }
    if (!task) {
        ++mBuildSkipped;
        ++mBuildCursor;
        log("cell skipped (no support neighbour)");
        return;
    }
    if (!Settings::get().getBool("allowPlace")) {
        ++mBuildSkipped;
        ++mBuildCursor;
        return;
    }
    beginDirectPlace(*task);
}

void Bot::beginDirectBreak(const BlockPos &pos) {
    mDirectBreakTarget = pos;
    mDirectBreakTicks = 0;
}

void Bot::onPathFinished(sdk::Player *player) {
    if (mMode == Mode::Goto) {
        log("goal reached");
        stop();
        return;
    }
    if (mMode == Mode::GotoBlock) {
        log("block reached");
        stop();
        return;
    }
    if (mMode == Mode::Build) {
        mGoal.reset(); // arrived at the current cell; tickBuild evaluates in reach
        (void)player;
        return;
    }
    if (mMode == Mode::Mine && mGoal) {
        const BlockPos target{mGoal->x, mGoal->y, mGoal->z};
        if (World::get().isSolid(target)) {
            beginDirectBreak(target);
        } else {
            mGoal.reset();
            mMineScanCooldown = 0;
        }
        (void)player;
    }
    if (mMode == Mode::Follow) {
        // Continuous follow: refresh the goal to the target's current position.
        mGoal.reset();
        mFollowRefreshCooldown = 0;
    }
}

void Bot::onTick(sdk::Player *player) {
    // Age out HUD messages on every tick.
    {
        std::lock_guard lock(mMessageMutex);
        for (auto &message : mMessages) --message.ttlTicks;
        mMessages.erase(std::remove_if(mMessages.begin(), mMessages.end(),
                                       [](const Message &m) { return m.ttlTicks <= 0; }),
                        mMessages.end());
    }

    if (mMode == Mode::Idle) {
        mExecutor.tick(player);
        return;
    }

    if (!player) {
        mExecutor.reset();
        return;
    }

    // World/dimension change guard.
    const void *level = player->level();
    if (mLevel && level != mLevel) {
        onWorldLeft();
        return;
    }
    mLevel = level;

    if (!World::get().refresh(player->dimension() ? player->dimension()->blockSource() : nullptr)) {
        return;
    }

    // Teleport / respawn guard: a huge position jump invalidates the path.
    const sdk::Vec3 currentPos = player->position();
    {
        const float dx = currentPos.x - mLastTickPosition.x;
        const float dy = currentPos.y - mLastTickPosition.y;
        const float dz = currentPos.z - mLastTickPosition.z;
        if (mLastTickPosition.x != 0 || mLastTickPosition.y != 0 || mLastTickPosition.z != 0) {
            if (dx * dx + dy * dy + dz * dz > 50.0f * 50.0f) {
                log("position jumped, replanning");
                mExecutor.reset();
                mGoal.reset();
                mDirectBreakTarget.reset();
                mMineScanCooldown = 0;
            }
        }
        mLastTickPosition = currentPos;
    }

    adoptPendingPath();

    const BlockPos feet = feetBlock(player->position());

    if (mMode == Mode::Mine || mMode == Mode::GotoBlock) {
        if (!mExecutor.hasPath() && !mDirectBreakTarget && !mComputing.load() && !mScanComputing.load()) {
            if (mMineScanCooldown > 0) {
                --mMineScanCooldown;
            } else if (!mGoal) {
                requestScan(feet);
            }
        }
        if (mPendingScan) adoptPendingScan(feet);
    }

    if (mMode == Mode::Follow) {
        if (mFollowRefreshCooldown > 0) {
            --mFollowRefreshCooldown;
        } else {
            updateFollowGoal(player);
            mFollowRefreshCooldown = Settings::get().getInt("followUpdateInterval", 10);
        }
    }

    if (mMode == Mode::Build) {
        if (mDirectPlaceTarget) {
            tickDirectPlace(player);
            return;
        }
        if (mDirectBreakTarget) return; // shared direct-break handler below
        tickBuild(player);
        if (!mExecutor.hasPath() && mGoal && !mComputing.load()) requestPath(player);
        if (mExecutor.hasPath()) {
            mExecutor.tick(player);
            if (!mExecutor.hasPath()) onPathFinished(player);
        }
        return;
    }

    if (!mExecutor.hasPath() && !mDirectBreakTarget && mGoal && !mComputing.load()) {
        requestPath(player);
    }

    // Direct-break mode for mining: close enough to break without moving.
    if (mDirectBreakTarget) {
        const BlockPos target = *mDirectBreakTarget;
        const World &world = World::get();

        if (!world.isSolid(target)) {
            if (mMode == Mode::Build) {
                ++mBuildBroken;
                ++mBuildCursor;
                mDirectBreakTarget.reset();
                return;
            }
            ++mMinedCount;
            mDirectBreakTarget.reset();
            mGoal.reset();
            mMineScanCooldown = 0;
            if (mMineRemaining > 0 && --mMineRemaining == 0) {
                log("mined " + std::to_string(mMinedCount) + " " + mMineTarget);
                stop();
            }
            return;
        }

        if (++mDirectBreakTicks > Settings::get().getInt("movementTimeoutTicks", 100)) {
            if (mMode == Mode::Build) {
                ++mBuildSkipped;
                ++mBuildCursor;
                mDirectBreakTarget.reset();
                log("cell skipped (break failed)");
                return;
            }
            mBlacklist.push_back(target);
            mDirectBreakTarget.reset();
            mGoal.reset();
            mMineScanCooldown = 0;
            return;
        }

        const sdk::Vec3 eye{player->position().x, player->position().y + kEyeHeight, player->position().z};
        const sdk::Vec3 center = sdk::blockCenter(target);
        const float dx = center.x - eye.x, dy = center.y - eye.y, dz = center.z - eye.z;
        const float reach = std::sqrt(dx * dx + dy * dy + dz * dz);

        void *mode = core::actions.mode();
        auto startDestroy = core::actions.startDestroy.load(std::memory_order_acquire);
        if (reach <= static_cast<float>(kBlockReach) && mode && startDestroy) {
            // Face the block, then start destroying (called every tick to
            // accumulate progress, like Baritone's break handling).
            const float ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
            std::uint8_t face = 0;
            if (ay >= ax && ay >= az) face = dy > 0 ? 1 : 0;
            else if (az >= ax) face = dz > 0 ? 3 : 2;
            else face = dx > 0 ? 5 : 4;
            bool destroyed = false;
            mExecutor.lookBlock(player, target);
            startDestroy(mode, &target, face, &destroyed);
            mExecutor.tick(player); // keeps movement input neutral
        } else {
            // Out of reach (world changed under us): path there again.
            mDirectBreakTarget.reset();
            mExecutor.reset();
        }
        return;
    }

    if (mExecutor.hasPath()) {
        mExecutor.tick(player);
        if (!mExecutor.hasPath()) {
            onPathFinished(player);
        }
    }
}

}
