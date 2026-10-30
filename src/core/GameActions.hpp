#pragma once

#include <atomic>
#include <cstdint>

#include "levibatone/sdk/Types.hpp"
#include "levibatone/sdk/world/Actor.hpp"

namespace levibatone::core {

// Game function pointers resolved from signatures, plus the GameMode instance
// captured from interaction hooks (the player's GameMode is not reachable
// through a verified offset, so it is learned from the first manual
// break/place/use the player performs after joining a world).
struct GameActions {
    using DestroyFn = bool (*)(void *, const void *, std::uint8_t, bool *);
    using StopDestroyFn = void (*)(void *, const void *);
    using BuildFn = void (*)(void *, const void *, std::uint8_t, std::uint32_t);
    using TurnDeltaFn = void (*)(sdk::Actor *, const sdk::Vec2 &);

    TurnDeltaFn applyTurnDelta = nullptr;

    std::atomic<DestroyFn> startDestroy{nullptr};
    std::atomic<StopDestroyFn> stopDestroy{nullptr};
    std::atomic<BuildFn> startBuild{nullptr};
    std::atomic<void *> gameMode{nullptr};

    void *mode() const { return gameMode.load(std::memory_order_acquire); }
};

extern GameActions actions;

}
