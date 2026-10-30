#include "Bot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "../core/GameActions.hpp"
#include "Settings.hpp"
#include "World.hpp"
#include "levibatone/sdk/client/ClientInstance.hpp"

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
    case Mode::Mine:
        return "mine " + mMineTarget;
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
    mPendingPath.reset();
    mComputing = false;
    ++mGeneration;
    mExecutor.reset();
}

void Bot::onWorldLeft() {
    mMode = Mode::Idle;
    mGoal.reset();
    mBlacklist.clear();
    mDirectBreakTarget.reset();
    mPendingPath.reset();
    mComputing = false;
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
        mGoal.reset();
        mBlacklist.clear();
        mDirectBreakTarget.reset();
        mExecutor.reset();
        log("mining " + mMineTarget);
        return {"mining " + mMineTarget};
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

    if (mMode == Mode::Mine && mGoal) {
        // blacklistClosestOnFailure: mark this target impossible, try the next.
        mBlacklist.push_back({mGoal->x, mGoal->y, mGoal->z});
        mGoal.reset();
        mMineScanCooldown = 0;
        log("target unreachable, trying next");
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

void Bot::updateMineGoal(sdk::Player *player, const BlockPos &feet) {
    if (mMineScanCooldown > 0) {
        --mMineScanCooldown;
        return;
    }

    const World &world = World::get();
    const int radius = std::clamp(Settings::get().getInt("mineScanRadius", 24), 4, 64);
    const int blockBudget = 12000; // hard cap on getBlock calls per scan

    std::vector<BlockPos> matches;
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
                    if (name && World::nameMatches(*name, mMineTarget) && !blacklisted(pos)) {
                        matches.push_back(pos);
                        if (matches.size() >= 16) break;
                    }
                }
            }
        }
    }

    mMineScanCooldown = Settings::get().getInt("mineGoalUpdateInterval", 5);

    if (matches.empty()) {
        if (!mGoal) {
            log("no " + mMineTarget + " found within " + std::to_string(radius) + " blocks");
            stop();
        }
        return;
    }

    std::sort(matches.begin(), matches.end(),
              [&feet](const BlockPos &a, const BlockPos &b) { return distSq(a, feet) < distSq(b, feet); });

    Goal goal;
    goal.type = Goal::Type::Near;
    goal.x = matches.front().x;
    goal.y = matches.front().y;
    goal.z = matches.front().z;
    goal.radius = 1;
    if (mGoal && mGoal->type == goal.type && mGoal->x == goal.x && mGoal->y == goal.y && mGoal->z == goal.z) return;
    mGoal = goal;
    mExecutor.reset();
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

    adoptPendingPath();

    const BlockPos feet = feetBlock(player->position());

    if (mMode == Mode::Mine && !mExecutor.hasPath() && !mDirectBreakTarget && !mComputing.load()) {
        updateMineGoal(player, feet);
    }

    if (!mExecutor.hasPath() && !mDirectBreakTarget && mGoal && !mComputing.load()) {
        requestPath(player);
    }

    // Direct-break mode for mining: close enough to break without moving.
    if (mDirectBreakTarget) {
        const BlockPos target = *mDirectBreakTarget;
        const World &world = World::get();

        if (!world.isSolid(target)) {
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
