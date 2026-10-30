#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "levibatone/sdk/Types.hpp"
#include "levibatone/sdk/world/Actor.hpp"
#include "Executor.hpp"
#include "Pathfinder.hpp"

namespace levibatone::bot {

using sdk::BlockPos;

// Top-level bot orchestration: owns the active task (goto / mine), schedules
// A* calculations and mine scans on a worker thread, drives the executor on
// the game thread, and implements the command surface (#goto, #mine, ...).
class Bot {
public:
    enum class Mode { Idle, Goto, GotoBlock, Mine, Farm, Follow, Build, Explore };

    struct Message {
        std::string text;
        int ttlTicks = 200;
    };

    static Bot &get();

    // Game-thread entry points (called from the pre-tick hook).
    void onTick(sdk::Player *player);
    void onWorldLeft();

    // Command surface. Returns feedback lines for the HUD.
    std::vector<std::string> executeCommand(const std::string &line);

    void stop();
    void log(std::string text);

    Mode mode() const { return mMode; }
    std::string modeLabel() const;
    const Goal *goal() const { return mGoal ? &*mGoal : nullptr; }
    Executor::Snapshot executorSnapshot() const { return mExecutor.snapshot(); }
    bool executorHasPath() const { return mExecutor.hasPath(); }
    int minedCount() const { return mMinedCount; }
    int mineRemaining() const { return mMineRemaining; }
    std::vector<Message> messages();
    std::string miningTarget() const;
    void setBuildDir(const std::filesystem::path &dir) { mBuildDir = dir; }

private:
    Bot() = default;

    void requestPath(sdk::Player *player);
    void adoptPendingPath();
    void requestScan(const BlockPos &feet);
    void adoptPendingScan(const BlockPos &feet);
    void pickScanGoal(const std::vector<BlockPos> &matches, const BlockPos &feet);
    void updateFollowGoal(sdk::Player *player);
    void onPathFinished(sdk::Player *player);
    bool blacklisted(const BlockPos &pos) const;
    void beginDirectBreak(const BlockPos &pos);

    // Builder (.mcstructure) state machine.
    struct BuildCell {
        BlockPos pos;
        std::string name; // full name incl. namespace, or "minecraft:air"
    };
    bool loadBuild(const std::string &file, const BlockPos &origin);
    void tickBuild(sdk::Player *player);
    bool tickDirectPlace(sdk::Player *player);
    void beginDirectPlace(const PlaceTask &task);
    bool directBreakInReach(sdk::Player *player, const BlockPos &target);

    Mode mMode = Mode::Idle;
    std::optional<Goal> mGoal;
    std::string mMineTarget;
    int mMineRemaining = 0;
    int mMinedCount = 0;
    int mMineScanCooldown = 0;
    int mFollowRefreshCooldown = 0;
    void *mFollowTarget = nullptr;
    std::string mFollowName;
    sdk::Vec3 mLastTickPosition{0, 0, 0};
    int mExploreTries = 0;
    std::vector<BlockPos> mBlacklist;
    std::optional<BlockPos> mDirectBreakTarget;
    int mDirectBreakTicks = 0;
    std::atomic<std::uint32_t> mGeneration{0};

    // Builder state.
    std::filesystem::path mBuildDir;
    std::vector<BuildCell> mBuildCells;
    std::size_t mBuildCursor = 0;
    std::string mBuildFile;
    int mBuildPlaced = 0;
    int mBuildBroken = 0;
    int mBuildSkipped = 0;
    int mBuildCooldown = 0;
    std::optional<PlaceTask> mDirectPlaceTarget;
    int mDirectPlaceTicks = 0;

    // Asynchronous block scan (worker thread) for mine / getto goals.
    struct ScanResult {
        bool valid = false;
        std::vector<BlockPos> matches;
        bool exhausted = false;
    };
    std::mutex mScanMutex;
    std::optional<ScanResult> mPendingScan;
    std::atomic<bool> mScanComputing{false};

    Executor mExecutor;

    std::shared_ptr<const Path> mPendingPath;
    std::mutex mPendingMutex;
    std::atomic<bool> mComputing{false};

    std::mutex mMessageMutex;
    std::vector<Message> mMessages;

    // Identity guard: drop all work when the world or dimension changes.
    const void *mLevel = nullptr;
};

}
