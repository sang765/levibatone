#pragma once

#include <memory>

#include "levibatone/sdk/Types.hpp"
#include "levibatone/sdk/world/Actor.hpp"
#include "Pathfinder.hpp"

namespace levibatone::bot {

using sdk::BlockPos;
using sdk::Vec3;

// Follows a Path on the game thread, one tick at a time: injects movement
// input, turns the player, and performs the breaks and places each edge
// requires. The analogue of Baritone's PathExecutor.
class Executor {
public:
    struct Snapshot {
        std::size_t edgeIndex = 0;
        std::size_t edgeCount = 0;
        bool breaking = false;
        bool placing = false;
        bool waitingForPath = false;
        int stuckTicks = 0;
    };

    // Called from the pre-tick hook. Writes input for this tick.
    void tick(sdk::Player *player);

    void setPath(std::shared_ptr<const Path> path);
    void reset(sdk::Player *player = nullptr);
    bool hasPath() const { return mPath != nullptr; }
    Snapshot snapshot() const;

private:
    enum class Act { Move, Break, Place };

    struct Input {
        bool forward = false;
        bool back = false;
        bool left = false;
        bool right = false;
        bool jump = false;
        bool sneak = false;
        bool sprint = false;
        float analogX = 0.0f;
        float analogY = 0.0f;
    };

    bool applyInput(sdk::Player *player, const Input &input);
    void clearInput(sdk::Player *player);
    void lookAt(sdk::Player *player, const sdk::Vec3 &target, float maxStepYaw = 35.0f, float maxStepPitch = 30.0f);
    bool lookBlock(sdk::Player *player, const BlockPos &pos);

    bool breakBlock(sdk::Player *player, const BlockPos &pos, bool *instantlyBroke);
    void stopBreaking(sdk::Player *player, const BlockPos &pos);
    bool placeBlock(sdk::Player *player, const BlockPos &pos);

    void executeEdge(sdk::Player *player, const PathEdge &edge, const sdk::Vec3 &pos);

    std::shared_ptr<const Path> mPath;
    std::size_t mEdgeIndex = 0;
    Act mAct = Act::Move;
    std::size_t mActionIndex = 0;
    int mJumpHoldTicks = 0;
    int mPlaceCooldown = 0;
    int mEdgeTicks = 0;
    int mStuckTicks = 0;
    sdk::Vec3 mLastPosition{0, 0, 0};
    bool mBreakingNow = false;
    bool mPlacingNow = false;
};

}
