#include "Executor.hpp"

#include <algorithm>
#include <cmath>

#include "../core/GameActions.hpp"
#include "Settings.hpp"
#include "World.hpp"
#include "levibatone/sdk/input/MoveInput.hpp"

namespace levibatone::bot {

namespace {

constexpr float kEyeHeight = 1.62f;
constexpr double kBlockReach = 4.5; // Baritone blockReachDistance

float wrapDegrees(float degrees) {
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return wrapped - 180.0f;
}

// Face of `target` hit by a ray from `eye` — Minecraft face ids:
// 0 down, 1 up, 2 north(-z), 3 south(+z), 4 west(-x), 5 east(+x).
std::uint8_t faceTowards(const sdk::Vec3 &eye, const sdk::Vec3 &center) {
    const float dx = center.x - eye.x;
    const float dy = center.y - eye.y;
    const float dz = center.z - eye.z;
    const float ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
    if (ay >= ax && ay >= az) return dy > 0 ? 1 : 0;
    if (az >= ax) return dz > 0 ? 3 : 2;
    return dx > 0 ? 5 : 4;
}

float distance(const sdk::Vec3 &a, const sdk::Vec3 &b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

BlockPos feetBlock(const sdk::Vec3 &pos) {
    return {static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
            static_cast<int>(std::floor(pos.z))};
}

} // namespace

Executor::Snapshot Executor::snapshot() const {
    Snapshot snap;
    if (mPath) {
        snap.edgeIndex = mEdgeIndex;
        snap.edgeCount = mPath->edges.size();
    }
    snap.breaking = mBreakingNow;
    snap.placing = mPlacingNow;
    snap.stuckTicks = mStuckTicks;
    return snap;
}

void Executor::setPath(std::shared_ptr<const Path> path) {
    mPath = std::move(path);
    mEdgeIndex = 0;
    mAct = Act::Move;
    mActionIndex = 0;
    mEdgeTicks = 0;
    mStuckTicks = 0;
    mJumpHoldTicks = 0;
}

void Executor::reset(sdk::Player *player) {
    if (player && mPath && mEdgeIndex < mPath->edges.size()) {
        // Release an in-progress destroy so the game does not keep mining.
        stopBreaking(player, mPath->edges[mEdgeIndex].toBreak.empty()
                                 ? mPath->edges[mEdgeIndex].dest
                                 : mPath->edges[mEdgeIndex].toBreak[mActionIndex]);
    }
    if (player) clearInput(player);
    mPath.reset();
    mAct = Act::Move;
    mActionIndex = 0;
    mEdgeIndex = 0;
    mBreakingNow = false;
    mPlacingNow = false;
    mStuckTicks = 0;
}

bool Executor::applyInput(sdk::Player *player, const Input &input) {
    auto *move = sdk::getMoveInputComponent(player);
    if (!move) return false;

    const auto set = [](sdk::MoveInputState &state, sdk::MoveInputFlag flag, bool value) {
        sdk::setFlag(state, flag, value);
    };
    for (sdk::MoveInputState *state : {&move->mInputState, &move->mRawInputState}) {
        set(*state, sdk::MoveInputFlag::Up, input.forward);
        set(*state, sdk::MoveInputFlag::Down, input.back);
        set(*state, sdk::MoveInputFlag::Left, input.left);
        set(*state, sdk::MoveInputFlag::Right, input.right);
        set(*state, sdk::MoveInputFlag::JumpDown, input.jump);
        set(*state, sdk::MoveInputFlag::SneakDown, input.sneak);
        set(*state, sdk::MoveInputFlag::SprintDown, input.sprint);
        state->mAnalogMoveVector = {input.analogX, input.analogY};
    }
    return true;
}

void Executor::clearInput(sdk::Player *player) {
    applyInput(player, Input{});
}

void Executor::lookAt(sdk::Player *player, const sdk::Vec3 &target, float maxStepYaw, float maxStepPitch) {
    if (!core::actions.applyTurnDelta) return;
    const sdk::Vec3 eye{player->position().x, player->position().y + kEyeHeight, player->position().z};
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float desiredYaw = std::atan2(-dx, dz) * (180.0f / 3.14159265f);
    const float desiredPitch = -std::atan2(dy, horizontal) * (180.0f / 3.14159265f);

    const sdk::Vec2 current = player->rotation();
    const float deltaYaw = std::clamp(wrapDegrees(desiredYaw - current.y), -maxStepYaw, maxStepYaw);
    const float deltaPitch =
        std::clamp(wrapDegrees(desiredPitch - current.x), -maxStepPitch, maxStepPitch);
    core::actions.applyTurnDelta(player, sdk::Vec2{deltaPitch, deltaYaw});
}

bool Executor::lookBlock(sdk::Player *player, const BlockPos &pos) {
    if (!core::actions.applyTurnDelta) return false;
    const sdk::Vec3 center = sdk::blockCenter(pos);
    lookAt(player, center, 45.0f, 40.0f);
    const sdk::Vec3 eye{player->position().x, player->position().y + kEyeHeight, player->position().z};
    return distance(eye, center) <= kBlockReach;
}

bool Executor::breakBlock(sdk::Player *player, const BlockPos &pos, bool *instantlyBroke) {
    void *mode = core::actions.mode();
    auto startDestroy = core::actions.startDestroy.load(std::memory_order_acquire);
    if (!mode || !startDestroy) return false;
    if (!lookBlock(player, pos)) return false;
    const sdk::Vec3 eye{player->position().x, player->position().y + kEyeHeight, player->position().z};
    const std::uint8_t face = faceTowards(eye, sdk::blockCenter(pos));
    bool destroyed = false;
    const bool result = startDestroy(mode, &pos, face, &destroyed);
    if (instantlyBroke) *instantlyBroke = destroyed;
    return result;
}

void Executor::stopBreaking(sdk::Player *player, const BlockPos &pos) {
    void *mode = core::actions.mode();
    auto stopDestroy = core::actions.stopDestroy.load(std::memory_order_acquire);
    if (mode && stopDestroy) stopDestroy(mode, &pos);
}

bool Executor::placeBlock(sdk::Player *player, const PlaceTask &task) {
    void *mode = core::actions.mode();
    auto startBuild = core::actions.startBuild.load(std::memory_order_acquire);
    if (!mode || !startBuild) return false;
    if (!World::get().isSolid(task.click)) return false;
    if (!lookBlock(player, task.click)) return false;
    startBuild(mode, &task.click, task.face, 0);
    return true;
}

void Executor::executeEdge(sdk::Player *player, const PathEdge &edge, const sdk::Vec3 &pos) {
    const World &world = World::get();

    if (mPlaceCooldown > 0) --mPlaceCooldown;

    // 1) Break any blocks this edge requires, closest first.
    if (mAct == Act::Move && mActionIndex < edge.toBreak.size()) {
        mAct = Act::Break;
    }
    if (mAct == Act::Break) {
        while (mActionIndex < edge.toBreak.size()) {
            const BlockPos &target = edge.toBreak[mActionIndex];
            if (!world.isSolid(target)) {
                ++mActionIndex;
                continue;
            }
            mBreakingNow = true;
            const sdk::Vec3 eye{pos.x, pos.y + kEyeHeight, pos.z};
            if (distance(eye, sdk::blockCenter(target)) > kBlockReach) {
                // Walk toward the target while it is out of reach
                // (Baritone's walkWhileBreaking).
                Input input;
                input.forward = true;
                input.analogY = 1.0f;
                input.sprint = Settings::get().getBool("allowSprint") && edge.kind != PathEdge::Kind::Pillar;
                lookBlock(player, target);
                applyInput(player, input);
                return;
            }
            bool instantly = false;
            breakBlock(player, target, &instantly);
            if (instantly || !world.isSolid(target)) {
                ++mActionIndex;
                if (mActionIndex >= edge.toBreak.size()) {
                    mAct = Act::Move;
                    mBreakingNow = false;
                }
                return;
            }
            applyInput(player, Input{});
            return;
        }
        mAct = Act::Move;
        mBreakingNow = false;
    }

    // 2) Place any blocks this edge requires.
    if (mAct == Act::Move && mActionIndex < edge.toPlace.size()) {
        mAct = Act::Place;
    }
    if (mAct == Act::Place) {
        while (mActionIndex < edge.toPlace.size()) {
            const PlaceTask &task = edge.toPlace[mActionIndex];
            if (world.isSolid(task.target)) {
                ++mActionIndex;
                continue;
            }
            mPlacingNow = true;
            if (mPlaceCooldown <= 0) {
                placeBlock(player, task);
                mPlaceCooldown = 4; // rightClickSpeed
            }
            Input hold;
            hold.sneak = edge.sneak;
            applyInput(player, hold);
            return;
        }
        mAct = Act::Move;
        mPlacingNow = false;
    }

    // 3) Steer toward the destination block.
    const sdk::Vec3 center = sdk::blockCenter(edge.dest);
    const float dx = center.x - pos.x;
    const float dz = center.z - pos.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float vertical = center.y - pos.y;

    Input input;
    input.forward = true;
    input.analogY = 1.0f;

    const bool inWater = world.classify(feetBlock(pos)) == World::Class::Liquid;
    input.sprint = Settings::get().getBool("allowSprint") && !inWater &&
                   edge.kind == PathEdge::Kind::Walk && edge.toBreak.empty();

    input.sneak = input.sneak || edge.sneak;

    switch (edge.kind) {
    case PathEdge::Kind::Ascend:
        if (!edge.jump) break; // ladder / swim ascend: forward press is enough
        // Jump once close to the base of the step.
        if (horizontal < 1.1f && vertical > 0.1f && mJumpHoldTicks <= 0) {
            mJumpHoldTicks = 6;
        }
        if (mJumpHoldTicks > 0) {
            input.jump = true;
            --mJumpHoldTicks;
        }
        break;
    case PathEdge::Kind::Pillar:
        input.forward = false;
        input.analogY = 0.0f;
        if (mJumpHoldTicks <= 0) mJumpHoldTicks = 5;
        if (mJumpHoldTicks > 0) {
            input.jump = true;
            --mJumpHoldTicks;
        } else {
            mJumpHoldTicks = 5;
        }
        // Place the pillar block under the feet while rising.
        if (vertical > 0.35f && mPlaceCooldown <= 0 && !edge.toPlace.empty()) {
            placeBlock(player, edge.toPlace.front());
            mPlaceCooldown = 3;
        }
        break;
    case PathEdge::Kind::Descend:
        // Walk off the edge; gravity handles the rest.
        break;
    case PathEdge::Kind::Walk:
    case PathEdge::Kind::Diagonal:
        break;
    }

    lookAt(player, {center.x, center.y + (edge.kind == PathEdge::Kind::Descend ? -0.6f : 0.2f), center.z});
    applyInput(player, input);
}

void Executor::tick(sdk::Player *player) {
    if (!player) return;
    if (!mPath) {
        clearInput(player);
        return;
    }

    const sdk::Vec3 pos = player->position();
    const World &world = World::get();

    if (mEdgeIndex >= mPath->edges.size()) {
        reset(player);
        return;
    }

    const PathEdge &edge = mPath->edges[mEdgeIndex];
    const BlockPos feet = feetBlock(pos);

    // Advance over reached destinations.
    while (mEdgeIndex < mPath->edges.size() && feet == mPath->edges[mEdgeIndex].dest) {
        ++mEdgeIndex;
        mAct = Act::Move;
        mActionIndex = 0;
        mEdgeTicks = 0;
        mStuckTicks = 0;
        mJumpHoldTicks = 0;
    }
    if (mEdgeIndex >= mPath->edges.size()) {
        reset(player);
        return;
    }

    // Stuck detection: no meaningful movement for 20 ticks triggers a replan
    // upstream (the Bot drops the path when stuckTicks grows past a threshold).
    const float moved = distance(pos, mLastPosition);
    if (moved < 0.01f) {
        ++mStuckTicks;
    } else {
        mStuckTicks = 0;
    }
    mLastPosition = pos;

    ++mEdgeTicks;
    const int timeout = static_cast<int>(edge.cost) * 4 +
                        Settings::get().getInt("movementTimeoutTicks", 100);
    if (mEdgeTicks > timeout || mStuckTicks > 20) {
        reset(player);
        return;
    }

    // Off-path recovery: the feet block is neither the previous destination nor
    // the current one — drop the path and let the Bot replan.
    const bool onTrack = feet == edge.dest || feet.x == edge.dest.x || feet.z == edge.dest.z;
    if (!onTrack && mEdgeTicks > 5) {
        reset(player);
        return;
    }

    executeEdge(player, mPath->edges[mEdgeIndex], pos);
}

}
